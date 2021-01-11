#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_OPENEVSE)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <MicroDebug.h>

#include "openevse.h"

#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval

OpenEVSEClass::OpenEVSEClass() :
  _sender(NULL),
  _connected(false),
  _protocol(OPENEVSE_ENCODE_VERSION(1,0,0)),
  _boot(NULL),
  _state(NULL),
  _wifi(NULL)
{
}

void OpenEVSEClass::begin(RapiSender &sender, std::function<void(bool connected)> callback)
{
  _connected = false;
  _sender = &sender;

  _sender->setOnEvent([this]() { onEvent(); });
  _sender->enableSequenceId(0);

  getVersion([this, callback](int ret, const char *firmware, const char *protocol) {
    if (RAPI_RESPONSE_OK == ret) {
      int major, minor, patch;
      if(3 == sscanf(protocol, "%d.%d.%d", &major, &minor, &patch))
      {
        _protocol = OPENEVSE_ENCODE_VERSION(major, minor, patch);
        DBUGVAR(_protocol);
        _connected = true;
      }
    }

    callback(_connected);
  });
}

void OpenEVSEClass::getVersion(std::function<void(int ret, const char *firmware, const char *protocol)> callback)
{
  if (!_sender) {
    return;
  }

  // Check OpenEVSE version is in.
  _sender->sendCmd("$GV", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 3)
      {
        const char *firmware = _sender->getToken(1);
        const char *protocol = _sender->getToken(2);

        callback(RAPI_RESPONSE_OK, firmware, protocol);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, NULL, NULL);
      }
    } else {
      callback(ret, NULL, NULL);
    }
  });
}

void OpenEVSEClass::getStatus(std::function<void(int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)> callback)
{
  if (!_sender) {
    return;
  }

  // Check state the OpenEVSE is in.
  _sender->sendCmd("$GS", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      int tokens_required = (_protocol < OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) ? 3 : 5;
      int state_base = (_protocol < OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) ? 10 : 16;
      if(_sender->getTokenCnt() >= tokens_required)
      {
        const char *val = _sender->getToken(1);
        uint8_t evse_state = strtol(val, NULL, state_base);

        val = _sender->getToken(2);
        uint32_t elapsed = strtol(val, NULL, 10);

        uint8_t pilot_state = OPENEVSE_STATE_INVALID;
        uint32_t vflags = 0;

        if(_protocol >= OPENEVSE_OCPP_SUPPORT_PROTOCOL_VERSION) {
          val = _sender->getToken(3);
          pilot_state = strtol(val, NULL, state_base);

          val = _sender->getToken(4);
          vflags = strtol(val, NULL, 16);
        }

        DBUGF("evse_state = %02x, elapsed = %d, pilot_state = %02x, vflags = %08x", evse_state, elapsed, pilot_state, vflags);
        callback(RAPI_RESPONSE_OK, evse_state, elapsed, pilot_state, vflags);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, OPENEVSE_STATE_INVALID, 0, OPENEVSE_STATE_INVALID, 0);
      }
    } else {
      callback(ret, OPENEVSE_STATE_INVALID, 0, OPENEVSE_STATE_INVALID, 0);
    }
  });
}

void OpenEVSEClass::getTime(std::function<void(int ret, time_t time)> callback)
{
  if (!_sender) {
    return;
  }

  // GT - get time (RTC)
  // response: $OK yr mo day hr min sec       yr=2-digit year
  // $GT^37

  _sender->sendCmd("$GT", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 7)
      {
        long year = strtol(_sender->getToken(1), NULL, 10);
        long month = strtol(_sender->getToken(2), NULL, 10);
        long day = strtol(_sender->getToken(3), NULL, 10);
        long hour = strtol(_sender->getToken(4), NULL, 10);
        long minute = strtol(_sender->getToken(5), NULL, 10);
        long second = strtol(_sender->getToken(6), NULL, 10);

        DBUGF("Got time %ld %ld %ld %ld %ld %ld", year, month, day, hour, minute, second);

        if(165 != year && 165 != month && 165 != day && 165 != hour && 165 != minute && 85 != second)
        {
          struct tm tm;
          memset(&tm, 0, sizeof(tm));
          
          tm.tm_year = 100+year;
          tm.tm_mon = month;
          tm.tm_mday = day;
          tm.tm_hour = hour;
          tm.tm_min = minute;
          tm.tm_sec = second;

          time_t time = mktime(&tm);
          callback(RAPI_RESPONSE_OK, time);
        } else {
          callback(RAPI_RESPONSE_FEATURE_NOT_SUPPORTED, 0);
        }
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, 0);
      }
    } else {
      callback(ret, 0);
    }
  });
}

void OpenEVSEClass::setTime(time_t time, std::function<void(int ret)> callback)
{
  // S1 yr mo day hr min sec - set clock (RTC) yr=2-digit year

  struct tm tm;
  gmtime_r(&time, &tm);

  setTime(tm, callback);
}

void OpenEVSEClass::setTime(tm &time, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  char command[64];
  snprintf(command, sizeof(command), "$S1 %d %d %d %d %d %d", 
    time.tm_year % 100, 
    time.tm_mon, 
    time.tm_mday, 
    time.tm_hour, 
    time.tm_min, 
    time.tm_sec);

  _sender->sendCmd(command, [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 1)
      {
        callback(RAPI_RESPONSE_OK);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE);
      }
    } else {
      callback(ret);
    }
  });

}

void OpenEVSEClass::getChargeCurrentAndVoltage(std::function<void(int ret, double amps, double volts)> callback)
{
  if (!_sender) {
    return;
  }

  // GG - get charging current and voltage
  //  response: $OK milliamps millivolts
  //  AMMETER must be defined in order to get amps, otherwise returns -1 amps
  //  $GG^24

  _sender->sendCmd("$GG", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 3)
      {
        long milliAmps = strtol(_sender->getToken(1), NULL, 10);
        long milliVolts = strtol(_sender->getToken(2), NULL, 10);

        callback(ret, (double)milliAmps / 1000.0, (double)milliVolts / 1000.0);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, 0, 0);
      }
    } else {
      callback(ret, 0, 0);
    }
  });
}

void OpenEVSEClass::getTemperature(std::function<void(int ret, double temp1, bool temp1_valid, double temp2, bool temp2_valid, double temp3, bool temp3_valid)> callback)
{
  if (!_sender) {
    return;
  }

  // GP - get temPerature (v1.0.3+)
  //  response: $OK ds3231temp mcp9808temp tmp007temp
  //  ds3231temp - temperature from DS3231 RTC
  //  mcp9808temp - temperature from MCP9808
  //  tmp007temp - temperature from TMP007
  //  all temperatures are in 10th's of a degree Celcius
  //  if any temperature sensor is not installed, its return value is -2560
  //  $GP^33

  _sender->sendCmd("$GP", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 4)
      {
        long temp1 = strtol(_sender->getToken(1), NULL, 10);
        long temp2 = strtol(_sender->getToken(2), NULL, 10);
        long temp3 = strtol(_sender->getToken(3), NULL, 10);

        callback(ret, (double)temp1 / 10.0, -2560 != temp1, (double)temp2 / 10.0, -2560 != temp2, (double)temp3 / 10.0, -2560 != temp3);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, 0, false, 0, false, 0, false);
      }
    } else {
      callback(ret, 0, false, 0, false, 0, false);
    }
  });
}

void OpenEVSEClass::getEnergy(std::function<void(int ret, double session_wh, double total_kwh)> callback)
{
  if (!_sender) {
    return;
  }

  // GU - get energy usage (v1.0.3+)
  //  response: $OK Wattseconds Whacc
  //  Wattseconds - Watt-seconds used this charging session, note you'll divide Wattseconds by 3600
  //                to get Wh
  //  Whacc - total Wh accumulated over all charging sessions, note you'll divide Wh by 1000 to get
  //          kWh
  //  $GU^36

  _sender->sendCmd("$GU", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 3)
      {
        long wattseconds = strtol(_sender->getToken(1), NULL, 10);
        long whacc = strtol(_sender->getToken(2), NULL, 10);

        callback(ret, (double)wattseconds / 3600.0, (double)whacc / 1000.0);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, 0, 0);
      }
    } else {
      callback(ret, 0, 0);
    }
  });
}

void OpenEVSEClass::setVoltage(uint32_t milliVolts, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  char command[64];
  snprintf(command, sizeof(command), "$SV %u", milliVolts);

  _sender->sendCmd(command, [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 1)
      {
        callback(RAPI_RESPONSE_OK);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE);
      }
    } else {
      callback(ret);
    }
  });
}

void OpenEVSEClass::setVoltage(double volts, std::function<void(int ret)> callback)
{
  setVoltage((uint32_t)round(volts * 1000), callback);
}

void OpenEVSEClass::getTimer(std::function<void(int ret, int start_hour, int start_minute, int end_hour, int end_minute)> callback)
{
  if (!_sender) {
    return;
  }

  // GD - get Delay timer
  //  response: $OK starthr startmin endhr endmin
  //    all values decimal
  //    if timer disabled, starthr=startmin=endhr=endmin=0

  _sender->sendCmd("$GD", [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 5)
      {
        int starthr = strtol(_sender->getToken(1), NULL, 10);
        int startmin = strtol(_sender->getToken(2), NULL, 10);
        int endhr = strtol(_sender->getToken(3), NULL, 10);
        int endmin = strtol(_sender->getToken(4), NULL, 10);

        callback(ret, starthr, startmin, endhr, endmin);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE, 0, 0, 0, 0);
      }
    } else {
      callback(ret, 0, 0, 0, 0);
    }
  });
}

void OpenEVSEClass::setTimer(int start_hour, int start_minute, int end_hour, int end_minute, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // ST starthr startmin endhr endmin - set timer
  //  $ST 0 0 0 0*0B - cancel timer

  char command[64];
  snprintf(command, sizeof(command), "$ST %d %d %d %d", start_hour, start_minute, end_hour, end_minute);

  _sender->sendCmd(command, [this, callback](int ret)
  {
    if (RAPI_RESPONSE_OK == ret)
    {
      if(_sender->getTokenCnt() >= 1)
      {
        callback(RAPI_RESPONSE_OK);
      } else {
        callback(RAPI_RESPONSE_INVALID_RESPONSE);
      }
    } else {
      callback(ret);
    }
  });
}

void OpenEVSEClass::enable(std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FE - enable EVSE
  //  $FE*AF

  _sender->sendCmd("$FE", [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::sleep(std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FS - sleep EVSE
  //  $FS*BD

  _sender->sendCmd("$FS", [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::disable(std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FR - restart EVSE
  //  $FR*BC

  _sender->sendCmd("$FD", [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::restart(std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FD - disable EVSE
  //  $FD*AE

  _sender->sendCmd("$FR", [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::feature(uint8_t feature, bool enable, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FF - enable/disable feature
  //  $FF feature_id 0|1
  //  0|1 0=disable 1=enable
  //  feature_id:
  //   B = disable/enable front panel button
  //   D = Diode check
  //   E = command Echo
  //    use this for interactive terminal sessions with RAPI.
  //    RAPI will echo back characters as they are typed, and add a <LF> character
  //    after its replies. Valid only over a serial connection, DO NOT USE on I2C
  //   F = GFI self test
  //   G = Ground check
  //   R = stuck Relay check
  //   T = temperature monitoring
  //   V = Vent required check
  //  $FF D 0 - disable diode check
  //  $FF G 1 - enable ground check

  char command[64];
  snprintf(command, sizeof(command), "$FF %c %d", feature, enable ? 1 : 0);

  _sender->sendCmd(command, [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::lcdEnable(bool enable, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // F0 {1|0}- enable/disable display updates
  //      enables/disables g_OBD.Update()
  //  $F0 1^43 - enable display updates and call g_OBD.Update()
  //  $F0 0^42 - disable display updates

  char command[64];
  snprintf(command, sizeof(command), "$F0 %d", enable ? 1 : 0);

  _sender->sendCmd(command, [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::lcdSetColour(int colour, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FB color - set LCD backlight color
  // colors:
  //  OFF 0
  //  RED 1
  //  YELLOW 3
  //  GREEN 2
  //  TEAL 6
  //  BLUE 4
  //  VIOLET 5
  //  WHITE 7 
  //
  //  $FB 7*03 - set backlight to white

  char command[64];
  snprintf(command, sizeof(command), "$FB %d", colour);

  _sender->sendCmd(command, [this, callback](int ret) {
    callback(ret);
  });
}

void OpenEVSEClass::lcdDisplayText(int x, int y, const char *text, std::function<void(int ret)> callback)
{
  if (!_sender) {
    return;
  }

  // FP x y text - print text on lcd display

  char command[64];
  snprintf(command, sizeof(command), "$FP %d %d %s", x, y, text);

  _sender->sendCmd(command, [this, callback](int ret) {
    callback(ret);
  });
}


void OpenEVSEClass::onEvent()
{
  if (!_sender) {
    return;
  }

  DBUGF("Got ASYNC event %s", _sender->getToken(0));

  if(!strcmp(_sender->getToken(0), "$ST"))
  {
    const char *val = _sender->getToken(1);
    DBUGVAR(val);
    uint8_t state = strtol(val, NULL, 16);
    DBUGVAR(state);

    if(_state) {
      _state(state, OPENEVSE_STATE_INVALID, 0, 0);
    }
  }
  else if(!strcmp(_sender->getToken(0), "$WF"))
  {
    const char *val = _sender->getToken(1);
    DBUGVAR(val);

    uint8_t wifiMode = strtol(val, NULL, 10);
    DBUGVAR(wifiMode);

    if(_wifi) {
      _wifi(wifiMode);
    }
  }
  else if(!strcmp(_sender->getToken(0), "$AT"))
  {
    const char *val = _sender->getToken(1);
    uint8_t evse_state = strtol(val, NULL, 16);

    val = _sender->getToken(2);
    uint8_t pilot_state = strtol(val, NULL, 16);

    val = _sender->getToken(3);
    uint32_t current_capacity = strtol(val, NULL, 10);

    val = _sender->getToken(4);
    uint32_t vflags = strtol(val, NULL, 16);

    DBUGF("evse_state = %02x, pilot_state = %02x, current_capacity = %d, vflags = %08x", evse_state, pilot_state, current_capacity, vflags);

    if(_state) {
      _state(evse_state, pilot_state, current_capacity, vflags);
    }
  }
  else if(!strcmp(_sender->getToken(0), "$AB"))
  {
    const char *val = _sender->getToken(1);
    uint8_t post_code = strtol(val, NULL, 16);

    if(_boot) {
      _boot(post_code, _sender->getToken(2));
    }
  }
}

OpenEVSEClass OpenEVSE;

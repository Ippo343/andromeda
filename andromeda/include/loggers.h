#pragma once

#include <ArduinoLog.h>
#include <LittleFS.h>

// Logger extension that logs everything to a file in LittleFS
// with simple log rotation
class SimpleFileLog : public Print
{
private:
  File _logFile;
  size_t _maxSize;

  const char* LOG_FILE_CUR = "/log0.txt";
  const char* LOG_FILE_OLD = "/log1.txt";

  void checkRotation()
  {
    if (_logFile.size() > _maxSize)
    {
      _logFile.close();

      // Simple rotation: delete old, rename current
      if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
      if (LittleFS.exists(LOG_FILE_CUR)) LittleFS.rename(LOG_FILE_CUR, LOG_FILE_OLD);

      _logFile = LittleFS.open(LOG_FILE_CUR, "w");
    }
  }

public:
  SimpleFileLog(size_t maxSize = 32768) : _maxSize(maxSize)
  {
    _logFile = LittleFS.open(LOG_FILE_CUR, "a");
  }

  size_t write(uint8_t c) override
  {
    if (!_logFile)
        return 0;

    size_t result = _logFile.write(c);

    // Flush and check rotation only at end of lines
    if (c == '\n')
    {
      _logFile.flush();
      checkRotation();
    }

    return result;
  }
};

// Tee to both Serial and file
class TeeLog : public Print
{
  Print* _serial;
  Print* _file;
public:
  TeeLog(Print* serial, Print* file) : _serial(serial), _file(file) {}

  size_t write(uint8_t c) override
  {
    _serial->write(c);
    return _file->write(c);
  }
};


// Set up logging to both Serial and File,
// and also configure log format with timestamp and log level
void setupLoggers()
{
  static SimpleFileLog fileLogger(32768);
  static TeeLog teeLogger(&Serial, &fileLogger);

  Log.begin(LOG_LEVEL_VERBOSE, &teeLogger, true);

  Log.setPrefix([](Print* _logOutput, int logLevel) {
    _logOutput->print(millis());
    _logOutput->print(" | ");

    switch (logLevel) {
        case 1: _logOutput->print("FTL"); break;
        case 2: _logOutput->print("ERR"); break;
        case 3: _logOutput->print("WRN"); break;
        case 4: _logOutput->print("INF"); break;
        case 5: _logOutput->print("TRC"); break;
        case 6: _logOutput->print("VRB"); break;
        default: _logOutput->print("UNK"); break;
    }

    _logOutput->print(" | ");
    });
}

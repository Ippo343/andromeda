#pragma once

#include <ArduinoLog.h>
#include <LittleFS.h>

#include "log-ring.h"
#include "utils.h"

// Logger extension that logs everything to a file in LittleFS
// with simple log rotation
class SimpleFileLog : public Print
{
   private:
    File _logFile;
    size_t _maxSize;

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
        if (!_logFile) return 0;

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

// Print sink that funnels formatted log lines into the in-RAM LogRing. Buffers
// bytes and hands the ring one whole line at a time (on '\n' or when the line
// buffer fills), so the ring's lock is taken per line rather than per byte.
class RingLog : public Print
{
    char _line[160];
    size_t _len = 0;

    void flushLine()
    {
        if (_len == 0) return;
        LogRing::Instance().write(reinterpret_cast<const uint8_t*>(_line), _len);
        _len = 0;
    }

   public:
    size_t write(uint8_t c) override
    {
        _line[_len++] = static_cast<char>(c);
        if (c == '\n' || _len == sizeof(_line)) flushLine();
        return 1;
    }
};

// Tee to Serial, file, and (optionally) the in-RAM ring buffer.
class TeeLog : public Print
{
    Print* _serial;
    Print* _file;
    Print* _extra;

   public:
    TeeLog(Print* serial, Print* file, Print* extra = nullptr)
        : _serial(serial), _file(file), _extra(extra)
    {
    }

    size_t write(uint8_t c) override
    {
        _serial->write(c);
        if (_extra) _extra->write(c);
        return _file->write(c);
    }
};

// Default runtime log level: NOTICE for normal builds (errors/warnings/notices
// only), overridable per-env with a build flag (e.g. -DLOG_RUNTIME_LEVEL=LOG_LEVEL_VERBOSE)
// for local debugging. Note this only affects what gets printed at runtime -
// ArduinoLog has no compile-time level gate (only an all-or-nothing
// DISABLE_LOGGING), so every log call's format string is compiled into flash
// regardless of this setting.
#ifndef LOG_RUNTIME_LEVEL
#define LOG_RUNTIME_LEVEL LOG_LEVEL_NOTICE
#endif

// Set up logging to both Serial and File,
// and also configure log format with timestamp and log level
void setupLoggers()
{
    static SimpleFileLog fileLogger(32768);
    static RingLog ringLogger;
    static TeeLog teeLogger(&Serial, &fileLogger, &ringLogger);

    Log.begin(LOG_RUNTIME_LEVEL, &teeLogger, true);

    Log.setPrefix(
        [](Print* _logOutput, int logLevel)
        {
            _logOutput->print(millis());
            _logOutput->print(" | ");

            switch (logLevel)
            {
                case 1:
                    _logOutput->print("FTL");
                    break;
                case 2:
                    _logOutput->print("ERR");
                    break;
                case 3:
                    _logOutput->print("WRN");
                    break;
                case 4:
                    _logOutput->print("INF");
                    break;
                case 5:
                    _logOutput->print("TRC");
                    break;
                case 6:
                    _logOutput->print("VRB");
                    break;
                default:
                    _logOutput->print("UNK");
                    break;
            }

            _logOutput->print(" | ");
        });
}

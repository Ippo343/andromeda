#pragma once

#include <ArduinoLog.h>
#include <LittleFS.h>

#include "log-suspend.h"
#include "log-writer-lock.h"
#include "utils.h"

// Suspends SimpleFileLog before OtaUpdater overwrites the LittleFS partition
// raw (#63's filesystem update) and never resumes it - a reboot always
// follows. Once suspended, SimpleFileLog stops touching LittleFS entirely: a
// log line racing that raw write corrupts the freshly-written image, because
// it goes through the *old* mount's cached filesystem state onto flash the OTA
// write just changed out from under it. Reproduced on hardware as "Corrupted
// dir pair" / a failed mount on the very next boot before this guard existed.
//
// The suspend must also *drain*: setting the flag isn't enough if another task
// is already inside write() - see log-suspend.h. Callers pass a yield (e.g.
// vTaskDelay) so the OTA task can wait for in-flight writes to finish before
// LittleFS.end().
template <typename YieldFn>
inline bool suspendFileLogging(YieldFn yield)
{
    return LogSuspend::suspendAndDrain(yield);
}

// Logger extension that logs everything to a file in LittleFS
// with simple log rotation
class SimpleFileLog : public Print
{
   public:
    // Per-file rotation cap. Halved from the original 32768 (#212/#214): the Advanced/Logs
    // page's browser-side fetch pulls the full retained history of both rotated files on every
    // poll, so this bounds that payload to ~32KB worst case (two files) instead of ~64KB.
    static constexpr size_t DEFAULT_MAX_LOG_BYTES = 16384;

   private:
    File _logFile;
    size_t _maxSize;

    // Serializes every write() body (including checkRotation()) against the other 5+
    // FreeRTOS tasks that also log - see log-writer-lock.h for why. Distinct from
    // LogSuspend above, which only drains writers once for the OTA handoff.
    LogWriterLock _lock;

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
    SimpleFileLog(size_t maxSize = DEFAULT_MAX_LOG_BYTES) : _maxSize(maxSize)
    {
        _logFile = LittleFS.open(LOG_FILE_CUR, "a");
    }

    size_t write(uint8_t c) override
    {
        // Hold a slot for the whole body, not just a flag check: the OTA
        // suspend path waits for this to return before LittleFS.end() (see
        // log-suspend.h). Returns false once a suspend is in progress.
        if (!LogSuspend::beginWrite()) return 0;

        // Held for the rest of this function, including checkRotation() - that's what
        // stops another task's write() from being mid-`_logFile.write(c)` while this one
        // closes/renames/reopens the shared File out from under it.
        LogWriterLockGuard guard(_lock);

        if (!_logFile)
        {
            LogSuspend::endWrite();
            return 0;
        }

        size_t result = _logFile.write(c);

        // Flush and check rotation only at end of lines
        if (c == '\n')
        {
            _logFile.flush();
            checkRotation();
        }

        LogSuspend::endWrite();
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
//
// inline: this header used to be #included from exactly one .cpp (main.cpp),
// but ota-updater.cpp now also includes it for suspendFileLogging() - inline
// keeps that legal instead of a multiple-definition link error.
inline void setupLoggers()
{
    static SimpleFileLog fileLogger(SimpleFileLog::DEFAULT_MAX_LOG_BYTES);
    static TeeLog teeLogger(&Serial, &fileLogger);

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

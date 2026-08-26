#pragma once

// Minimal no-op stand-in for ArduinoLog.h, sufficient to compile project
// code that logs via Log.verboseln/noticeln/errorln/etc. Actual logging
// behavior is irrelevant to native unit tests, so every call is a no-op.

class LoggingStub
{
   public:
    template <typename... Args>
    void verboseln(const char*, Args...)
    {
    }
    template <typename... Args>
    void noticeln(const char*, Args...)
    {
    }
    template <typename... Args>
    void warningln(const char*, Args...)
    {
    }
    template <typename... Args>
    void errorln(const char*, Args...)
    {
    }
    template <typename... Args>
    void fatalln(const char*, Args...)
    {
    }
    template <typename... Args>
    void trace(const char*, Args...)
    {
    }
};

inline LoggingStub Log;

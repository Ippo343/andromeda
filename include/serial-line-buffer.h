#pragma once

#include <cstddef>
#include <cstring>

// Accumulates bytes fed one at a time (from Serial.read(), see main.cpp) into
// NUL-terminated lines, so a real USB-serial connection can feed the same
// WsCommandParser::parse() the WebSocket handler (comms.cpp) and the native
// stdin reader (native-runtime.cpp) already call. Deliberately Arduino-free
// so it's natively unit-testable, mirroring ws-command-parser.h's extraction.
class SerialLineBuffer
{
   public:
    // Feed one byte. Returns true and fills outLine (NUL-terminated) when a
    // '\n' just completed a line; a trailing '\r' is stripped so both LF and
    // CRLF senders work. Returns false otherwise - including for a line
    // that overflowed the internal buffer: it is discarded rather than
    // truncated and handed on, since a truncated JSON command can still
    // parse successfully into a *different*, unintended command (e.g. a
    // long device_name payload truncating into a bare `"type":"model"`
    // fragment). The buffer resyncs cleanly at the next '\n'.
    bool feed(char c, char* outLine, size_t outLineSize)
    {
        if (c == '\n')
        {
            bool completedCleanly = !overflowed_;
            if (completedCleanly)
            {
                size_t len = len_;
                if (len > 0 && buffer_[len - 1] == '\r') len--;  // tolerate CRLF
                size_t copyLen = len < outLineSize - 1 ? len : outLineSize - 1;
                memcpy(outLine, buffer_, copyLen);
                outLine[copyLen] = '\0';
            }
            len_ = 0;
            overflowed_ = false;
            return completedCleanly;
        }

        if (len_ < sizeof(buffer_))
            buffer_[len_++] = c;
        else
            overflowed_ = true;  // discard - see the overflow comment above

        return false;
    }

   private:
    char buffer_[256];
    size_t len_ = 0;
    bool overflowed_ = false;
};

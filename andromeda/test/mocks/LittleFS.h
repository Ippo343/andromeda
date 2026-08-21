#pragma once

// Trivial stand-in for the ESP32 Arduino core's LittleFS.h. Only exists so
// AsyncWebServerRequest::send(LittleFS, path, contentType)'s overload
// resolves natively - no real filesystem behavior needed, native tests
// never assert on served file content.

class LittleFSFS
{
};

inline LittleFSFS LittleFS;

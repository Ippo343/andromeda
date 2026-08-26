#pragma once

#include <string>

// Minimal stand-in for the Arduino core's String (WString.h), covering only
// the surface comms.cpp/wifi-manager.h actually use: construction from a
// C string or an integer, length()/c_str(), +=/+, and (in)equality. Backed
// by std::string rather than reimplementing Arduino's SSO/copy-on-write
// buffer - native tests don't care about that, only about behavior.
class String
{
   public:
    String() = default;
    String(const char* s) : value(s ? s : "") {}
    String(const std::string& s) : value(s) {}
    String(int v) : value(std::to_string(v)) {}
    String(unsigned int v) : value(std::to_string(v)) {}
    String(long v) : value(std::to_string(v)) {}
    String(unsigned long v) : value(std::to_string(v)) {}
    String(float v) : value(std::to_string(v)) {}
    String(double v) : value(std::to_string(v)) {}

    size_t length() const { return value.length(); }
    const char* c_str() const { return value.c_str(); }

    String& operator+=(const String& other)
    {
        value += other.value;
        return *this;
    }
    String operator+(const String& other) const { return String(value + other.value); }

    bool operator==(const String& other) const { return value == other.value; }
    bool operator!=(const String& other) const { return value != other.value; }

   private:
    std::string value;
};

inline String operator+(const char* lhs, const String& rhs) { return String(lhs) + rhs; }

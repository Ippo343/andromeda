#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free UID derivation and name sanitization for
// DeviceIdentity (device-identity.h). Kept Arduino/WiFi-free so it's
// natively unit-testable on its own, mirroring ws-command-parser.h's
// extraction pattern.

namespace DeviceUid
{

constexpr size_t LENGTH = 4;
constexpr size_t MAX_NAME_LENGTH = 32;  // matches the 32-byte WiFi SSID limit

// Derives a LENGTH-character alphanumeric (0-9, A-Z) UID from a 6-byte MAC
// address, used to disambiguate multiple devices on the same network (AP
// SSID "Andromeda-$UID", mDNS hostname "andromeda-$uid.local"). Deterministic:
// the same MAC always yields the same UID. `out` must have room for LENGTH
// characters plus a NUL terminator.
inline void generate(const uint8_t mac[6], char out[LENGTH + 1])
{
    constexpr char ALPHABET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr uint32_t BASE = 36;

    uint32_t hash = 2166136261u;  // FNV-1a offset basis
    for (int i = 0; i < 6; i++)
    {
        hash ^= mac[i];
        hash *= 16777619u;  // FNV-1a prime
    }

    for (size_t i = 0; i < LENGTH; i++)
    {
        out[i] = ALPHABET[hash % BASE];
        hash /= BASE;
    }
    out[LENGTH] = '\0';
}

// Lowercases an ASCII string in place (mDNS hostnames are conventionally
// lowercase, unlike the AP SSID which preserves case for display).
inline void toLowerAscii(char* s)
{
    for (; *s != '\0'; ++s)
    {
        if (*s >= 'A' && *s <= 'Z') *s = static_cast<char>(*s + 32);
    }
}

// Sanitizes a user-supplied device name for safe use as both the AP SSID
// and the mDNS hostname label: keeps alphanumerics, maps whitespace/
// underscore to a hyphen, drops everything else, collapses repeated
// hyphens, and trims leading/trailing hyphens (mDNS hostname labels can't
// start/end with one). Truncates to MAX_NAME_LENGTH. `out` must have room
// for at least outSize bytes; writes a NUL-terminated result and returns its
// length (excluding the NUL).
inline size_t sanitize(const char* input, char* out, size_t outSize)
{
    if (outSize == 0) return 0;

    size_t j = 0;
    bool lastWasHyphen = false;
    size_t maxLen = (outSize - 1 < MAX_NAME_LENGTH) ? outSize - 1 : MAX_NAME_LENGTH;

    for (size_t i = 0; input[i] != '\0' && j < maxLen; i++)
    {
        char c = input[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
            out[j++] = c;
            lastWasHyphen = false;
        }
        else if ((c == '-' || c == ' ' || c == '_') && j > 0 && !lastWasHyphen)
        {
            out[j++] = '-';
            lastWasHyphen = true;
        }
    }

    while (j > 0 && out[j - 1] == '-') j--;  // trim a trailing hyphen left by truncation

    out[j] = '\0';
    return j;
}

}  // namespace DeviceUid

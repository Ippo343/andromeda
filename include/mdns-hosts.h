#pragma once

#include <cstddef>
#include <cstring>

// Pure, dependency-free helpers shared by Comms::startMdns() (registers these as the primary +
// delegated mDNS hostnames) and GET /device-info (reports the same list to the setup UI) - see
// issues #135/#210. Kept Arduino/WiFi-free so it's natively unit-testable, mirroring
// ws-command-parser.h's extraction pattern. The two call sites must never drift, which is
// exactly what a single shared builder guarantees.
namespace MdnsHosts
{

// One winner per name "pair" - device/model/andromeda - each independently resolved to either
// its plain form or its "-<uid>" fallback depending on whether the plain form was found taken
// on the network (see resolveFallback() and Comms::startMdns()). Sized for that fixed set; grow
// it if a new pair is added.
constexpr size_t MAX_HOSTS = 3;

// Picks which half of a name pair to actually use: the plain `primary` if it's free, or the
// "-<uid>" `fallback` if `primary` was found already taken by another device. Availability
// itself can only be determined by an on-network mDNS query (Comms::startMdns(), ESP32-only,
// not natively testable) - this is just the resulting decision, extracted so it's covered here.
inline const char* resolveFallback(bool primaryTaken, const char* primary, const char* fallback)
{
    return primaryTaken ? fallback : primary;
}

// Builds the deduplicated, order-preserving list of mDNS hostname labels (no ".local" suffix)
// this device answers to, from the three already-resolved pair winners (device, model,
// andromeda - in that order). All inputs must already be lowercased (mDNS hostname convention -
// see DeviceUid::toLowerAscii()) and non-empty. Two winners commonly collapse to the same string
// - e.g. an unrenamed device's device-winner and model-winner are both "<model-token>-<uid>", and
// ANDROMEDA_MK0's model-winner and andromeda-winner both derive "andromeda[-<uid>]" - so this
// dedupes rather than assuming 3 distinct entries. `out` entries alias the input strings -
// nothing is copied. Returns the number of entries written (1-3).
inline size_t buildHostList(const char* deviceHost, const char* modelHost,
                            const char* andromedaHost, const char* out[MAX_HOSTS])
{
    const char* candidates[] = {deviceHost, modelHost, andromedaHost};
    size_t n = 0;
    for (const char* candidate : candidates)
    {
        if (!candidate || candidate[0] == '\0') continue;

        bool alreadyListed = false;
        for (size_t i = 0; i < n; i++)
        {
            if (strcmp(out[i], candidate) == 0)
            {
                alreadyListed = true;
                break;
            }
        }
        if (!alreadyListed) out[n++] = candidate;
    }
    return n;
}

}  // namespace MdnsHosts

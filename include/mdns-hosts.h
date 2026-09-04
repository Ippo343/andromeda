#pragma once

#include <cstddef>
#include <cstring>

// Pure, dependency-free helper shared by Comms::startMdns() (registers these
// as the primary + delegated mDNS hostnames) and GET /device-info (reports
// the same list to the setup UI) - see issue #135. Kept Arduino/WiFi-free so
// it's natively unit-testable, mirroring ws-command-parser.h's extraction
// pattern. The two call sites must never drift, which is exactly what a
// single shared builder guarantees.
namespace MdnsHosts
{

// primary (custom-or-default name), <model>-<uid>, andromeda-<uid>, and the
// bare "andromeda" catch-all - see include/device-identity.h for how each is
// derived. Sized for that fixed set; grow it if a new host source is added.
constexpr size_t MAX_HOSTS = 4;

// Builds the deduplicated, order-preserving list of mDNS hostname labels
// (no ".local" suffix) this device answers to. `primary` and `andromedaUid`
// are always non-empty; `modelUid` is empty only if a caller has nothing to
// offer there (there is currently no such caller - GEOMETRY always resolves
// to at least the "Andromeda" fallback, see DeviceIdentity::getDefaultName()).
// All inputs must already be lowercased (mDNS hostname convention - see
// DeviceUid::toLowerAscii()). Order matters to callers: the primary name is
// always first, so a display list naturally leads with "this device's own
// name". `out` entries alias the input strings - nothing is copied. Returns
// the number of entries written (1-4).
inline size_t buildHostList(const char* primary, const char* modelUid, const char* andromedaUid,
                            const char* out[MAX_HOSTS])
{
    const char* candidates[] = {primary, modelUid, andromedaUid, "andromeda"};
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

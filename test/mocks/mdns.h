#pragma once

// Trivial no-op stand-in for the ESP-IDF mdns component's mdns.h (used directly by
// comms.cpp for delegated-hostname registration - see include/mdns-hosts.h, issue #135).
// ESPmDNS.h wraps the same component but doesn't expose the delegate API, hence the direct
// include. Only test_comms_integration actually compiles comms.cpp natively (see
// test/mocks/ESPmDNS.h's own header comment for why); this just has to satisfy the
// compiler, not behave like the real responder.

#include <cstdint>

#define ESP_IPADDR_TYPE_V4 0U

typedef enum
{
    ESP_OK = 0,
    ESP_FAIL = -1,
    ESP_ERR_INVALID_ARG = 0x102,
    ESP_ERR_NO_MEM = 0x101,
    ESP_ERR_INVALID_STATE = 0x103,
} esp_err_t;

struct esp_ip4_addr_t
{
    uint32_t addr;
};

struct esp_ip_addr_t
{
    uint32_t type;
    union
    {
        esp_ip4_addr_t ip4;
    } u_addr;
};

struct mdns_ip_addr_t
{
    esp_ip_addr_t addr;
    mdns_ip_addr_t* next;
};

inline esp_err_t mdns_delegate_hostname_add(const char*, const mdns_ip_addr_t*) { return ESP_OK; }
inline esp_err_t mdns_delegate_hostname_remove(const char*) { return ESP_OK; }
inline bool mdns_hostname_exists(const char*) { return false; }

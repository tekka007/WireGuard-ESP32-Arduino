/*
 * WireGuard ESP32 - Modernized C API implementation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WireGuardModern.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <algorithm>
#include <cstring>

extern "C" {
#include "wireguardif.h"
#include "lwip/netif.h"
}

//=============================================================================
// Static globals
//=============================================================================

static WireGuardError_t g_last_error = WG_ERR_OK;
static WireGuardErrorCallback_t g_error_callback = nullptr;
static void* g_error_user_data = nullptr;
static WireGuardStats_t g_stats = {0};
static uint32_t g_session_start_ms = 0;
static uint32_t g_handshake_count = 0;

// Access to the global netif - we need to expose this from WireGuard.cpp
extern struct netif* wg_netif;
extern uint8_t wireguard_peer_index;

//=============================================================================
// Internal helpers
//=============================================================================

static void setError(WireGuardError_t err, const char* msg = nullptr) {
    g_last_error = err;
    if(g_error_callback && (msg || err != WG_ERR_OK)) {
        char buf[128];
        if(msg) {
            strncpy(buf, msg, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        } else {
            snprintf(buf, sizeof(buf), "WireGuard error %d", (int)err);
        }
        g_error_callback(err, buf, g_error_user_data);
    }
}

static bool validateBase64Key(const char* key) {
    if(!key) return false;
    size_t len = strlen(key);
    // WireGuard keys: base64, should be 44 chars for 32-byte keys
    if(len < 40 || len > 100) return false;
    static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    for(size_t i = 0; i < len; i++) {
        if(strchr(b64_chars, key[i]) == nullptr) {
            return false;
        }
    }
    return true;
}

//=============================================================================
// Public API Implementation
//=============================================================================

WireGuardError_t WireGuard_getLastError(void) {
    return g_last_error;
}

const char* WireGuard_strerror(WireGuardError_t err) {
    switch(err) {
        case WG_ERR_OK: return "No error";
        case WG_ERR_NOMEM: return "Out of memory";
        case WG_ERR_INVALID_KEY: return "Invalid key format or length (expected base64, 44 chars)";
        case WG_ERR_INVALID_IP: return "Invalid IP address configuration";
        case WG_ERR_NTP_NOT_SYNCED: return "System time not synchronized - NTP required";
        case WG_ERR_NETWORK: return "Network error (bind, send, or receive failed)";
        case WG_ERR_NO_PEERS: return "No peer slot available (max peers reached)";
        case WG_ERR_PEER_NOT_FOUND: return "Peer not found (invalid index)";
        case WG_ERR_ALREADY_INIT: return "WireGuard interface already initialized";
        case WG_ERR_NOT_INIT: return "WireGuard interface not initialized";
        case WG_ERR_DNS_RESOLUTION: return "DNS resolution failed";
        case WG_ERR_TIMEOUT: return "Operation timeout";
        case WG_ERR_CRYPTO: return "Cryptographic operation failed";
        case WG_ERR_UNKNOWN: return "Unknown error";
        default: return "Unknown error code";
    }
}

void WireGuard_setErrorCallback(WireGuardErrorCallback_t cb, void* user_data) {
    g_error_callback = cb;
    g_error_user_data = user_data;
}

void WireGuard_setLogLevel(uint8_t level) {
    esp_log_level_t esp_level;
    switch(level) {
        case WG_LOG_NONE: esp_level = ESP_LOG_NONE; break;
        case WG_LOG_ERROR: esp_level = ESP_LOG_ERROR; break;
        case WG_LOG_WARN: esp_level = ESP_LOG_WARN; break;
        case WG_LOG_INFO: esp_level = ESP_LOG_INFO; break;
        case WG_LOG_DEBUG: esp_level = ESP_LOG_DEBUG; break;
        case WG_LOG_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
        default: esp_level = ESP_LOG_INFO;
    }
    esp_log_level_set("*", esp_level);
    esp_log_level_set("WireGuard", esp_level);
    esp_log_level_set("WG", esp_level);
}

bool WireGuard_getStats(WireGuardStats_t* stats) {
    if(!stats) {
        setError(WG_ERR_INVALID_IP);  // Reuse for invalid parameter
        return false;
    }

    if(!wg_netif) {
        memset(stats, 0, sizeof(WireGuardStats_t));
        stats->is_connected = false;
        return true;  // Not an error, just zero stats
    }

    // Update stats from multiple sources
    memset(stats, 0, sizeof(WireGuardStats_t));

    // Basic stats
    stats->peer_count = 1;  // Currently single peer
    stats->is_connected = netif_is_link_up(wg_netif);
    stats->uptime_ms = g_session_start_ms;

    if(stats->is_connected) {
        stats->last_handshake_ms = g_stats.last_handshake_ms;
        stats->handshake_count = g_handshake_count;
    }

    // TODO: Get actual byte counts from lwIP netif statistics
    // For now, these remain zero. Could be implemented by:
    // - Accessing netif->ip_addr (not available)
    // - Hooking into wireguardif_output/wireguardif_input
    // - Reading from /proc/net/dev equivalent (not in Arduino)

    return true;
}

bool WireGuard_isNtpSynced(void) {
    time_t now = time(nullptr);
    // Check if time is reasonable (after Nov 2023)
    return now > 1700000000;
}

bool WireGuard_validatePrivateKey(const char* key) {
    return validateBase64Key(key);
}

bool WireGuard_validatePublicKey(const char* key) {
    return validateBase64Key(key);
}

bool WireGuard_generateKeyPair(char* out_private, size_t priv_buf_len,
                              char* out_public, size_t pub_buf_len) {
    setError(WG_ERR_UNKNOWN, "Key generation not yet implemented");
    return false;
}

//=============================================================================
// NVS Persistence
//=============================================================================

bool WireGuard_saveConfig(const char* privateKey, const char* publicKey,
                         const char* endpoint, uint16_t port,
                         const IPAddress* resolvedEndpointIP) {
    if(!privateKey || !publicKey || !endpoint) {
        setError(WG_ERR_INVALID_IP);  // Reuse for invalid params
        return false;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wgcfg", NVS_READWRITE, &nvs);
    if(err != ESP_OK) {
        setError(WG_ERR_NETWORK, "NVS open failed");
        return false;
    }

    // Store config version
    const uint32_t cfg_ver = 1;
    nvs_set_u32(nvs, "cfg_ver", cfg_ver);

    // Store keys and endpoint
    nvs_set_str(nvs, "priv_key", privateKey);
    nvs_set_str(nvs, "pub_key", publicKey);
    nvs_set_str(nvs, "endpoint", endpoint);
    nvs_set_u16(nvs, "port", port);

    // Store resolved IP for faster reconnect
    if(resolvedEndpointIP && *resolvedEndpointIP != IPAddress(0, 0, 0, 0)) {
        uint32_t ip = resolvedEndpointIP->v4();
        nvs_set_u32(nvs, "endpoint_ip", ip);
    } else {
        nvs_set_u32(nvs, "endpoint_ip", 0);
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);

    if(err != ESP_OK) {
        setError(WG_ERR_NETWORK, "NVS commit failed");
        return false;
    }

    return true;
}

bool WireGuard_loadConfig(String& out_privateKey, String& out_publicKey,
                         String& out_endpoint, uint16_t& out_port,
                         IPAddress& out_endpointIP) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wgcfg", NVS_READONLY, &nvs);
    if(err != ESP_OK) {
        // No saved config - not an error
        return false;
    }

    uint32_t cfg_ver = 0;
    err = nvs_get_u32(nvs, "cfg_ver", &cfg_ver);
    if(err != ESP_OK || cfg_ver != 1) {
        nvs_close(nvs);
        return false;
    }

    size_t str_len;
    char buffer[256];

    // Load private key
    err = nvs_get_str(nvs, "priv_key", nullptr, &str_len);
    if(err == ESP_OK && str_len < sizeof(buffer)) {
        nvs_get_str(nvs, "priv_key", buffer, &str_len);
        out_privateKey = String(buffer);
    } else {
        nvs_close(nvs);
        return false;
    }

    // Load public key
    err = nvs_get_str(nvs, "pub_key", nullptr, &str_len);
    if(err == ESP_OK && str_len < sizeof(buffer)) {
        nvs_get_str(nvs, "pub_key", buffer, &str_len);
        out_publicKey = String(buffer);
    } else {
        nvs_close(nvs);
        return false;
    }

    // Load endpoint
    err = nvs_get_str(nvs, "endpoint", nullptr, &str_len);
    if(err == ESP_OK && str_len < sizeof(buffer)) {
        nvs_get_str(nvs, "endpoint", buffer, &str_len);
        out_endpoint = String(buffer);
    } else {
        nvs_close(nvs);
        return false;
    }

    // Load port
    err = nvs_get_u16(nvs, "port", &out_port);
    if(err != ESP_OK) {
        nvs_close(nvs);
        return false;
    }

    // Load resolved IP (optional)
    uint32_t ip_val = 0;
    nvs_get_u32(nvs, "endpoint_ip", &ip_val);
    out_endpointIP = IPAddress(ip_val);

    nvs_close(nvs);
    return true;
}

bool WireGuard_clearConfig(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wgcfg", NVS_READWRITE, &nvs);
    if(err != ESP_OK) {
        return false;
    }

    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);

    g_last_error = WG_ERR_OK;
    return true;
}

//=============================================================================
// Additional utilities
//=============================================================================

const char* WireGuard_getInterfaceName(void) {
    if(!wg_netif) return nullptr;
    static char name[8];
    snprintf(name, sizeof(name), "%c%c%d", wg_netif->name[0], wg_netif->name[1], wg_netif->num);
    return name;
}

bool WireGuard_isInterfaceUp(void) {
    if(!wg_netif) return false;
    return netif_is_link_up(wg_netif);
}

//=============================================================================
// Hook into wireguardif to update statistics
// This would be called from wireguardif.c at key points
//=============================================================================

void WireGuard_onHandshakeSuccess(void) {
    g_handshake_count++;
    g_stats.last_handshake_ms = millis();
    if(g_session_start_ms == 0) {
        g_session_start_ms = millis();
    }
}

void WireGuard_updateRxTxCounters(uint32_t rx_bytes, uint32_t tx_bytes) {
    g_stats.rx_bytes += rx_bytes;
    g_stats.tx_bytes += tx_bytes;
}

// These would need to be called from wireguardif.c
// For now, they're declared but not used

#ifdef __cplusplus
}
#endif

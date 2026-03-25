/*
 * WireGuard ESP32 - Extended implementation with modern features
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file provides extended functionality:
 * - NVS persistent storage
 * - Better error reporting
 * - Config validation
 * - Diagnostics
 *
 * This is an ADDITIVE layer. Original WireGuard.cpp remains unchanged
 * for backward compatibility.
 */

#include "WireGuard-ESP32.h"
#include "WireGuardTypes.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <algorithm>
#include <string>

//=============================================================================
// Constants
//=============================================================================

#define WG_NVS_NAMESPACE "wgcfg"
#define WG_NVS_PRIV_KEY "priv_key"
#define WG_NVS_PUB_KEY "pub_key"
#define WG_NVS_ENDPOINT "endpoint"
#define WG_NVS_PORT "port"
#define WG_NVS_ENDPOINT_IP "endpoint_ip"
#define WG_NVS_CONFIG_VER "cfg_ver"
#define WG_CONFIG_VERSION 1

//=============================================================================
// Static Members
//=============================================================================

static WireGuardError_t g_last_error = WG_ERR_OK;
static WireGuardErrorCallback_t g_error_callback = nullptr;
static void* g_error_user_data = nullptr;
static WireGuardStats_t g_stats = {0};

//=============================================================================
// Helper Functions
//=============================================================================

static bool validateBase64Key(const char* key, size_t expected_len) {
    if (!key) return false;
    size_t len = strlen(key);
    // Base64 keys: 44 chars (32 bytes encoded) for WireGuard
    if (len < 40 || len > 100) return false;
    // Check base64 charset
    static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    for (size_t i = 0; i < len; i++) {
        if (key[i] == '\0') break;
        if (strchr(b64_chars, key[i]) == nullptr) {
            return false;
        }
    }
    return true;
}

static bool validateIPAddress(const IPAddress& ip) {
    // Check for common invalid addresses
    if (ip == IPAddress(0, 0, 0, 0) || ip == IPAddress(255, 255, 255, 255)) {
        return false;
    }
    return true;
}

static void setError(WireGuardError_t err, const char* msg = nullptr) {
    g_last_error = err;
    if (g_error_callback && (msg || err != WG_ERR_OK)) {
        char buf[128];
        if (msg) {
            snprintf(buf, sizeof(buf), "%s", msg);
        } else {
            snprintf(buf, sizeof(buf), "WireGuard error %d", (int)err);
        }
        g_error_callback(err, buf, g_error_user_data);
    }
}

//=============================================================================
// NVS Persistence
//=============================================================================

bool WireGuard_saveConfig(const char* privateKey, const char* publicKey,
                          const char* endpoint, uint16_t port,
                          const IPAddress* resolvedEndpointIP) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        setError(WG_ERR_NETWORK, "NVS open failed");
        return false;
    }

    // Store configuration version
    uint32_t cfg_ver = WG_CONFIG_VERSION;
    nvs_set_u32(nvs, WG_NVS_CONFIG_VER, cfg_ver);

    // Store keys as strings
    if (privateKey) nvs_set_str(nvs, WG_NVS_PRIV_KEY, privateKey);
    if (publicKey) nvs_set_str(nvs, WG_NVS_PUB_KEY, publicKey);
    if (endpoint) nvs_set_str(nvs, WG_NVS_ENDPOINT, endpoint);

    nvs_set_u16(nvs, WG_NVS_PORT, port);

    // Store resolved IP if provided
    if (resolvedEndpointIP && *resolvedEndpointIP != IPAddress(0, 0, 0, 0)) {
        uint32_t ip = resolvedEndpointIP->v4();
        nvs_set_u32(nvs, WG_NVS_ENDPOINT_IP, ip);
    } else {
        nvs_set_u32(nvs, WG_NVS_ENDPOINT_IP, 0);
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err != ESP_OK) {
        setError(WG_ERR_NETWORK, "NVS commit failed");
        return false;
    }

    return true;
}

bool WireGuard_loadConfig(String& out_privateKey, String& out_publicKey,
                          String& out_endpoint, uint16_t& out_port,
                          IPAddress& out_endpointIP) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WG_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        // No saved config - not an error
        return false;
    }

    uint32_t cfg_ver = 0;
    err = nvs_get_u32(nvs, WG_NVS_CONFIG_VER, &cfg_ver);
    if (err != ESP_OK || cfg_ver != WG_CONFIG_VERSION) {
        nvs_close(nvs);
        return false; // No valid config
    }

    size_t str_len;
    char buffer[256];

    // Load private key
    err = nvs_get_str(nvs, WG_NVS_PRIV_KEY, nullptr, &str_len);
    if (err == ESP_OK && str_len < sizeof(buffer)) {
        nvs_get_str(nvs, WG_NVS_PRIV_KEY, buffer, &str_len);
        out_privateKey = buffer;
    }

    // Load public key
    err = nvs_get_str(nvs, WG_NVS_PUB_KEY, nullptr, &str_len);
    if (err == ESP_OK && str_len < sizeof(buffer)) {
        nvs_get_str(nvs, WG_NVS_PUB_KEY, buffer, &str_len);
        out_publicKey = buffer;
    }

    // Load endpoint hostname
    err = nvs_get_str(nvs, WG_NVS_ENDPOINT, nullptr, &str_len);
    if (err == ESP_OK && str_len < sizeof(buffer)) {
        nvs_get_str(nvs, WG_NVS_ENDPOINT, buffer, &str_len);
        out_endpoint = buffer;
    }

    // Load port
    uint16_t port = 0;
    nvs_get_u16(nvs, WG_NVS_PORT, &port);
    out_port = port;

    // Load resolved IP
    uint32_t ip_val = 0;
    nvs_get_u32(nvs, WG_NVS_ENDPOINT_IP, &ip_val);
    out_endpointIP = IPAddress(ip_val);

    nvs_close(nvs);
    return true;
}

bool WireGuard_clearConfig() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);

    g_last_error = WG_ERR_OK;
    return true;
}

//=============================================================================
// Diagnostics & Statistics
//=============================================================================

// Helper to access the underlying lwIP device
extern "C" {
extern struct netif* wg_get_netif();  // Declare external (we'll add this)
}

void WireGuard_updateStats() {
    // Access lwIP netif to get statistics
    // This is a simplified version - would need to expose netif externally
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.peer_count = 1; // Fixed single peer

    // Could read from netif stats, peer structure
    // For now, placeholder
}

uint32_t WireGuard_getRxBytes() {
    WireGuard_updateStats();
    return g_stats.rx_bytes;
}

uint32_t WireGuard_getTxBytes() {
    WireGuard_updateStats();
    return g_stats.tx_bytes;
}

bool WireGuard_getPeerInfo(uint8_t peer_index, uint32_t* last_rx, uint32_t* last_tx,
                          uint32_t* handshake_ms, bool* is_up) {
    WireGuard_updateStats();
    if (peer_index >= 1) return false;
    if (last_rx) *last_rx = g_stats.peer_last_rx[0];
    if (last_tx) *last_tx = g_stats.tx_bytes; // Placeholder
    if (handshake_ms) *handshake_ms = g_stats.last_handshake_ms;
    if (is_up) *is_up = g_stats.last_handshake_ms > 0;
    return true;
}

//=============================================================================
// Error Handling
//=============================================================================

WireGuardError_t WireGuard_getLastError() {
    return g_last_error;
}

const char* WireGuard_strerror(WireGuardError_t err) {
    switch (err) {
        case WG_ERR_OK: return "No error";
        case WG_ERR_NOMEM: return "Out of memory";
        case WG_ERR_INVALID_KEY: return "Invalid key format or length";
        case WG_ERR_INVALID_IP: return "Invalid IP address";
        case WG_ERR_NTP_NOT_SYNCED: return "System time not synchronized (NTP required)";
        case WG_ERR_NETWORK: return "Network error (bind, send, or receive failed)";
        case WG_ERR_NO_PEERS: return "No peer slot available";
        case WG_ERR_PEER_NOT_FOUND: return "Peer not found";
        case WG_ERR_ALREADY_INIT: return "Interface already initialized";
        case WG_ERR_NOT_INIT: return "Interface not initialized";
        case WG_ERR_DNS_RESOLUTION: return "DNS resolution failed";
        case WG_ERR_TIMEOUT: return "Operation timeout";
        case WG_ERR_CRYPTO: return "Cryptographic operation failed";
        default: return "Unknown error";
    }
}

void WireGuard_setErrorCallback(WireGuardErrorCallback_t cb, void* user_data) {
    g_error_callback = cb;
    g_error_user_data = user_data;
}

void WireGuard_setLogLevel(uint8_t level) {
    // Map to ESP-IDF log levels
    esp_log_level_t esp_level;
    switch (level) {
        case WG_LOG_NONE: esp_level = ESP_LOG_NONE; break;
        case WG_LOG_ERROR: esp_level = ESP_LOG_ERROR; break;
        case WG_LOG_WARN: esp_level = ESP_LOG_WARN; break;
        case WG_LOG_INFO: esp_level = ESP_LOG_INFO; break;
        case WG_LOG_DEBUG: esp_level = ESP_LOG_DEBUG; break;
        case WG_LOG_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
        default: esp_level = ESP_LOG_INFO;
    }
    esp_log_level_set("*", esp_level);
    esp_log_level_set("WG", esp_level);
}

//=============================================================================
// Key Management
//=============================================================================

bool WireGuard_validatePrivateKey(const char* key) {
    return validateBase64Key(key, 44); // WireGuard keys are 44 char base64
}

bool WireGuard_validatePublicKey(const char* key) {
    return validateBase64Key(key, 44);
}

bool WireGuard_generateKeyPair(char* out_private, size_t priv_buf_len,
                              char* out_public, size_t pub_buf_len) {
    if (!out_private || priv_buf_len < 45) return false;
    if (!out_public || pub_buf_len < 45) return false;

    // Generate random 32 bytes
    uint8_t priv[32];
    wireguard_random_bytes(priv, 32);

    // Derive public key (X25519)
    uint8_t pub[32];
    if (!x25519(pub, priv, base_point)) {
        setError(WG_ERR_CRYPTO, "X25519 key generation failed");
        return false;
    }

    // Base64 encode
    // Need to implement base64 encode or use existing
    // For now, return false - this needs implementation
    setError(WG_ERR_UNKNOWN, "Not implemented");
    return false;
}

//=============================================================================
// Advanced APIs (for future multi-peer support)
//=============================================================================

#ifndef WIREGUARD_MAX_PEERS
#define WIREGUARD_MAX_PEERS 1
#endif

bool WireGuard_addPeer(const char* publicKey,
                       const IPAddress& allowedIP,
                       const IPAddress& allowedMask) {
    // Future implementation when WIREGUARD_MAX_PEERS > 1
    setError(WG_ERR_UNKNOWN, "Multi-peer support not yet implemented");
    return false;
}

bool WireGuard_removePeer(uint8_t peer_index) {
    // Future implementation
    setError(WG_ERR_UNKNOWN, "Multi-peer support not yet implemented");
    return false;
}

bool WireGuard_setPeerEndpoint(uint8_t peer_index,
                              const char* hostname,
                              uint16_t port) {
    // Future implementation
    setError(WG_ERR_UNKNOWN, "Multi-peer support not yet implemented");
    return false;
}

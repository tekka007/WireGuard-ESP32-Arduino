/*
 * WireGuard ESP32 - Extended types and error reporting
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Additional types and error handling for modernized library
 */

#ifndef WIREGUARD_TYPES_H
#define WIREGUARD_TYPES_H

#include <Arduino.h>
#include "WireGuard-ESP32.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Forward declarations
struct wireguard_device;
struct wireguard_peer;

/**
 * WireGuard error codes
 * All API functions return these to indicate success/failure
 */
typedef enum {
    WG_ERR_OK = 0,
    WG_ERR_NOMEM,              // Out of memory
    WG_ERR_INVALID_KEY,        // Invalid key format/长度
    WG_ERR_INVALID_IP,         // Invalid IP address
    WG_ERR_NTP_NOT_SYNCED,     // System time not synchronized
    WG_ERR_NETWORK,            // Network error (bind, send, recv failed)
    WG_ERR_NO_PEERS,           // No peer slot available
    WG_ERR_PEER_NOT_FOUND,     // Specified peer index invalid
    WG_ERR_ALREADY_INIT,       // Interface already initialized
    WG_ERR_NOT_INIT,           // Interface not initialized
    WG_ERR_DNS_RESOLUTION,     // DNS lookup failed
    WG_ERR_TIMEOUT,            // Operation timeout
    WG_ERR_CRYPTO,             // Cryptographic operation failed
    WG_ERR_UNKNOWN             // Unknown error
} WireGuardError_t;

/**
 * Statistics structure for diagnostics
 */
typedef struct {
    uint32_t rx_bytes;           // Total bytes received through tunnel
    uint32_t tx_bytes;           // Total bytes transmitted through tunnel
    uint32_t handshake_count;    // Number of successful handshakes
    uint32_t last_handshake_ms;  // Millis of last successful handshake
    uint32_t session_start_ms;   // Millis when current session started
    uint8_t  peer_count;         // Number of active peers
    uint32_t peer_last_rx[1];    // Per-peer last packet receive time (flexible array - actual size = max_peers)
} WireGuardStats_t;

/**
 * Peer configuration structure
 * Used for adding/updating peers dynamically
 */
typedef struct {
    uint8_t  public_key[32];     // Peer public key (raw binary)
    IPAddress allowed_ip;        // Allowed source IP
    IPAddress allowed_mask;      // Allowed netmask
    char     endpoint_host[256]; // Endpoint hostname (NULL for dynamic)
    char     endpoint_ip[46];    // Endpoint IP string (for display)
    uint16_t endpoint_port;     // Endpoint UDP port
    uint16_t keepalive_interval; // Keepalive in seconds (0 = disabled)
    bool     persistent_keepalive; // Enable automatic keepalive
} WireGuardPeerConfig_t;

/**
 * Get human-readable error string
 */
const char* WireGuard_strerror(WireGuardError_t err);

/**
 * Set error callback for async operations
 */
typedef void (*WireGuardErrorCallback_t)(WireGuardError_t err, const char* msg, void* user_data);
void WireGuard_setErrorCallback(WireGuardErrorCallback_t cb, void* user_data);

/**
 * Enable/disable verbose logging at runtime
 */
void WireGuard_setLogLevel(uint8_t level);

// Log levels (matching ESP-IDF)
#define WG_LOG_NONE   0
#define WG_LOG_ERROR  1
#define WG_LOG_WARN   2
#define WG_LOG_INFO   3
#define WG_LOG_DEBUG  4
#define WG_LOG_VERBOSE 5

#ifdef __cplusplus
}
#endif

#endif // WIREGUARD_TYPES_H

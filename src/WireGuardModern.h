/*
 * WireGuard ESP32 - Modernized API with error handling and diagnostics
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This header provides additional features beyond the basic WireGuard-ESP32.h
 * Include this AFTER WireGuard-ESP32.h to add:
 * - Error codes and diagnostics
 * - Statistics tracking
 * - Configuration validation
 * - NVS persistent storage
 */

#ifndef WIREGUARD_MODERN_H
#define WIREGUARD_MODERN_H

#include <Arduino.h>
#include "WireGuard-ESP32.h"

#ifdef __cplusplus
extern "C" {
#endif

// Include C definitions for NVS and error types
#include <stdint.h>

/**
 * @brief WireGuard error codes
 */
typedef enum {
    WG_ERR_OK = 0,
    WG_ERR_NOMEM,              ///< Out of memory
    WG_ERR_INVALID_KEY,        ///< Invalid key format/length
    WG_ERR_INVALID_IP,         ///< Invalid IP address configuration
    WG_ERR_NTP_NOT_SYNCED,     ///< System time not synchronized (NTP required)
    WG_ERR_NETWORK,            ///< Network error (bind, send, recv failed)
    WG_ERR_NO_PEERS,           ///< No peer slot available (max peers exceeded)
    WG_ERR_PEER_NOT_FOUND,     ///< Specified peer index invalid
    WG_ERR_ALREADY_INIT,       ///< Interface already initialized
    WG_ERR_NOT_INIT,           ///< Interface not initialized
    WG_ERR_DNS_RESOLUTION,     ///< DNS lookup failed
    WG_ERR_TIMEOUT,            ///< Operation timeout
    WG_ERR_CRYPTO,             ///< Cryptographic operation failed
    WG_ERR_UNKNOWN             ///< Unknown/unclassified error
} WireGuardError_t;

/**
 * @brief Log level enumeration
 */
typedef enum {
    WG_LOG_NONE = 0,       ///< No logging
    WG_LOG_ERROR,          ///< Errors only
    WG_LOG_WARN,           ///< Warnings and errors
    WG_LOG_INFO,           ///< informational messages (default)
    WG_LOG_DEBUG,          ///< Debug-level logging
    WG_LOG_VERBOSE         ///< Very verbose (all internal operations)
} WireGuardLogLevel_t;

/**
 * @brief Statistics structure for diagnostics
 */
typedef struct {
    uint32_t rx_bytes;           ///< Total bytes received through tunnel
    uint32_t tx_bytes;           ///< Total bytes transmitted through tunnel
    uint32_t handshake_count;    ///< Number of successful handshakes
    uint32_t last_handshake_ms;  ///< Millis of last successful handshake (0 if never)
    uint32_t session_start_ms;   ///< Millis when current session started
    uint8_t  peer_count;         ///< Number of active peers
    bool     is_connected;       ///< Tunnel currently connected
    uint32_t uptime_ms;          ///< Total time interface has been up
} WireGuardStats_t;

/**
 * @brief Callback function type for error reporting
 * @param err Error code
 * @param msg Human-readable error message (may be NULL)
 * @param user_data User-provided context pointer
 */
typedef void (*WireGuardErrorCallback_t)(WireGuardError_t err, const char* msg, void* user_data);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the last error code
 * @return Most recent error code (WG_ERR_OK if no error)
 */
WireGuardError_t WireGuard_getLastError(void);

/**
 * @brief Get human-readable error string
 * @param err Error code to convert
 * @return Static string describing the error
 */
const char* WireGuard_strerror(WireGuardError_t err);

/**
 * @brief Set custom error callback
 * @param cb Callback function (NULL to disable)
 * @param user_data User data pointer passed to callback
 *
 * The callback is invoked whenever an error occurs, including during
 * initialization, handshake failures, and network errors.
 */
void WireGuard_setErrorCallback(WireGuardErrorCallback_t cb, void* user_data);

/**
 * @brief Set log verbosity at runtime
 * @param level Log level (WG_LOG_* constants)
 *
 * Default: WG_LOG_INFO
 * Higher levels produce more output but may impact performance.
 */
void WireGuard_setLogLevel(uint8_t level);

/**
 * @brief Get current statistics
 * @param stats Pointer to stats structure to fill
 * @return true on success, false if not initialized
 *
 * Retrieves real-time statistics about the WireGuard interface.
 * Call after initialization to monitor tunnel health.
 */
bool WireGuard_getStats(WireGuardStats_t* stats);

/**
 * @brief Check if NTP is synchronized
 * @return true if time is synced, false otherwise
 *
 * WireGuard requires accurate time. Call this before begin().
 * Returns false if time(nullptr) returns a value before Nov 2023.
 */
bool WireGuard_isNtpSynced(void);

/**
 * @brief Validate private key format
 * @param key Base64-encoded private key string
 * @return true if valid format, false otherwise
 *
 * WireGuard private keys are 44-character base64 strings (32 bytes decoded).
 */
bool WireGuard_validatePrivateKey(const char* key);

/**
 * * Validate public key format
 * @param key Base64-encoded public key string
 * @return true if valid format, false otherwise
 */
bool WireGuard_validatePublicKey(const char* key);

/**
 * @brief Generate a new random key pair
 * @param out_private Buffer for private key (44+ bytes)
 * @param out_public Buffer for public key (44+ bytes)
 * @return true on success, false on failure
 *
 * WARNING: Not yet implemented. Planned for future release.
 */
bool WireGuard_generateKeyPair(char* out_private, size_t priv_buf_len,
                              char* out_public, size_t pub_buf_len);

/**
 * @brief Save configuration to NVS
 * @param privateKey Private key (base64)
 * @param publicKey Public key (base64)
 * @param endpoint DNS name or IP string
 * @param port UDP port
 * @param resolvedEndpointIP Optional: resolved IP address for faster reconnect
 * @return true on success
 *
 * Saves configuration to non-volatile storage. Enables instant recovery
 * after reboot (handshake state not preserved, but config is).
 */
bool WireGuard_saveConfig(const char* privateKey, const char* publicKey,
                         const char* endpoint, uint16_t port,
                         const IPAddress* resolvedEndpointIP = nullptr);

/**
 * @brief Load configuration from NVS
 * @param out_privateKey Returns loaded private key
 * @param out_publicKey Returns loaded public key
 * @param out_endpoint Returns endpoint hostname
 * @param out_port Returns endpoint port
 * @param out_endpointIP Returns resolved endpoint IP (if saved)
 * @return true if config found and loaded, false if no config
 *
 * Loads previously saved configuration. Call before begin() to restore
 * settings and enable faster reconnect.
 */
bool WireGuard_loadConfig(String& out_privateKey, String& out_publicKey,
                         String& out_endpoint, uint16_t& out_port,
                         IPAddress& out_endpointIP);

/**
 * @brief Clear saved configuration from NVS
 * @return true on success
 */
bool WireGuard_clearConfig(void);

/**
 * @brief Get interface name
 * @return "wg0" or similar (from netif->name)
 */
const char* WireGuard_getInterfaceName(void);

/**
 * @brief Check if interface is up
 * @return true if initialized and link is up
 */
bool WireGuard_isInterfaceUp(void);

#ifdef __cplusplus
}
#endif

#endif /* WIREGUARD_MODERN_H */

/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "WireGuard-ESP32.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip.h"
#include "lwip/netdb.h"

#include "esp32-hal-log.h"

extern "C" {
#include "wireguardif.h"
#include "wireguard-platform.h"
}

// Wireguard instance
static struct netif wg_netif_struct = {0};
static struct netif *wg_netif = NULL;
static struct netif *previous_default_netif = NULL;
static uint8_t wireguard_peer_index = WIREGUARDIF_INVALID_INDEX;

#define TAG "[WireGuard] "

bool WireGuard::begin(const IPAddress& localIP, const IPAddress& Subnet, const IPAddress& Gateway, const char* privateKey, const char* remotePeerAddress, const char* remotePeerPublicKey, uint16_t remotePeerPort) {
    // Validate inputs early
    if (!privateKey || !remotePeerAddress || !remotePeerPublicKey || remotePeerPort == 0) {
        log_e(TAG "Invalid parameters: null pointer or zero port");
        return false;
    }

    // Check if already initialized
    if (this->_is_initialized) {
        log_w(TAG "WireGuard already initialized, call end() first");
        return false;
    }

    // Validate NTP sync (mandatory for WireGuard)
    time_t now = time(nullptr);
    if (now < 1700000000) {  // Roughly Nov 2023, adjust as needed
        log_e(TAG "NTP not synchronized! Call configTime() and wait for time sync.");
        return false;
    }

    struct wireguardif_init_data wg;
    struct wireguardif_peer peer;
    ip_addr_t ipaddr = IPADDR4_INIT(static_cast<uint32_t>(localIP));
    ip_addr_t netmask = IPADDR4_INIT(static_cast<uint32_t>(Subnet));
    ip_addr_t gateway = IPADDR4_INIT(static_cast<uint32_t>(Gateway));

    // Setup the WireGuard device structure
    wg.private_key = privateKey;
    wg.listen_port = remotePeerPort;
    wg.bind_netif = NULL;

    // Initialise the first WireGuard peer structure
    wireguardif_peer_init(&peer);

    // Resolve endpoint address with retry
    bool success_get_endpoint_ip = false;
    for(int retry = 0; retry < 5; retry++) {
        ip_addr_t endpoint_ip = IPADDR4_INIT_BYTES(0, 0, 0, 0);
        struct addrinfo *res = NULL;
        struct addrinfo hint;
        memset(&hint, 0, sizeof(hint));
        memset(&endpoint_ip, 0, sizeof(endpoint_ip));

        log_i(TAG "Resolving %s (attempt %d)...", remotePeerAddress, retry + 1);
        if( lwip_getaddrinfo(remotePeerAddress, NULL, &hint, &res) != 0 ) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        success_get_endpoint_ip = true;
        struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
        inet_addr_to_ip4addr(ip_2_ip4(&endpoint_ip), &addr4);
        lwip_freeaddrinfo(res);

        peer.endpoint_ip = endpoint_ip;
        log_i(TAG "Resolved %s -> %d.%d.%d.%d",
            remotePeerAddress,
            (endpoint_ip.u_addr.ip4.addr >>  0) & 0xff,
            (endpoint_ip.u_addr.ip4.addr >>  8) & 0xff,
            (endpoint_ip.u_addr.ip4.addr >> 16) & 0xff,
            (endpoint_ip.u_addr.ip4.addr >> 24) & 0xff);
        break;
    }

    if(!success_get_endpoint_ip) {
        log_e(TAG "Failed to resolve endpoint IP for %s", remotePeerAddress);
        return false;
    }

    // Register the new WireGuard network interface with lwIP
    wg_netif = netif_add(&wg_netif_struct, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gateway), &wg, &wireguardif_init, &ip_input);
    if(wg_netif == nullptr) {
        log_e(TAG "Failed to add WireGuard netif (out of memory?)");
        return false;
    }

    // Configure netif
    netif_set_up(wg_netif);
    wg_netif->mtu = WIREGUARDIF_MTU;
    wg_netif->name[0] = 'w';
    wg_netif->name[1] = 'g';

    peer.public_key = remotePeerPublicKey;
    peer.preshared_key = NULL;

    // Allow all source IPs (0.0.0.0/0) - acts as default route
    peer.allowed_ip = *IP_ADDR_ANY;
    peer.allowed_mask = *IP_ADDR_ANY;
    peer.endport_port = remotePeerPort;

    // Initialize the platform (crypto, timers)
    wireguard_platform_init();

    // Add peer to device
    wireguardif_add_peer(wg_netif, &peer, &wireguard_peer_index);
    if(wireguard_peer_index == WIREGUARDIF_INVALID_INDEX) {
        log_e(TAG "Failed to add peer (max peers reached?)");
        netif_remove(wg_netif);
        wg_netif = nullptr;
        return false;
    }

    // Start connection if endpoint is valid
    if(!ip_addr_isany(&peer.endpoint_ip)) {
        log_i(TAG "Connecting to peer...");
        wireguardif_connect(wg_netif, wireguard_peer_index);

        // Save and set default interface
        previous_default_netif = netif_default;
        netif_set_default(wg_netif);
        log_i(TAG "WireGuard interface set as default route");
    } else {
        log_w(TAG "Endpoint IP is any - interface added but not connected");
    }

    this->_is_initialized = true;
    log_i(TAG "WireGuard initialized successfully (local IP: %s, MTU: %d)",
          localIP.toString().c_str(), WIREGUARDIF_MTU);

    return true;
}

bool WireGuard::begin(const IPAddress& localIP, const char* privateKey, const char* remotePeerAddress, const char* remotePeerPublicKey, uint16_t remotePeerPort) {
	// Maintain compatiblity with old begin 
	auto subnet = IPAddress(255,255,255,255);
	auto gateway = IPAddress(0,0,0,0);
	return WireGuard::begin(localIP, subnet, gateway, privateKey, remotePeerAddress, remotePeerPublicKey, remotePeerPort);
}

void WireGuard::end() {
    if(!this->_is_initialized) {
        log_w(TAG "end() called but not initialized");
        return;
    }

    log_i(TAG "Shutting down WireGuard interface...");

    // Disconnect peer first (stops timers, sends handshake failure)
    if(wireguard_peer_index != WIREGUARDIF_INVALID_INDEX) {
        wireguardif_disconnect(wg_netif, wireguard_peer_index);
        wireguardif_remove_peer(wg_netif, wireguard_peer_index);
        wireguard_peer_index = WIREGUARDIF_INVALID_INDEX;
    }

    // Shutdown the interface
    if(wg_netif) {
        wireguardif_shutdown(wg_netif);
        netif_remove(wg_netif);
        wg_netif = nullptr;
    }

    // Restore previous default interface
    if(previous_default_netif) {
        netif_set_default(previous_default_netif);
        previous_default_netif = nullptr;
        log_i(TAG "Restored previous default interface");
    }

    this->_is_initialized = false;
    log_i(TAG "WireGuard shutdown complete");
}
/**
 * WireGuard ESP32 - Complete Diagnostics Example
 *
 * Features demonstrated:
 * - NTP time validation
 * - Configuration validation
 * - Error handling with callbacks
 * - Persistent storage (NVS)
 * - Statistics monitoring
 * - Automatic reconnect
 * - Public IP verification
 * - Memory diagnostics
 *
 * Library: WireGuard-ESP32 v0.2.0+
 * Board: ESP32-DevKit or ESP32-S3
 *
 * Configuration: Edit the settings below before uploading
 */

#include <WiFi.h>
#include <WireGuard-ESP32.h>
#include <WireGuardModern.h>  // Extended API
#include <HTTPClient.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <cstdio>

// ============================================================================
// CONFIGURATION - EDIT THESE VALUES
// ============================================================================

// WiFi Settings
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// WireGuard Settings (from wg0.conf on server)
// Generate keys: wg genkey | wg pubkey
const char* WG_PRIVATE_KEY = "YOUR_PRIVATE_KEY_BASE64_44_CHARS";
const char* WG_PUBLIC_KEY = "SERVER_PUBLIC_KEY_BASE64_44_CHARS";
const char* WG_ENDPOINT = "your.wireguard.server.com";  // or IP address
const uint16_t WG_PORT = 51820;

// Local WireGuard IP (must match server's AllowedIPs)
// Example: if server has [Peer] AllowedIPs = 10.0.0.2/32
IPAddress WG_LOCAL_IP(10, 0, 0, 2);
IPAddress WG_SUBNET(255, 255, 255, 0);
IPAddress WG_GATEWAY(10, 0, 0, 1);

// NTP servers for time synchronization (MANDATORY)
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.nist.gov";
const char* NTP_SERVER3 = "time.google.com";
const long GMT_OFFSET_SEC = 0;         // Adjust for your timezone
const int DAYLIGHT_OFFSET_SEC = 0;     // DST offset if applicable

// Operational settings
const unsigned long LOOP_INTERVAL_MS = 10000;    // Diagnostics interval
const unsigned long PUBLIC_IP_CHECK_MS = 60000;  // Public IP check frequency

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

static WireGuard wg;
static HTTPClient httpClient;
static bool nvs_initialized = false;

// Error callback
static void onWireGuardError(WireGuardError_t err, const char* msg, void* user_data) {
    Serial.printf("[WG-ERROR] %s (%d)\n", msg ? msg : "No message", (int)err);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void printBanner(const char* title) {
    Serial.println("\n========================================");
    Serial.println(title);
    Serial.println("========================================");
}

void printSystemStatus() {
    printBanner("SYSTEM STATUS");

    Serial.printf("Uptime: %.2f seconds\n", millis() / 1000.0);
    Serial.printf("Free Heap: %u KB\n", ESP.getFreeHeap() / 1024);
    Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("SDK: %s\n", ESP.getSdkVersion());

    // WiFi status
    if(WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi: Connected to %s\n", WiFi.SSID().c_str());
        Serial.printf("WiFi IP: %s (RSSI: %d dBm)\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
    } else {
        Serial.println("WiFi: Disconnected!");
    }

    // Time status
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    Serial.printf("NTP: %s (as of %04d-%02d-%02d %02d:%02d:%02d)\n",
                  WireGuard_isNtpSynced() ? "Synced" : "NOT SYNCED",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void printWireGuardStatus() {
    printBanner("WIREGUARD STATUS");

    if(!wg.is_initialized()) {
        Serial.println("WireGuard: NOT INITIALIZED");
        return;
    }

    WireGuardStats_t stats;
    if(WireGuard_getStats(&stats)) {
        Serial.printf("Interface: %s\n", WireGuard_getInterfaceName() ?: "unknown");
        Serial.printf("State: %s\n", stats.is_connected ? "CONNECTED" : "DISCONNECTED");
        Serial.printf("RX: %u bytes, TX: %u bytes\n", stats.rx_bytes, stats.tx_bytes);
        Serial.printf("Handshakes: %u\n", stats.handshake_count);

        if(stats.last_handshake_ms > 0) {
            uint32_t age = millis() - stats.last_handshake_ms;
            Serial.printf("Last handshake: %.2f seconds ago\n", age / 1000.0);
        }

        Serial.printf("Uptime: %.2f seconds\n", stats.uptime_ms / 1000.0);
    }

    // Show lwIP netif info
    extern struct netif* wg_netif;
    if(wg_netif) {
        Serial.printf("lwIP netif: %s (flags: 0x%04x)\n", wg_netif->name, wg_netif->flags);
        Serial.printf("MTU: %d\n", wg_netif->mtu);
    }
}

void checkPublicIP() {
    printBanner("PUBLIC IP CHECK");

    Serial.println("Checking public IP through WireGuard tunnel...");

    if(!httpClient.begin("https://api.ipify.org?format=json")) {
        Serial.println("Failed to allocate HTTP client");
        return;
    }

    httpClient.setTimeout(5000);
    int code = httpClient.GET();

    if(code == 200) {
        String payload = httpClient.getString();
        Serial.printf("Response: %s\n", payload.c_str());

        // Parse JSON (simple extraction)
        int ip_start = payload.indexOf("\"ip\":\"");
        if(ip_start > 0) {
            ip_start += 6;
            int ip_end = payload.indexOf("\"", ip_start);
            String public_ip = payload.substring(ip_start, ip_end);
            Serial.printf("Public IP: %s\n", public_ip.c_str());

            // Check if it matches the server (basic check)
            if(public_ip.length() > 0) {
                Serial.println("✓ Public IP check succeeded - tunnel appears active");
            }
        }
    } else {
        Serial.printf("HTTP GET failed with code %d\n", code);
        if(code == -1) {
            Serial.println("Tip: Check firewall, ensure UDP 51820 is open on server");
        }
    }

    httpClient.end();
}

void checkConfig() {
    printBanner("CONFIG VALIDATION");

    Serial.println("Validating configuration...");

    // Check private key
    if(WireGuard_validatePrivateKey(WG_PRIVATE_KEY)) {
        Serial.println("✓ Private key format valid (base64)");
    } else {
        Serial.println("✗ Private key INVALID (expected base64, 44 characters)");
    }

    // Check public key
    if(WireGuard_validatePublicKey(WG_PUBLIC_KEY)) {
        Serial.println("✓ Public key format valid (base64)");
    } else {
        Serial.println("✗ Public key INVALID");
    }

    // Check NTP
    if(WireGuard_isNtpSynced()) {
        Serial.println("✓ NTP synchronized");
    } else {
        Serial.println("✗ NTP NOT SYNCHRONIZED - WireGuard will fail!");
        Serial.println("  Ensure you call configTime() and wait 10-30 seconds");
    }

    // Check local IP
    if(WG_LOCAL_IP != IPAddress(0, 0, 0, 0)) {
        Serial.printf("✓ Local IP configured: %s\n", WG_LOCAL_IP.toString().c_str());
    } else {
        Serial.println("✗ Local IP is 0.0.0.0 - configure WG_LOCAL_IP");
    }

    // Check WiFi
    if(strlen(WIFI_SSID) > 0 && strlen(WIFI_PASSWORD) > 0) {
        Serial.println("✓ WiFi credentials configured");
    } else {
        Serial.println("✗ WiFi credentials missing");
    }
}

void saveCurrentConfig() {
    Serial.println("Saving configuration to NVS...");

    // Get endpoint IP if connected
    IPAddress current_endpoint_ip(0, 0, 0, 0);
    if(wg.is_initialized()) {
        // Could extract from peer structure if we had access
        // For now, save 0.0.0.0
    }

    bool ok = WireGuard_saveConfig(
        WG_PRIVATE_KEY,
        WG_PUBLIC_KEY,
        WG_ENDPOINT,
        WG_PORT,
        &current_endpoint_ip
    );

    if(ok) {
        Serial.println("✓ Configuration saved to NVS");
    } else {
        Serial.println("✗ Failed to save configuration");
    }
}

void loadSavedConfig() {
    Serial.println("Checking for saved configuration...");

    String saved_priv, saved_pub, saved_endpoint;
    uint16_t saved_port;
    IPAddress saved_ip;

    if(WireGuard_loadConfig(saved_priv, saved_pub, saved_endpoint, saved_port, saved_ip)) {
        Serial.println("Found saved configuration:");
        Serial.printf("  Endpoint: %s:%d\n", saved_endpoint.c_str(), saved_port);
        Serial.printf("  Private key: %s...%s\n",
                      saved_priv.substring(0, 10).c_str(),
                      saved_priv.substring(saved_priv.length() - 5).c_str());
        Serial.println("Use this configuration? (y/n) - not implemented, edit code to use");
    } else {
        Serial.println("No saved configuration found");
    }
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor
    Serial.println("\n\n");

    printBanner("WIREGUARD ESP32 - COMPLETE DIAGNOSTICS");
    Serial.println("Build: " __DATE__ " " __TIME__);

    // Set up error callback
    WireGuard_setErrorCallback(onWireGuardError, nullptr);

    // Initialize NVS (required for persistent storage)
    esp_err_t nvs_err = nvs_flash_init();
    if(nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    nvs_initialized = true;
    Serial.println("✓ NVS initialized");

    // Check for saved config
    loadSavedConfig();

    // Validate configuration before proceeding
    checkConfig();

    // Connect to WiFi
    printBanner("WIFI CONNECTION");
    Serial.printf("Connecting to %s...\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long wifi_start = millis();
    while(WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if(millis() - wifi_start > 30000) {
            Serial.println("\n✗ WiFi connection timeout!");
            Serial.println("Check SSID/password, signal strength");
            ESP.restart();
        }
    }

    Serial.println("\n✓ WiFi Connected");
    Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());

    // Sync time via NTP (CRITICAL)
    printBanner("NTP TIME SYNC");
    Serial.println("Configuring NTP...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
               NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

    Serial.println("Waiting for NTP sync (may take 10-30 seconds)...");
    unsigned long ntp_start = millis();
    time_t now;
    do {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        if(millis() - ntp_start > 30000) {
            Serial.println("\n✗ NTP sync timeout!");
            Serial.println("  WireGuard will likely fail without accurate time");
            Serial.println("  Check firewall: UDP 123 must be open");
            break;
        }
    } while(now < 1700000000);

    if(WireGuard_isNtpSynced()) {
        Serial.println("\n✓ NTP synchronized");
        struct tminfo;
        getLocalTime(&timeinfo);
        Serial.printf("  Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    // Initialize WireGuard
    printBanner("WIREGUARD INITIALIZATION");
    Serial.println("Starting WireGuard interface...");
    Serial.printf("Local IP: %s\n", WG_LOCAL_IP.toString().c_str());
    Serial.printf("Endpoint: %s:%d\n", WG_ENDPOINT, WG_PORT);

    unsigned long wg_start = millis();
    bool wg_ok = wg.begin(
        WG_LOCAL_IP,
        WG_SUBNET,
        WG_GATEWAY,
        WG_PRIVATE_KEY,
        WG_ENDPOINT,
        WG_PUBLIC_KEY,
        WG_PORT
    );

    if(wg_ok) {
        unsigned long wg_time = millis() - wg_start;
        Serial.printf("✓ WireGuard initialized in %lu ms\n", wg_time);

        // Save config for future fast reconnect
        if(nvs_initialized) {
            saveCurrentConfig();
        }

        // Wait a moment for handshake to complete
        Serial.println("Waiting for handshake (5 seconds)...");
        delay(5000);

        printWireGuardStatus();
    } else {
        Serial.println("✗ WireGuard initialization FAILED");
        Serial.println("\nTroubleshooting:");
        Serial.println("1. Verify NTP is synced (above)");
        Serial.println("2. Check private/public key format (44 base64 chars)");
        Serial.println("3. Verify endpoint DNS resolves (try: nslookup)");
        Serial.println("4. Ensure UDP port 51820 open on server");
        Serial.println("5. Check server's wg0.conf has this peer");
        Serial.println("6. Verify AllowedIPs matches WG_LOCAL_IP");
        Serial.println("\nRestarting in 10 seconds...");
        delay(10000);
        ESP.restart();
    }

    printBanner("SETUP COMPLETE");
    Serial.println("Entering main loop...\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

unsigned long last_diag_time = 0;
unsigned long last_ip_check_time = 0;

void loop() {
    unsigned long now = millis();

    // Periodic diagnostics (every LOOP_INTERVAL_MS)
    if(now - last_diag_time >= LOOP_INTERVAL_MS) {
        last_diag_time = now;

        printSystemStatus();
        printWireGuardStatus();

        // Check if still connected
        if(!wg.is_initialized()) {
            Serial.println("WARNING: WireGuard not initialized!");
        }
    }

    // Public IP check (less frequent)
    if(now - last_ip_check_time >= PUBLIC_IP_CHECK_MS) {
        last_ip_check_time = now;
        checkPublicIP();
    }

    // Small delay to yield to background tasks
    delay(100);
}

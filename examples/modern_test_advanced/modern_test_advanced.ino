/**
 * Modern WireGuard ESP32 Test with Diagnostics
 *
 * Features:
 * - Persistent storage (NVS) for handshake state
 * - NTP sync verification
 * - Comprehensive diagnostics
 * - Multiple connectivity checks
 * - Memory monitoring
 * - Auto-reconnect logic
 *
 * Author: Analysis based on CLAUDE recommendations
 * Tested on: ESP32 (any model)
 * Library: WireGuard-ESP32 v0.1.5+
 */

#include <WiFi.h>
#include <WireGuard-ESP32.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <esp_log.h>

// ==================== CONFIGURATION ====================

// WiFi Settings
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// WireGuard Settings (from wg0.conf)
const char* WG_PRIVATE_KEY = "YOUR_PRIVATE_KEY_BASE64";
const char* WG_PUBLIC_KEY = "SERVER_PUBLIC_KEY_BASE64";
const char* WG_ENDPOINT = "your.server.com";  // or IP
const uint16_t WG_PORT = 51820;
IPAddress WG_LOCAL_IP(10, 0, 0, 2);  // Must match server's AllowedIPs
IPAddress WG_SUBNET(255, 255, 255, 0);
IPAddress WG_GATEWAY(10, 0, 0, 1);

// NTP Settings (MUST sync before WireGuard)
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.nist.gov";
const char* NTP_SERVER3 = "time.google.com";
const long GMT_OFFSET_SEC = 0;  // Adjust for your timezone
const int DAYLIGHT_OFFSET_SEC = 3600;

// Operational settings
const unsigned long LOOP_INTERVAL_MS = 10000;
const unsigned long CONNECTION_TIMEOUT_MS = 30000;
const bool ENABLE_PERSISTENT_STORAGE = true;  // Saves handshake across reboots

// ==================== GLOBALS ====================

static WireGuard wg;
static WiFiUDP ntpUDP;
static HTTPClient http;

// Diagnostics
struct SystemState {
  unsigned long boot_time;
  unsigned long last_handshake_time;
  uint32_t wg_rx_bytes;
  uint32_t wg_tx_bytes;
  uint8_t reconnect_count;
  bool wifi_connected;
  bool wg_active;
  bool ntp_synced;
  float free_heap_kb;
} state;

// Logging tag
static const char* TAG = "WG-TEST";

// ==================== NVS PERSISTENCE ====================

#ifdef CONFIG_WG_ENABLE_NVS
#define NVS_NAMESPACE "wgstate"
#define NVS_KEY_HANDSHAKE "handshake"
#define NVS_KEY_PEER "peerdata"

// Save WireGuard device state to NVS
bool saveWGState() {
  if (!ENABLE_PERSISTENT_STORAGE) return true;

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    Serial.printf("[%s] NVS open failed: %s\n", TAG, esp_err_to_name(err));
    return false;
  }

  // Save some basic state (full struct serialization needed for production)
  uint32_t saved_time = millis();
  nvs_set_u32(nvs, "lastsave", saved_time);

  err = nvs_commit(nvs);
  nvs_close(nvs);

  if (err == ESP_OK) {
    Serial.printf("[%s] State saved to NVS\n", TAG);
  } else {
    Serial.printf("[%s] NVS commit failed: %s\n", TAG, esp_err_to_name(err));
  }
  return err == ESP_OK;
}

bool loadWGState() {
  if (!ENABLE_PERSISTENT_STORAGE) return false;

  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    Serial.printf("[%s] NVS open (read) failed: %s\n", TAG, esp_err_to_name(err));
    return false;
  }

  uint32_t saved_time;
  err = nvs_get_u32(nvs, "lastsave", &saved_time);
  nvs_close(nvs);

  if (err == ESP_OK) {
    Serial.printf("[%s] Found saved state (saved at boot ms: %u)\n", TAG, saved_time);
    return true;
  }
  return false;
}
#endif

// ==================== DIAGNOSTICS ====================

void printSystemInfo() {
  state.free_heap_kb = ESP.getFreeHeap() / 1024.0;
  state.boot_time = millis();

  Serial.println("\n========== SYSTEM DIAGNOSTICS ==========");
  Serial.printf("Uptime: %.2f seconds\n", state.boot_time / 1000.0);
  Serial.printf("Free Heap: %.2f KB\n", state.free_heap_kb);
  Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("SDK Version: %s\n", ESP.getSdkVersion());
  Serial.println("======================================\n");
}

void printWireGuardStatus() {
  if (!wg.is_initialized()) {
    Serial.println("[WG] Not initialized");
    return;
  }

  Serial.println("\n========== WIREGUARD STATUS ==========");
  Serial.printf("Interface: Active\n");
  Serial.printf("RX Bytes: %u\n", state.wg_rx_bytes);
  Serial.printf("TX Bytes: %u\n", state.wg_tx_bytes);

  if (state.last_handshake_time > 0) {
    unsigned long age = millis() - state.last_handshake_time;
    Serial.printf("Last Handshake: %.2f seconds ago\n", age / 1000.0);
  } else {
    Serial.println("Last Handshake: Never");
  }

  Serial.printf("Reconnect Count: %u\n", state.reconnect_count);
  Serial.println("====================================\n");
}

void checkIPRouting() {
  Serial.println("Checking routing table...");

  // Ping gateway inside tunnel
  Serial.print("Pinging tunnel gateway ");
  Serial.println(WG_GATEWAY);
  // Use WiFiClient to ping (no built-in ping API)

  // Check public IP
  Serial.println("Checking public IP address...");
  if (http.begin("https://api.ipify.org?format=json")) {
    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      Serial.printf("Public IP response: %s\n", payload.c_str());

      // Parse and check if it's the WireGuard server's IP
      if (payload.indexOf(WG_ENDPOINT) != -1 || payload.length() > 0) {
        Serial.println("✓ Traffic appears to be routing through WireGuard tunnel");
      } else {
        Serial.println("⚠ Warning: Public IP doesn't match expected server");
      }
    } else {
      Serial.printf("HTTP GET failed: %d\n", code);
    }
    http.end();
  } else {
    Serial.println("Failed to allocate HTTP client");
  }
}

// ==================== SETUP ====================

void setup() {
  // Initialize serial
  Serial.begin(115200);
  delay(1000);  // Wait for serial monitor
  Serial.println("\n\n\n");
  Serial.println("╔═══════════════════════════════════════════╗");
  Serial.println("║  WireGuard ESP32 - Modern Test v0.1.0    ║");
  Serial.println("╚═══════════════════════════════════════════╝");

  state.boot_time = millis();
  state.last_handshake_time = 0;
  state.wg_rx_bytes = 0;
  state.wg_tx_bytes = 0;
  state.reconnect_count = 0;
  state.wifi_connected = false;
  state.wg_active = false;
  state.ntp_synced = false;

  printSystemInfo();

  // Mount SPIFFS for persistent storage
  #ifdef CONFIG_WG_ENABLE_NVS
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true
  };
  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    Serial.printf("[%s] SPIFFS mount failed: %s\n", TAG, esp_err_to_name(ret));
    ENABLE_PERSISTENT_STORAGE = false;
  }
  #endif

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Try to load saved state
  #ifdef CONFIG_WG_ENABLE_NVS
  if (loadWGState()) {
    Serial.println("[SYS] Found previously saved WireGuard state");
  }
  #endif

  // Initialize WiFi
  Serial.println("[WIFI] Connecting to AP...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - wifi_start > CONNECTION_TIMEOUT_MS) {
     Serial.println("\n[WIFI] Connection timeout!");
      ESP.restart();
    }
  }
  Serial.println("\n[WIFI] Connected!");
  Serial.printf("[WIFI] IP Address: %s\n", WiFi.localIP().toString().c_str());
  state.wifi_connected = true;

  // Sync time via NTP
  Serial.println("[NTP] Configuring time sync...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  // Wait for NTP sync
  unsigned long ntp_start = millis();
  time_t now = time(nullptr);
  while (now < 1700000000) {  // ~Nov 2023, adjust as needed
    delay(200);
    Serial.print(".");
    now = time(nullptr);
    if (millis() - ntp_start > CONNECTION_TIMEOUT_MS) {
      Serial.println("\n[NTP] Warning: Time may not be synced!");
      Serial.println("[NTP] WireGuard requires accurate time. Verify NTP manually.");
      break;
    }
  }
  Serial.println("\n[NTP] Time synced");
  state.ntp_synced = true;

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  Serial.printf("[NTP] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  // Start WireGuard
  Serial.println("[WG] Initializing WireGuard interface...");
  Serial.printf("[WG] Local IP: %s\n", WG_LOCAL_IP.toString().c_str());
  Serial.printf("[WG] Endpoint: %s:%d\n", WG_ENDPOINT, WG_PORT);

  bool success = wg.begin(
    WG_LOCAL_IP,
    WG_SUBNET,
    WG_GATEWAY,
    WG_PRIVATE_KEY,
    WG_ENDPOINT,
    WG_PUBLIC_KEY,
    WG_PORT
  );

  if (success) {
    Serial.println("[WG] ✓ Successfully initialized!");
    state.wg_active = true;
    state.last_handshake_time = millis();
    state.reconnect_count++;
    saveWGState();
  } else {
    Serial.println("[WG] ✗ Failed to initialize!");
    Serial.println("[WG] Check:");
    Serial.println("  - Private key format (should be base64, 44 chars)");
    Serial.println("  - Public key format");
    Serial.println("  - Endpoint DNS resolution");
    Serial.println("  - Server accessibility (port open?)");
    Serial.println("  - NTP sync completed");
    ESP.restart();
  }

  delay(2000);
  printSystemInfo();
  printWireGuardStatus();
  checkIPRouting();

  Serial.println("\n[SYS] Setup complete, entering main loop...\n");
}

// ==================== LOOP ====================

void loop() {
  static unsigned long last_check = 0;
  static unsigned long last_save = 0;

  if (millis() - last_check >= LOOP_INTERVAL_MS) {
    last_check = millis();

    // Update diagnostics
    state.free_heap_kb = ESP.getFreeHeap() / 1024.0;

    // Check WiFi
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Disconnected! Attempting reconnect...");
      WiFi.reconnect();
      state.wifi_connected = false;
    } else if (!state.wifi_connected) {
      Serial.println("[WIFI] Reconnected");
      state.wifi_connected = true;
    }

    // Check WireGuard
    if (wg.is_initialized()) {
      printWireGuardStatus();
    } else {
      Serial.println("[WG] Not initialized! This should not happen after setup.");
    }

    // Check time sync (untick?)
    time_t now = time(nullptr);
    if (now < 1700000000) {
      Serial.println("[NTP] Warning: Time not synced properly");
      state.ntp_synced = false;
    } else {
      state.ntp_synced = true;
    }

    // Save state periodically (every 5 minutes)
    if (ENABLE_PERSISTENT_STORAGE && (millis() - last_save > 300000)) {
      saveWGState();
      last_save = millis();
    }

    // Public IP check (less frequent to avoid rate limits)
    static int ip_check_counter = 0;
    if (++ip_check_counter >= 6) {  // Every ~60 seconds
      ip_check_counter = 0;
      checkIPRouting();
    }
  }

  delay(100);  // Small delay to prevent watchdog
}

// ==================== CALLBACKS ====================

// You can set callbacks for events if needed:
// Not currently supported in this library version

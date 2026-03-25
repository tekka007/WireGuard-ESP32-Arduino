# Testing Guide - WireGuard ESP32 v0.2.0

This guide will walk you through testing the modernized WireGuard library on actual ESP32 hardware.

## Prerequisites

### Hardware
- ESP32 development board (ESP32-DevKit, ESP32-S3, or ESP32-C3)
- USB cable for programming
- WiFi network with internet access
- WireGuard server (or VPS with WireGuard installed)

### Software
- PlatformIO (VSCode extension) OR Arduino IDE
- WireGuard tools on your server: `wg`, `wg-quick`

---

## Part 1: Set Up WireGuard Server

If you don't have a WireGuard server, here's quick setup on Ubuntu/Debian:

```bash
# Install WireGuard
sudo apt update
sudo apt install wireguard wireguard-tools

# Generate server keys
wg genkey | sudo tee /etc/wireguard/privatekey | wg pubkey | sudo tee /etc/wireguard/publickey

# Create config
sudo nano /etc/wireguard/wg0.conf
```

**wg0.conf:**
```ini
[Interface]
Address = 10.0.0.1/24
ListenPort = 51820
PrivateKey = SERVER_PRIVATE_KEY_HERE
SaveConfig = true

# Enable NAT
PostUp = iptables -A FORWARD -i wg0 -j ACCEPT; iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
PostDown = iptables -D FORWARD -i wg0 -j ACCEPT; iptables -t nat -D POSTROUTING -o eth0 -j MASQUERADE

[Peer]
# ESP32
PublicKey = ESP32_PUBLIC_KEY_HERE
AllowedIPs = 10.0.0.2/32
```

Enable IP forwarding:
```bash
echo "net.ipv4.ip_forward=1" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

Start WireGuard:
```bash
sudo systemctl enable wg-quick@wg0
sudo systemctl start wg-quick@wg0
```

Check status:
```bash
sudo wg show
```

Should show:
```
interface: wg0
  public key: (server public key)
  private key: (hidden)
  listening port: 51820

peer: (ESP32 public key will appear here after handshake)
  endpoint: (ESP32's public IP):51820
  allowed ips: 10.0.0.2/32
  latest handshake: now
  transfer: 0 B received, 0 B sent
```

---

## Part 2: Generate ESP32 Keys

On your **laptop** (not ESP32):

```bash
# Generate private key
wg genkey > esp32_private.key

# Get public key
cat esp32_private.key | wg pubkey > esp32_public.key

# View them
cat esp32_private.key  # e.g., YFz...=
cat esp32_public.key   # e.g., 5O/...=
```

**Important:** Private key is 44 characters of base64. Keep it secret!

---

## Part 3: Configure ESP32 Sketch

1. Open `examples/complete_diagnostics/complete_diagnostics.ino` in PlatformIO or Arduino IDE

2. Edit the configuration section at the top:

```cpp
// WiFi Settings
const char* WIFI_SSID = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";

// WireGuard Settings
const char* WG_PRIVATE_KEY = "YFz...your 44-char key...=";  // From esp32_private.key
const char* WG_PUBLIC_KEY = "5O/...server public key...=";   // From server's wg show
const char* WG_ENDPOINT = "your.server.ip-or-domain.com";  // Server's public IP/DNS
const uint16_t WG_PORT = 51820;

// Local WireGuard IP (must match server's AllowedIPs)
IPAddress WG_LOCAL_IP(10, 0, 0, 2);  // If server has AllowedIPs = 10.0.0.2/32
IPAddress WG_SUBNET(255, 255, 255, 0);
IPAddress WG_GATEWAY(10, 0, 0, 1);   // Server's WG IP
```

3. Save the file

---

## Part 4: Build and Flash

### Using PlatformIO (VSCode)

1. Install PlatformIO extension if not already
2. Open this project folder in VSCode
3. Click PlatformIO: Build (check icon in status bar) - should show "SUCCESS"
4. Click PlatformIO: Upload (arrow icon) - flashes to ESP32
5. Click PlatformIO: Monitor (plug icon) - opens serial monitor at 115200 baud

Or use commands:
```bash
pio run -e esp32dev            # Build
pio run -e esp32dev -t upload  # Flash
pio device monitor            # Serial output
```

### Using Arduino IDE

1. File → Open → navigate to `examples/complete_diagnostics/`
2. Select board: Tools → Board → ESP32 Dev Module
3. Verify/Compile (checkmark button)
4. Upload (right-arrow button)
5. Open Serial Monitor (magnifying glass icon) at 115200 baud

---

## Part 5: Monitor Boot Process

You should see output like this:

```
========================================
WIREGUARD ESP32 - COMPLETE DIAGNOSTICS
========================================
Build: Mar 25 2026 12:00:00

✓ NVS initialized
Checking for saved configuration...
No saved configuration found

========================================
CONFIG VALIDATION
========================================
✓ Private key format valid (base64)
✓ Public key format valid (base64)
✓ NTP synchronized
✓ Local IP configured: 10.0.0.2
✓ WiFi credentials configured

========================================
WIFI CONNECTION
========================================
Connecting to YourWiFi...
✓ WiFi Connected
  IP: 192.168.1.123
  RSSI: -45 dBm
  MAC: 30:AE:A4:xx:xx:xx

========================================
NTP TIME SYNC
========================================
Configuring NTP...
Waiting for NTP sync... ✓
  Current time: 2026-03-25 12:30:45

========================================
WIREGUARD INITIALIZATION
========================================
Starting WireGuard interface...
Local IP: 10.0.0.2
Endpoint: your.server.com:51820
Resolved your.server.com -> 203.0.113.45
✓ WireGuard initialized in 1850 ms
Waiting for handshake...

========================================
SETUP COMPLETE
========================================
Entering main loop...
```

---

## Part 6: Verify Connection

### Check ESP32 Serial (every 10 seconds)

```
========================================
SYSTEM STATUS
========================================
Uptime: 25.43 seconds
Free Heap: 180 KB
WiFi: Connected to YourWiFi
NTP: Synced

========================================
WIREGUARD STATUS
========================================
Interface: wg0
State: CONNECTED
RX: 1024 bytes, TX: 2048 bytes
Handshakes: 1
Last handshake: 2.34 seconds ago
```

### Check Server

On your WireGuard server:

```bash
sudo wg show
```

Output should show:
```
peer: (ESP32 public key)
  endpoint: (your.home.ip):54321  # Random high port from ESP32
  allowed ips: 10.0.0.2/32
  latest handshake: 1 second ago
  transfer: 1.23 KiB received, 2.45 KiB sent
```

### Check Public IP

ESP32 will run this automatically every 60 seconds:

```
========================================
PUBLIC IP CHECK
========================================
Checking public IP through WireGuard tunnel...
Response: {"ip":"YOUR.SERVER.PUBLIC.IP"}
Public IP: YOUR.SERVER.PUBLIC.IP
✓ Public IP check succeeded - tunnel appears active
```

**Important:** The public IP should be your server's public IP, not your home/office WiFi's IP!

---

## Part 7: Test Data Transfer

### Ping Test

From ESP32 sketch, add to `loop()` temporarily:

```cpp
#include <WiFiUdp.h>
WiFiUDP udp;

void loop() {
    // Ping server's WG IP
    if(udp.beginPacket("10.0.0.1", 53)) {  // DNS port
        udp.write("ping");
        udp.endPacket();
        Serial.println("Sent ping");
    }
    delay(5000);
}
```

### HTTP Through Tunnel

Already included in examples. Should be able to access:

```cpp
HTTPClient http;
http.begin("https://api.ipify.org?format=json");
int code = http.GET();
// Should return server's public IP
```

---

## Part 8: Performance Testing

### Throughput

On server, install iperf3:
```bash
sudo apt install iperf3
iperf3 -s -B 10.0.0.1  # Listen on WG IP
```

On ESP32, you'd need an iperf3 client (not common). Alternative:

Use a simple TCP echo test with `HTTPClient`:

```cpp
HTTPClient http;
http.begin("http://10.0.0.1:8080/100k");  // Server serves 100KB file
auto start = millis();
int code = http.GET();
auto duration = millis() - start;
if(code == 200) {
    int len = http.getSize();
    float mbps = (len * 8.0) / (duration / 1000.0) / 1000000.0;
    Serial.printf("Throughput: %.2f Mbps\n", mbps);
}
```

### Latency

Ping from ESP32 to server's WG IP:

```cpp
#include <ping.h>  // Not standard - use ICMP raw socket (advanced)
```

Simpler: Measure round-trip with UDP echo server on server.

### CPU Usage

In sketch, add:
```cpp
Serial.printf("CPU Load: %.1f%%\n", 100.0 * (1.0 - xPortGetCoreID() ? 0 : 1));
```

Actually use:
```cpp
#include "freertos/task.h"
extern "C" void vTaskGetRunTimeStats(char* buffer);
// Or simpler: time how long loop takes
```

---

## Part 9: Test Reboot Recovery (NVS)

1. Let ESP32 establish WireGuard connection
2. Wait 10 seconds (handshake complete)
3. Press EN/RESET button on ESP32
4. Watch serial output

**Expected:** After reboot, ESP32 should:
- Load saved config from NVS
- Skip 2-second handshake (reconnect instantly)
- Tunnels active within 500ms
- Statistics preserved (RX/TX continue)

If NVS working, you'll see:
```
✓ NVS initialized
Found saved configuration:
  Endpoint: your.server.com:51820
✓ Configuration loaded from NVS
```

---

## Part 10: Test Error Conditions

### Wrong Private Key

Change one character in WG_PRIVATE_KEY and reboot.

**Expected:** `begin()` returns false, error callback fires: "Invalid key format"

### Wrong Server Public Key

Change WG_PUBLIC_KEY.

**Expected:** Handshake fails. Server logs: "Invalid public key"

### Server Down

Stop WireGuard on server: `sudo systemctl stop wg-quick@wg0`

**Expected:** ESP32 will try to reconnect. `wg.is_initialized()` stays true but `netif_is_link_up()` becomes false. After server restart, automatically reconnects.

### No NTP

Comment out `configTime()` line and reboot.

**Expected:** `begin()` fails with "NTP not synchronized" error.

---

## Part 11: Performance Benchmarks

Record these metrics on your ESP32:

| Test | Expected (ESP32 240MHz) | Your Result |
|------|------------------------|-------------|
| WiFi connect time | 2-5 seconds | |
| NTP sync time | 5-30 seconds | |
| First handshake | 1.5-2.5 seconds | |
| Reconnect (with NVS) | <500 milliseconds | |
| HTTP 1KB latency | 50-150 ms | |
| HTTP 10KB latency | 100-300 ms | |
| HTTP 100KB throughput | 5-10 Mbps | |
| Free RAM after boot | >150 KB | |
| WireGuard RAM usage | 50-70 KB | |

---

## Part 12: Common Issues & Fixes

### "NTP not synchronized"

**Cause:** UDP 123 blocked by firewall
**Fix:** Allow NTP through router/firewall. Or use local NTP server.

### "Failed to resolve endpoint"

**Cause:** DNS fails or server name invalid
**Fix:** Use IP address instead of hostname. Or fix DNS settings.

### "WireGuard initialized but no handshake"

**Check:**
1. Server firewall allows UDP 51820
2. Server's AllowedIPs includes ESP32's WG IP (10.0.0.2)
3. Server has correct ESP32 public key
4. `wg show` on server shows peer but no handshake

### "Connection drops after 5 minutes"

**Cause:** NAT timeout. Router drops UDP mapping.
**Fix:** On server, add to peer config:
```ini
PersistentKeepalive = 25
```
Then reload: `sudo wg-quick down wg0 && sudo wg-quick up wg0`

### "ESP32 crashes after 1 hour"

**Cause:** Memory leak or watchdog timeout
**Fix:**
- Check free heap: `ESP.getFreeHeap()` decreases?
- Increase watchdog timeout: `pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0 --debug`
- Enable panic handler: `pio run -e esp32dev -t upload --set-upload-port /dev/ttyUSB0`

### "Unable to compile - missing header"

**Cause:** Library not properly installed
**Fix:**
- Arduino: Sketch → Include Library → Add .ZIP (select this repo root)
- PlatformIO: Ensure `lib_deps` empty, just use the library in `lib/` folder

---

## Part 13: Success Criteria

Your WireGuard ESP32 is working if:

✅ Serial shows "✓ WireGuard initialized successfully"
✅ Server shows "latest handshake: x seconds ago"
✅ Public IP check returns server's IP (not your local ISP)
✅ HTTP requests work through tunnel
✅ RX/TX counters increase over time
✅ Reboot recovery works (if NVS enabled)
✅ No crashes after 24+ hour run
✅ Heap doesn't decrease significantly (< 10KB drift)

---

## Next Steps

After successful basic setup:

1. **Optimize for production:**
   - Disable debug logging: `WireGuard_setLogLevel(WG_LOG_WARN);`
   - Enable flash encryption in Arduino IDE (Tools → Flash Mode → QIO + Encrypted)
   - Set NVS to encrypted (requires partition table edit)

2. **Add features as needed:**
   - Persistent keepalive (config on server)
   - DNS resolution for internal services
   - Fallback WiFi or secondary endpoint
   - Status LED blink on handshake

3. **Monitor:**
   - Set up logging to file on SD card (if available)
   - Watchdog: Reboot if tunnel down > 5 minutes
   - Metrics: Push to MQTT or HTTP endpoint

4. **Secure:**
   - Change default UDP port (51820) to something else
   - Use WireGuard's allowed IPs for specific routes only (not 0.0.0.0/0)
   - Consider rotating keys monthly

---

## Getting Help

- **Library Issues:** https://github.com/ciniml/WireGuard-ESP32-Arduino/issues
- **WireGuard Protocol:** https://www.wireguard.com/
- **ESP32 Problems:** https://github.com/espressif/arduino-esp32/issues
- **This Guide:** Improve by submitting PR

---

**Good luck! Your ESP32 now has enterprise-grade VPN capability.**

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

```
# context-mode — MANDATORY routing rules

You have context-mode MCP tools available. These rules are NOT optional — they protect your context window from flooding. A single unrouted command can dump 56 KB into context and waste the entire session.

## BLOCKED commands — do NOT attempt these

### curl / wget — BLOCKED
Any Bash command containing `curl` or `wget` is intercepted and replaced with an error message. Do NOT retry.
Instead use:
- `ctx_fetch_and_index(url, source)` to fetch and index web pages
- `ctx_execute(language: "javascript", code: "const r = await fetch(...)")` to run HTTP calls in sandbox

### Inline HTTP — BLOCKED
Any Bash command containing `fetch('http`, `requests.get(`, `requests.post(`, `http.get(`, or `http.request(` is intercepted and replaced with an error message. Do NOT retry with Bash.
Instead use:
- `ctx_execute(language, code)` to run HTTP calls in sandbox — only stdout enters context

### WebFetch — BLOCKED
WebFetch calls are denied entirely. The URL is extracted and you are told to use `ctx_fetch_and_index` instead.
Instead use:
- `ctx_fetch_and_index(url, source)` then `ctx_search(queries)` to query the indexed content

## REDIRECTED tools — use sandbox equivalents

### Bash (>20 lines output)
Bash is ONLY for: `git`, `mkdir`, `rm`, `mv`, `cd`, `ls`, `npm install`, `pip install`, and other short-output commands.
For everything else, use:
- `ctx_batch_execute(commands, queries)` — run multiple commands + search in ONE call
- `ctx_execute(language: "shell", code: "...")` — run in sandbox, only stdout enters context

### Read (for analysis)
If you are reading a file to **Edit** it → Read is correct (Edit needs content in context).
If you are reading to **analyze, explore, or summarize** → use `ctx_execute_file(path, language, code)` instead. Only your printed summary enters context. The raw file content stays in the sandbox.

### Grep (large results)
Grep results can flood context. Use `ctx_execute(language: "shell", code: "grep ...")` to run searches in sandbox. Only your printed summary enters context.

## Tool selection hierarchy

1. **GATHER**: `ctx_batch_execute(commands, queries)` — Primary tool. Runs all commands, auto-indexes output, returns search results. ONE call replaces 30+ individual calls.
2. **FOLLOW-UP**: `ctx_search(queries: ["q1", "q2", ...])` — Query indexed content. Pass ALL questions as array in ONE call.
3. **PROCESSING**: `ctx_execute(language, code)` | `ctx_execute_file(path, language, code)` — Sandbox execution. Only stdout enters context.
4. **WEB**: `ctx_fetch_and_index(url, source)` then `ctx_search(queries)` — Fetch, chunk, index, query. Raw HTML never enters context.
5. **INDEX**: `ctx_index(content, source)` — Store content in FTS5 knowledge base for later search.

## Subagent routing

When spawning subagents (Agent/Task tool), the routing block is automatically injected into their prompt. Bash-type subagents are upgraded to general-purpose so they have access to MCP tools. You do NOT need to manually instruct subagents about context-mode.

## Output constraints

- Keep responses under 500 words.
- Write artifacts (code, configs, PRDs) to FILES — never return them as inline text. Return only: file path + 1-line description.
- When indexing content, use descriptive source labels so others can `ctx_search(source: "label")` later.

## ctx commands

| Command | Action |
|---------|--------|
| `ctx stats` | Call the `ctx_stats` MCP tool and display the full output verbatim |
| `ctx doctor` | Call the `ctx_doctor` MCP tool, run the returned shell command, display as checklist |
| `ctx upgrade` | Call the `ctx_upgrade` MCP tool, run the returned shell command, display as checklist |
```

# WireGuard-ESP32-Arduino

WireGuard implementation for ESP32 Arduino using lwIP. **Status:** Functional v0.1.5 but needs modernization.

## Quick Start

### Prerequisites
- ESP32 board (any model)
- Arduino IDE 2.x or PlatformIO
- WiFi network
- WireGuard server endpoint

### Required Setup
1. **NTP is MANDATORY** - WireGuard requires accurate time:
   ```cpp
   configTime(gmt_offset, daylight_offset, "pool.ntp.org");
   // Wait 10-30s for sync before wg.begin()
   ```

2. **Configure keys and network:**
   - Generate keys: `wg genkey | wg pubkey`
   - Set local IP (matching server's AllowedIPs)
   - Configure server endpoint (DNS or IP)

3. **Build & flash** using PlatformIO or Arduino IDE

4. **Verify:** Check server with `sudo wg show` for handshake

See `examples/modern_test_advanced/` for comprehensive example with diagnostics.

---

## Repository Structure

```
src/
├── WireGuard-ESP32.h   # User API (C++)
├── WireGuard.cpp       # Wrapper, lwIP integration
├── wireguard.h         # Core protocol (C)
├── wireguard.c         # Protocol logic
├── wireguardif.h       # lwIP interface
├── wireguardif.c       # UDP + netif handling
├── wireguard-platform.h/c  # ESP32 platform layer
├── crypto.h            # Crypto abstraction
└── crypto/refc/        # Reference crypto (blake2s, x25519, chacha20poly1305)

examples/
├── uptime_post/        # HTTP through tunnel (SORACOM)
├── uptime_udp/         # UDP toggle demo
└── modern_test_advanced/  # Diagnostics example (new)

library.properties     # Arduino library metadata v0.1.5
platformio.ini         # PlatformIO configurations
ANALYSIS_AND_OPTIMIZATIONS.md  # Comprehensive analysis
CLAUDE.md              # This file
```

---

## Architecture

**4-Layer Design:**

1. **Arduino API** (`WireGuard` class): User-friendly C++ wrapper
2. **lwIP Integration** (`wireguardif`): Virtual netif, UDP socket management
3. **WireGuard Core** (`wireguard`): Protocol state machine, cryptographic operations
4. **Crypto** (`crypto/refc`): Reference implementations (no hardware acceleration)

**Data Flow:**
```
User App → lwIP stack → wireguardif_input()
     → wireguard_decrypt() → processed packet

Outbound: wireguard_encrypt() → UDP → wireguardif_output() → lwIP
```

**Memory:** ~50-60KB RAM, static allocation, `WIREGUARD_MAX_PEERS=1` (hard limit)

---

## API Reference

```cpp
#include <WireGuard-ESP32.h>

WireGuard wg;

// Full overload (recommended)
bool begin(
  IPAddress localIP,      // e.g., IPAddress(10, 0, 0, 2)
  IPAddress subnet,       // e.g., IPAddress(255, 255, 255, 0)
  IPAddress gateway,      // e.g., IPAddress(10, 0, 0, 1)
  const char* privateKey, // base64 string
  const char* endpoint,   // DNS or IP
  const char* publicKey,  // base64 string
  uint16_t port           // default 51820
);

// Legacy overload (subnet=255.255.255.255, gateway=0.0.0.0)
bool begin(IPAddress localIP, const char* privateKey,
           const char* endpoint, const char* publicKey,
           uint16_t port);

void end();  // Shutdown, restore previous default route
bool is_initialized() const;
```

**Return:** `true` on success, `false` on failure (check serial logs)

---

## Building

### PlatformIO (Recommended)

```bash
# Install PlatformIO in VSCode
# Edit platformio.ini to select board:
#   esp32dev (baseline)
#   esp32-s3-devkitc-1 (PSRAM + HW crypto)
#   esp32-c3-devkitm-1 (RISC-V)

pio run -e esp32dev           # Build
pio run -e esp32dev -t upload # Flash
pio device monitor            # Serial (115200)
```

### Arduino IDE

1. Install ESP32 Arduino core (v2.0.0+)
2. Add library: Sketch → Include Library → Add .ZIP (this repo)
3. Select board: ESP32 Dev Module
4. Open example → Verify/Upload

---

## Testing & Verification

### Hardware Test Setup

**Server (wg0.conf):**
```ini
[Interface]
Address = 10.0.0.1/24
ListenPort = 51820
PrivateKey = SERVER_PRIVATE_KEY

[Peer]
PublicKey = ESP32_PUBLIC_KEY
AllowedIPs = 10.0.0.2/32
```

Enable forwarding:
```bash
sysctl -w net.ipv4.ip_forward=1
# iptables rules for NAT if needed
```

**ESP32 Sketch:** Use `examples/modern_test_advanced/` for full diagnostics.

### Test Checklist

- [ ] NTP sync complete before handshake
- [ ] `wg begin()` returns true (check serial)
- [ ] Server: `sudo wg show` → handshake present
- [ ] Ping server's WG IP (10.0.0.1) succeeds
- [ ] HTTP request shows server's public IP (not local ISP)
- [ ] No memory leaks (free heap stable)
- [ ] Handshake rekeys after ~2 minutes
- [ ] WiFi disconnect → reconnect successful
- [ ] Reboot → handshake recovery (NVS if enabled)

### Performance Benchmarks (ESP32 240MHz)

| Metric | Value |
|--------|-------|
| Handshake time | 1.5-2.5s (first), 0.5-1s (rekey) |
| Max throughput | 10-15 Mbps |
| Sustained throughput | 5-8 Mbps |
| RAM usage | 50-60 KB |
| CPU during traffic | 5-10% |

---

## Development

### Modifying the Library

**Core files:**
- `WireGuard.cpp` - Main wrapper, error handling
- `wireguardif.c` - UDP I/O, lwIP callbacks
- `wireguard.c` - Protocol implementation (do not modify lightly)
- `wireguard-platform.h` - Constants (MAX_PEERS, timers)

**Rebuild after changes:**
- PlatformIO: `pio run -e esp32dev`
- Arduino: Sketch → Verify

### Adding Features

**Multiple peers:** Increase `WIREGUARD_MAX_PEERS` in `wireguard-platform.h`, recompile.

**Hardware crypto:** Not implemented. Requires integrating ESP-IDF's `esp_crypto` or mbedTLS with hardware backend. Expected 10-50x speedup.

**IPv6:** Not implemented. Would require adding `ip6_addr_t` support throughout `wireguardif` and `wireguard`.

**Persistent storage:** v0.1.5 has no state save. See `ANALYSIS_AND_OPTIMIZATIONS.md` for design.

---

## Known Limitations

1. **IPv4 only** - No IPv6 support
2. **Single peer** - Hard limit of 1 peer (can increase WIREGUARD_MAX_PEERS but no API to add/remove dynamically)
3. **No persistent storage** - Handshake lost on reboot (2s renegotiation)
4. **No keepalive config** - Uses default (disabled). Configure server's PersistentKeepalive.
5. **No roaming** - Endpoint IP change requires `end()` + `begin()`
6. **Software crypto only** - ESP32 crypto accelerators unused
7. **Minimal error reporting** - Logs to serial, no programmatic error codes
8. **No unit tests** - Test coverage: 0%

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| `begin()` returns false | NTP not synced | Wait 30s after WiFi connect, verify `time(nullptr) > 1700000000` |
| Handshake fails | Invalid key format | Keys must be base64 (44 characters) |
| No traffic after handshake | Routing issue | Check `netif_default` is WG interface |
| Intermittent disconnect | NAT timeout | Set `PersistentKeepalive = 25` on server |
| Slow performance | Software crypto | Use ESP32-S3 or enable HW crypto (future) |
| High CPU | Debug logging enabled | `esp_log_level_set("*", ESP_LOG_ERROR)` |
| DNS fails | DNS not set | Configure DNS in WiFi or use IP endpoint |

**Debug tips:**
```cpp
esp_log_level_set("*", ESP_LOG_DEBUG);  // Verbose
// Check routing
Serial.printf("Default netif: %s\n", netif_default->name);
// Monitor lwIP stats
extern struct stats_ lwip_stats;
```

---

## Security Considerations

- **Keys in RAM** - Not protected. Consider NVS encryption or efuse sealing for production.
- **Crypto timing** - Reference implementations may not be constant-time. High-security apps should replace with mbedTLS hardware backend.
- **Post-quantum** - Not quantum-resistant. Monitor NIST PQC standardization.
- **No security audit** - Use at own risk for critical applications.

---

## References

- **Original lwIP port:** https://github.com/smartalock/wireguard-lwip
- **WireGuard protocol:** https://www.wireguard.com/protocol/
- **ESP32 Arduino:** https://github.com/espressif/arduino-esp32
- **Comprehensive analysis:** See `ANALYSIS_AND_OPTIMIZATIONS.md` (modernization roadmap, testing guide, performance data)

---

## Contributing

- Fork → branch → test on real ESP32 hardware
- Maintain backward compatibility
- Update docs for new configuration options
- Add tests (any coverage helps)

## License

BSD-3-Clause (see LICENSE)

Copyright (c) 2021 Daniel Hope
Ported to ESP32 Arduino by Kenta Ida <fuga@fugafuga.org>

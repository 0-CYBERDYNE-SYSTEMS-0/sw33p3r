# Room Sweep v3.0.1 — Complete User Guide

## Quick Start

1. Launch **Room Sweep** from Apps → Tools on your Flipper
2. You start on the **RF tab** — it's already scanning 16 frequencies
3. **Short Back** opens Settings — turn Sound ON to hear Geiger clicks
4. **Left/Right** switches tabs. **Long Back** exits.

---

## Tabs (Left ◀ / Right ▶ to cycle, wraps around)

### 1. RF Tab — Sub-GHz Signal Detection

The RF tab has **3 sub-modes** (cycle with **Up ▲ / Down ▼**):

#### [SURVEY] — Quick 16-Point Scan (default)
- Sweeps 16 preset ISM frequencies (304–925 MHz) continuously
- Bar chart shows RSSI per channel
- Dotted line = alert threshold (-75 dBm)
- "SIGNAL!" banner when any channel exceeds threshold
- Peak dBm shown top-right
- **No button action needed** — runs automatically

#### [SWEEP] — Coarse Band Sweep
- **Long Left/Right** (when idle): select band (300-348 / 387-464 / 779-928 MHz)
- **Short Left/Right**: switch tabs
- **OK**: starts sweep (progress bar + current frequency + peak hold)
- **OK** (while running): cancels sweep
- After sweep: shows peak frequency and RSSI
- Step size: 250 kHz — finds signals BETWEEN the 16 presets

#### [PEAK] — Fine Peak Refinement
- Requires a detected signal (from Survey or Sweep)
- **OK**: starts fine sweep ±1 MHz around last detected peak
- Step size: 25 kHz — pinpoints exact frequency
- Shows refined frequency and RSSI when done
- If no peak: shows "No peak detected" message

---

### 2. WiFi Tab — Access Point Scanner

**Requires:** BFFB ESP32 with Marauder connected via UART. The app can acquire the
Flipper UART even when the board is absent, so actual scan result lines confirm the
board is responding.

- **Primary display:** Strongest AP RSSI (dBm) + meter bar + AP count + freshness
- **Secondary:** Top 3 APs sorted by signal (SSID + RSSI)
- **OK**: manually trigger scan
- **Auto-rescan:** every 5 seconds (configurable in Settings)
- **No response:** shows `ERR` after 30 seconds without Marauder result lines
- **Up ▲**: toggle Sound ON/OFF (test beep confirms)
- **Down ▼**: toggle Vibro ON/OFF (test buzz confirms)

When Sound is ON: Geiger clicks speed up as you move toward access points.

---

### 3. BLE Tab — Bluetooth Device Scanner (BFFB Marauder `sniffbt`)

**Requires:** BFFB ESP32 with Marauder connected via UART. Actual scan result lines
confirm that the board is responding.

- **Primary display:** Strongest device RSSI + meter bar + device count + freshness
- **Secondary:** Top 3 BLE devices (name + RSSI)
- **OK**: manually trigger sniff
- **Auto-rescan:** every 5 seconds
- **No response:** shows `ERR` after 30 seconds without Marauder result lines
- **Up ▲ / Down ▼**: Sound / Vibro toggle (same as WiFi)

---

### 4. GPS Tab — Position & Fix Status

**Requires:** BFFB ESP32 (Just Call Me Koko Marauder) with GPS module.

On tab enter the app sends **`nmea`** so the BFFB streams NMEA over UART
(115200 CLI). Passive listen alone never works — GPS lives on the ESP32, not on
the Flipper's UART. **OK** re-requests the stream if waiting, or sets/clears a
**mark** when a fix is present (distance shown via haversine).

- Shows: fix state (3D FIX / NO FIX / STALE), UTC time, date, satellites, lat/lon
- Fully passive — no buttons needed, just watches navigation NMEA data
- Sentence counter (bottom-right) confirms data is flowing
- **Up ▲ / Down ▼**: Sound / Vibro toggle

---

### 5. TX Tab — Transmit (SAFETY-GATED)

⚠️ **This tab radiates RF energy. Own property / licensed use only.**

**State machine (NEVER transmits accidentally):**

| State | Display | How to enter | How to exit |
|-------|---------|-------------|-------------|
| **DISARMED** | "DISARMED" + legal text | Default on tab entry | Press OK → ARMED |
| **ARMED** | Inverse "!! ARMED !!" + freq | Press OK from DISARMED | Long-OK → TRANSMIT, or Back → DISARMED |
| **TRANSMITTING** | Inverse countdown screen | Long-OK while ARMED | Auto-disarms when timer ends |

**Controls while DISARMED:**
- **OK**: Arm the transmitter (you hear a double-beep warning)
- Nothing else works — you MUST arm first

**Controls while ARMED:**
- **Up ▲ / Down ▼**: Change frequency (6 presets: 433.92, 868.35, 915.00, 315.00, 390.00, 418.00 MHz)
- **Long OK**: TRANSMIT (bounded by TX Duration setting, default 3s)
- **Back**: Disarm (back to safe state)

**Controls while TRANSMITTING:**
- Full-screen inverse countdown display
- **Auto-disarms** when timer expires
- No user input can extend transmission

**Signal → TX flow:** When RF Survey/Sweep detects a signal above threshold, that exact frequency is stored and pre-loaded when you enter TX. Up/Down while armed selects one of the six presets; the stored signal remains shown separately.

---

### 6. Info Tab — Live Status Card

Shows (always current, never stale):
- App version (v3.0.1)
- Tab count and RF sub-modes
- UART connection state (acquired / no device)
- Sound and Vibro actual state (ON/off)
- TX state (disarmed / ARMED / ACTIVE)
- Legal notice
- API version

---

## Settings Menu (Short Back from any tab)

| Item | Options | Effect |
|------|---------|--------|
| **Sound** | ON / OFF | Geiger clicks + lock tone. Test beep on enable. |
| **Vibro** | ON / OFF | Pulse on detection + heartbeat. Test buzz on enable. |
| **Auto-Rescan** | ON / OFF | WiFi/BLE auto-scan every 5s |
| **TX Duration** | 1–10s | How long TX transmits before auto-stop |

**Navigation:**
- **Up ▲ / Down ▼**: Move selection
- **OK**: Toggle/change selected item
- **Short Back**: Close settings
- **Long Back**: Exit the app

---

## Audio Feedback Behavior (when Sound = ON)

| Signal Level | Click Rate | Additional |
|-------------|-----------|------------|
| None (idle) | 1 click / 2 sec | Heartbeat — confirms audio is live |
| Weak (-100 to -90 dBm) | 1 click / 1.2 sec | — |
| Light (-90 to -80 dBm) | 1 click / 0.7 sec | — |
| Moderate (-80 to -70 dBm) | 1 click / 0.35 sec | — |
| Strong (-70 to -60 dBm) | 1 click / 0.18 sec | — |
| Very strong (-60 to -50 dBm) | 1 click / 0.1 sec | — |
| Extreme (> -50 dBm) | 1 click / 0.06 sec | Lock tone after 5 ticks above -75 |

**Lock tone:** A sustained note plays when signal stays above -75 dBm for 5+ consecutive ticks. Stops when signal drops.

## Vibro Feedback (when Vibro = ON)

- **Heartbeat:** Subtle pulse every 4 seconds (confirms it's active)
- **Detection edge:** Pulse on rising edge of signal above threshold
- **Sustained lock:** Pulse every 800ms while signal stays above threshold

---

## LED Behavior (always active, no setting — **per active tab**)

LED / sound / vibro follow the **current tab's** signal (RF peak, WiFi/BLE
strongest RSSI, GPS sat quality, TX arm/transmit state).

| Peak RSSI | LED |
|-----------|-----|
| < -85 dBm | Off |
| -85 to -75 | Green |
| -75 to -65 | Yellow |
| -65 to -55 | Red solid |
| > -55 | Red blinking |

---

## Button Summary (all contexts)

| Button | RF Tab | WiFi/BLE/GPS | TX Tab | Settings |
|--------|--------|-------------|--------|----------|
| **◀ Left** | Prev tab | Prev tab | Prev tab | — |
| **▶ Right** | Next tab | Next tab | Next tab | — |
| **▲ Up** | Sub-mode ↑ | Sound toggle | Freq ↑ (armed) | Menu ↑ |
| **▼ Down** | Sub-mode ↓ | Vibro toggle | Freq ↓ (armed) | Menu ↓ |
| **OK** | Start sweep | Start scan | Arm TX | Toggle item |
| **Long OK** | — | — | TRANSMIT | — |
| **Short Back** | Settings | Settings | Disarm/Settings | Close |
| **Long Back** | EXIT APP | EXIT APP | EXIT APP | EXIT APP |

---

## Hardware Requirements

| Feature | Hardware |
|---------|----------|
| RF Survey/Sweep/Peak | Flipper Zero (built-in CC1101) |
| WiFi scanning | BFFB ESP32 with Marauder firmware, UART-connected |
| BLE scanning | BFFB ESP32 with Marauder firmware, UART-connected |
| GPS | BFFB ESP32 with GPS module, UART-connected |
| TX | Flipper Zero (built-in CC1101) |
| Audio | Flipper Zero speaker |
| Vibro | Flipper Zero vibration motor |

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| No sound after enabling | Global Flipper volume at 0 | App forces volume — should work. Check speaker isn't physically blocked. |
| WiFi shows "No UART" | USART unavailable | Release the UART from another app, then plug in BFFB via UART (PC0/PC1) |
| WiFi shows data but no RSSI | Parser format mismatch | Capture serial output, report for parser update |
| TX won't transmit | Still DISARMED | Press OK to arm first, then Long-OK |
| Settings "crashes" | Was InputTypePress bug | Fixed in v3.0.1 — update |
| RF bars don't move | Normal in quiet environment | Walk near a WiFi router, radio, or remote control |

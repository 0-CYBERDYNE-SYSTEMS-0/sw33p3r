#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "room_sweep_scan.h"     /* prompt stripping */
#include "room_sweep_marauder.h" /* tokenizer helpers */
#include "room_sweep_wireless.h" /* identity match semantics */

/*
 * Phase 10 — hostile-tooling detection (pivoted from deauth detection,
 * which this firmware build cannot do: `sniffdeauth` is confirmed absent).
 * Two Marauder sources, run in ALTERNATING WINDOWS (only one Marauder scan
 * runs at a time; the host planner below decides when to switch, and the
 * caller sends `stopscan` before the new command):
 *
 *   sniffesp — other ESP32/Marauder-class WiFi devices
 *   sniffpwn — Pwnagotchi-class offensive-WiFi peers
 *
 * FORMAT PINS (upstream source; not yet observed live on this build —
 * annotated in docs/BFFB_MOMENTUM.md):
 *
 * sniffpwn (WiFiScan.cpp beaconSnifferCallback -> processPwnagotchiBeacon):
 *   v1.9.1:  Serial.print(F("Pwnagotchi Name: ")); Serial.println(name);
 *            Serial.print(F("Pwnd Totals: "));   Serial.println(pwnd_tot);
 *   master:  Serial.print(F("Name: ")); Serial.println(name);
 *            Serial.print(F("Pwnd #: ")); Serial.println(pwnd_tot);
 *   -> "Pwnagotchi Name: <name>" / "Name: <name>" (no RSSI, no MAC).
 *   Noise lines "JSON payload not found." / "Not a Pwnagotchi frame." are
 *   rejected (no "Name:" key).
 *
 * sniffesp: NO per-frame print exists in any current upstream tree
 * (v1.7.2…master): StartScan() has no WIFI_SCAN_ESPRESSIF branch, so the
 * command prints its banner and never streams lines. The historical
 * espressifSnifferCallback (commit 1a41361, 2020-07-02, removed in
 * bc3038c "Trim fat") printed
 *   "RSSI: " rssi " Ch: " ch " BSSID: " addr
 * so the parser accepts that documented shape; on current builds no such
 * line can arrive and the ESP half of a TOOL window honestly stays empty.
 *
 * TRUTH CONTRACT: a TOOL row is "a device advertising like attack
 * tooling" — never "an attacker". A Pwnagotchi name is a self-reported
 * label; an ESP-class beacon is a dev-board-class radio.
 */

#define MAX_TOOL_DEVS 8

#define ROOM_SWEEP_TOOL_KIND_ESP 1
#define ROOM_SWEEP_TOOL_KIND_PWN 2

/* Bounded table row. rssi is 0 when the source prints none (pwn). */
typedef struct {
    char name[24];
    char mac[18];
    int8_t rssi;
    uint8_t kind; /* 1=esp 2=pwn */
    uint32_t first_seen;
    uint32_t last_seen;
    uint16_t observations;
    bool valid;
} ToolDev;

/* One parsed tooling observation. */
typedef struct {
    char name[24];
    char mac[18];
    int8_t rssi; /* 0 = not printed by this source */
    uint8_t kind;
    bool valid;
} RoomSweepToolRecord;

/*
 * ESP lines use the documented historical shape (RSSI/Ch/BSSID keys). The
 * raw parser shares it by construction — routing is per active source, so
 * the two never see the same window.
 */
static inline bool room_sweep_tool_parse_esp_line(const char* line, RoomSweepToolRecord* out) {
    if(!out) return false;
    out->valid = false;
    if(!line) return false;
    line = room_sweep_uart_strip_prompt(line);
    if(line[0] == '\0' || line[0] == '#') return false;
    if(strstr(line, "ESSID") || strstr(line, "Device:") || strstr(line, "Requesting:") ||
       strstr(line, "Name:")) {
        return false;
    }
    if(!strstr(line, "BSSID")) return false;

    char mac[18] = {0};
    if(!room_sweep_marauder_copy_mac(line, mac)) return false;

    int rssi_val = 0;
    bool found_rssi = false;
    if(line[0] == '-' && line[1] >= '0' && line[1] <= '9') {
        char* end;
        long v = strtol(line, &end, 10);
        if(v >= -120 && v <= 0 && (*end == ' ' || *end == '\0')) {
            rssi_val = (int)v;
            found_rssi = true;
        }
    }
    if(!found_rssi && !room_sweep_marauder_int_after(line, "RSSI", &rssi_val)) {
        return false;
    }
    if(rssi_val > 0 || rssi_val < -120) return false;

    strncpy(out->name, mac, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    strncpy(out->mac, mac, sizeof(out->mac) - 1);
    out->mac[sizeof(out->mac) - 1] = '\0';
    out->rssi = (int8_t)rssi_val;
    out->kind = ROOM_SWEEP_TOOL_KIND_ESP;
    out->valid = true;
    return true;
}

/*
 * PWN lines: "Pwnagotchi Name: <name>" (v1.9.1) or "Name: <name>" (master).
 * The bare "Name:" form is accepted only inside the TOOL source window, so
 * BLE/WiFi "Name:"-bearing lines can never reach it.
 */
static inline bool room_sweep_tool_parse_pwn_line(const char* line, RoomSweepToolRecord* out) {
    if(!out) return false;
    out->valid = false;
    if(!line) return false;
    line = room_sweep_uart_strip_prompt(line);
    if(line[0] == '\0' || line[0] == '#') return false;
    if(strstr(line, "RSSI") || strstr(line, "BSSID") || strstr(line, "Device:") ||
       strstr(line, "ESSID")) {
        return false;
    }

    const char* key = strstr(line, "Pwnagotchi Name:");
    if(!key) key = strstr(line, "Name:");
    if(!key) return false;
    key = strchr(key, ':') + 1;
    while(*key == ' ') key++;
    const char* end = key;
    while(*end) end++;
    while(end > key && (end[-1] == ' ' || end[-1] == '\r')) end--;
    size_t len = (size_t)(end - key);
    if(len == 0) return false; /* "Not a Pwnagotchi frame." never matches: no Name: key */
    if(len >= sizeof(out->name)) len = sizeof(out->name) - 1;

    memcpy(out->name, key, len);
    out->name[len] = '\0';
    out->mac[0] = '\0';
    out->rssi = 0; /* the pinned prints carry no RSSI */
    out->kind = ROOM_SWEEP_TOOL_KIND_PWN;
    out->valid = true;
    return true;
}

/*
 * MAC-first upsert for esp rows; pwn rows (no MAC) merge by exact name.
 * ToolDev keeps only the latest dBm sample (rows without one keep rssi=0,
 * the "not printed" sentinel). Returns the row index, or -1 when full.
 * `now` is the caller's tick.
 */
static inline int room_sweep_tool_upsert(
    ToolDev* table,
    int max,
    const RoomSweepToolRecord* obs,
    uint32_t now) {
    if(!table || max <= 0 || !obs || !obs->valid) return -1;
    for(int i = 0; i < max; i++) {
        if(table[i].valid && table[i].kind == obs->kind &&
           room_sweep_wireless_identity_matches(
               table[i].mac, table[i].name, obs->mac, obs->name)) {
            if(obs->rssi < 0) table[i].rssi = obs->rssi;
            table[i].last_seen = now;
            if(table[i].observations < UINT16_MAX) table[i].observations++;
            return i;
        }
    }
    for(int i = 0; i < max; i++) {
        if(!table[i].valid) {
            memset(&table[i], 0, sizeof(table[i]));
            strncpy(table[i].name, obs->name, sizeof(table[i].name) - 1);
            strncpy(table[i].mac, obs->mac, sizeof(table[i].mac) - 1);
            table[i].rssi = obs->rssi;
            table[i].kind = obs->kind;
            table[i].first_seen = now;
            table[i].last_seen = now;
            table[i].observations = 1;
            table[i].valid = true; /* publish the completed row last */
            return i;
        }
    }
    return -1;
}

/*
 * Alternating-window planner for the TOOL source. Pure: pass the current
 * tick + window length in. Returns 0 = keep the current window, 1 = switch
 * to esp now, 2 = switch to pwn now (the caller sends stopscan first).
 * `current`: 0 = none yet, else ROOM_SWEEP_TOOL_KIND_*.
 */
static inline int room_sweep_tool_next_window(uint32_t now, uint32_t window_ms, uint8_t current) {
    if(window_ms == 0) return 0;
    int target = (int)((now / window_ms) % 2U) == 0 ? ROOM_SWEEP_TOOL_KIND_ESP :
                                                      ROOM_SWEEP_TOOL_KIND_PWN;
    if((uint8_t)target == current) return 0;
    return target;
}

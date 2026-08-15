#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h> /* strtol */
#include <string.h> /* strstr, strlen, strncpy, memcpy */

#include "room_sweep_scan.h" /* framing helpers: strip_prompt, find_ble_record, ble_record_end */

/*
 * Pure Marauder line parsing — Flipper-header-free so host tests compile with
 * plain cc. A line in, a parsed record out. No App*, no furi_get_tick(), no
 * mutex, no table state: the caller owns the bounded wireless table and the
 * update-or-insert decision (which reuses room_sweep_wireless_identity_matches).
 *
 * This is the host-testable core of the parser that used to live inline in
 * room_sweep.c. Keep every preserved invariant in specs/marauder-parser-2026-08-15.md.
 */

typedef struct {
    int8_t rssi;
    uint8_t channel;
    char ssid[33];
    char bssid[18];
    bool valid;
} RoomSweepWifiRecord;

typedef struct {
    int8_t rssi;
    char name[33];
    char mac[18];
    bool valid;
} RoomSweepBleRecord;

/* ---------------------------------------------------------------- */
/* tokenizers (private)                                             */
/* ---------------------------------------------------------------- */

/* Integer value after a key string. True if found. */
static inline bool room_sweep_marauder_int_after(const char* line, const char* key, int* out) {
    const char* p = strstr(line, key);
    if(!p) return false;
    p += strlen(key);
    while(*p == ' ' || *p == ':' || *p == '"' || *p == ',') p++;
    char* end;
    long val = strtol(p, &end, 10);
    if(end == p) return false;
    *out = (int)val;
    return true;
}

/* String value after key (up to space/comma/quote/end). */
static inline void room_sweep_marauder_str_after(
    const char* line,
    const char* key,
    char* out,
    size_t out_sz) {
    out[0] = '\0';
    const char* p = strstr(line, key);
    if(!p) return;
    p += strlen(key);
    while(*p == ' ' || *p == ':' || *p == '"') p++;
    size_t i = 0;
    while(*p && *p != '"' && *p != ',' && i < out_sz - 1) {
        if(*p == ' ' && i > 0) break; /* stop at first space after content */
        out[i++] = *p++;
    }
    out[i] = '\0';
}

/* ESSID may contain spaces (everything after "ESSID: " to end), trailing
 * " XX XX" capability bytes stripped. */
static inline void room_sweep_marauder_essid(const char* line, char* out, size_t out_sz) {
    out[0] = '\0';
    const char* p = strstr(line, "ESSID: ");
    if(!p) p = strstr(line, "ESSID:");
    if(!p) return;
    p += 6; /* skip "ESSID:" */
    while(*p == ' ') p++;
    const char* end = line + strlen(line);
    const char* trim = end;
    while(trim > p && *(trim - 1) == ' ') trim--;
    if(trim - p > 6 && *(trim - 6) == ' ' && *(trim - 3) == ' ') {
        trim -= 6;
    }
    size_t i = 0;
    while(p < trim && i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static inline bool room_sweep_marauder_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/* First 17-char MAC pattern XX:XX:XX:XX:XX:XX not adjacent to a hex digit. */
static inline bool room_sweep_marauder_copy_mac(const char* line, char* out) {
    size_t line_len = strlen(line);
    if(line_len < 17) return false;
    for(size_t offset = 0; offset + 17 <= line_len; offset++) {
        bool match = true;
        for(size_t i = 0; i < 17; i++) {
            if(i % 3 == 2) {
                if(line[offset + i] != ':') match = false;
            } else if(!room_sweep_marauder_is_hex_digit(line[offset + i])) {
                match = false;
            }
        }
        if(!match) continue;
        if(offset > 0 && room_sweep_marauder_is_hex_digit(line[offset - 1])) continue;
        if(offset + 17 < line_len && room_sweep_marauder_is_hex_digit(line[offset + 17])) continue;
        memcpy(out, line + offset, 17);
        out[17] = '\0';
        return true;
    }
    return false;
}

/* Device/name text after "Device:" or "Name:", trailing " MAC:"/" RSSI"/spaces/'"' stripped. */
static inline void room_sweep_marauder_device_name(const char* line, char* out, size_t out_sz) {
    const char* key = strstr(line, "Device:");
    if(!key) key = strstr(line, "Name:");
    if(!key) return;
    key = strchr(key, ':') + 1;
    while(*key == ' ' || *key == '"') key++;
    const char* end = strstr(key, " MAC:");
    if(!end) end = strstr(key, " RSSI");
    if(!end) end = line + strlen(line);
    while(end > key && (end[-1] == ' ' || end[-1] == '"')) end--;
    size_t len = (size_t)(end - key);
    if(len >= out_sz) len = out_sz - 1;
    memcpy(out, key, len);
    out[len] = '\0';
}

/* ---------------------------------------------------------------- */
/* public parse API                                                  */
/* ---------------------------------------------------------------- */

/* True iff `line` is a valid WiFi AP line; fills *out.
 * Formats seen on BFFB:
 *   "-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: Name 00 00"
 *   "> RSSI: -38 Ch: 5 BSSID: aa:… ESSID: Name" */
static inline bool room_sweep_marauder_parse_wifi(const char* line, RoomSweepWifiRecord* out) {
    if(!out) return false;
    out->valid = false;
    line = room_sweep_uart_strip_prompt(line);
    if(!line || line[0] == '\0' || line[0] == '#') return false;

    int rssi_val = 0;
    bool found_rssi = false;

    /* Primary: line starts with negative number (older Marauder format). */
    if(line[0] == '-' && line[1] >= '0' && line[1] <= '9') {
        char* end;
        long v = strtol(line, &end, 10);
        if(v >= -120 && v <= 0 && (*end == ' ' || *end == '\0')) {
            rssi_val = (int)v;
            found_rssi = true;
        }
    }
    /* BFFB live: "RSSI: -38 Ch: …". */
    if(!found_rssi) {
        if(!room_sweep_marauder_int_after(line, "RSSI", &rssi_val) &&
           !room_sweep_marauder_int_after(line, "rssi", &rssi_val)) {
            return false;
        }
    }
    if(rssi_val > 0 || rssi_val < -120) return false;

    /* Must have ESSID or Ch: to be a WiFi AP line (not a BLE "RSSI: Device:" line). */
    if(!strstr(line, "ESSID") && !strstr(line, "Ch:") && !strstr(line, "essid") &&
       !strstr(line, "BSSID")) {
        return false;
    }
    if(strstr(line, "Device:")) return false; /* BLE */

    char ssid[33] = {0};
    room_sweep_marauder_essid(line, ssid, sizeof(ssid));
    if(ssid[0] == '\0') {
        room_sweep_marauder_str_after(line, "ESSID", ssid, sizeof(ssid));
    }
    if(ssid[0] == '\0') {
        strncpy(ssid, "Hidden/unknown", sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
    }

    int ch_val = 0;
    room_sweep_marauder_int_after(line, "Ch:", &ch_val);
    if(ch_val == 0) room_sweep_marauder_int_after(line, "Channel", &ch_val);

    char bssid[18] = {0};
    room_sweep_marauder_copy_mac(line, bssid);

    out->rssi = (int8_t)rssi_val;
    out->channel = (uint8_t)ch_val;
    strncpy(out->ssid, ssid, sizeof(out->ssid) - 1);
    out->ssid[sizeof(out->ssid) - 1] = '\0';
    strncpy(out->bssid, bssid, sizeof(out->bssid) - 1);
    out->bssid[sizeof(out->bssid) - 1] = '\0';
    out->valid = true;
    return true;
}

/* True iff `line` is a valid single BLE observation record; fills *out.
 * Formats: "-60 Device: AirPods" and "RSSI: -37 Device: mac". */
static inline bool room_sweep_marauder_parse_ble_record(const char* line, RoomSweepBleRecord* out) {
    if(!out) return false;
    out->valid = false;
    line = room_sweep_uart_strip_prompt(line);
    if(!line || line[0] == '\0' || line[0] == '#') return false;
    /* Not WiFi. */
    if(strstr(line, "ESSID") || strstr(line, "BSSID") || strstr(line, "Ch:")) return false;

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
    if(!found_rssi) {
        if(!room_sweep_marauder_int_after(line, "RSSI", &rssi_val) &&
           !room_sweep_marauder_int_after(line, "rssi", &rssi_val)) {
            return false;
        }
    }
    if(rssi_val > 0 || rssi_val < -120) return false;

    char mac_early[18] = {0};
    room_sweep_marauder_copy_mac(line, mac_early);
    if(!strstr(line, "Device") && !strstr(line, "Name") && !strstr(line, "name") &&
       mac_early[0] == '\0') {
        return false;
    }

    char name[33] = {0};
    room_sweep_marauder_device_name(line, name, sizeof(name));
    if(name[0] == '\0') room_sweep_marauder_str_after(line, "Device", name, sizeof(name));
    if(name[0] == '\0') room_sweep_marauder_str_after(line, "Name", name, sizeof(name));
    /* Truncate name if a second RSSI token leaked in. */
    char* cut = strstr(name, " RSSI");
    if(cut) *cut = '\0';
    cut = strstr(name, "RSSI:");
    if(cut) *cut = '\0';
    if(name[0] == '\0' && mac_early[0]) {
        strncpy(name, mac_early, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    if(name[0] == '\0') {
        strncpy(name, "Hidden/unknown", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    char mac[18] = {0};
    room_sweep_marauder_str_after(line, "MAC", mac, sizeof(mac));
    if(mac[0] == '\0') room_sweep_marauder_copy_mac(line, mac);
    if(mac[0] == '\0' && name[0] != '\0') {
        char maybe[18] = {0};
        if(room_sweep_marauder_copy_mac(name, maybe)) {
            strncpy(mac, maybe, sizeof(mac) - 1);
            mac[sizeof(mac) - 1] = '\0';
        }
    }

    out->rssi = (int8_t)rssi_val;
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    strncpy(out->mac, mac, sizeof(out->mac) - 1);
    out->mac[sizeof(out->mac) - 1] = '\0';
    out->valid = true;
    return true;
}

/* Frame `line` (may hold several abutting records) and parse up to `max`.
 * Returns the number of records successfully parsed; writes them to out[0..count)
 * (bounded by max). Stops at '#'. Bounded by ROOM_SWEEP_UART_LINE_MAX (~18 max
 * abutting records on a 128-byte line), so a caller max of 8 is always sufficient
 * for real BFFB output. */
static inline uint8_t room_sweep_marauder_parse_ble(
    const char* line,
    RoomSweepBleRecord* out,
    uint8_t max) {
    if(!line || !out) return 0;
    line = room_sweep_uart_strip_prompt(line);
    if(!line[0] || line[0] == '#') return 0;

    uint8_t found = 0;
    const char* rec = room_sweep_uart_find_ble_record(line);
    while(rec) {
        const char* end = room_sweep_uart_ble_record_end(rec);
        char one[ROOM_SWEEP_UART_LINE_MAX];
        size_t len = (size_t)(end - rec);
        if(len >= sizeof(one)) len = sizeof(one) - 1U;
        memcpy(one, rec, len);
        one[len] = '\0';
        /* Trim trailing whitespace. */
        while(len > 0 && (one[len - 1] == ' ' || one[len - 1] == '\t')) {
            one[--len] = '\0';
        }
        if(found < max) {
            if(room_sweep_marauder_parse_ble_record(one, &out[found])) found++;
        } else {
            /* Count only what we can write; >max never occurs on real lines. */
            break;
        }
        if(*end == '\0' || *end == '#') break;
        rec = room_sweep_uart_find_ble_record(end);
    }
    return found;
}

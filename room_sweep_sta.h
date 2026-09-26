#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "room_sweep_scan.h"   /* prompt stripping */
#include "room_sweep_marauder.h" /* int_after / copy_mac tokenizer helpers */
#include "room_sweep_wireless.h" /* identity match semantics */
#include "room_sweep_stats.h"  /* evidence stats fold */

/*
 * Phase 4 — transmitter radar over Marauder `sniffraw`. Every received
 * 802.11 frame on the current channel prints one line; STATIONS included,
 * not just APs. Format confirmed by a redacted synthetic fixture derived from
 * the 2026-09-20 probe battery:
 *
 *   RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:01
 *
 * The "BSSID" label is the firmware's; the address is the TRANSMITTER of
 * the frame (AP or station). Header-only and Flipper-header-free so host
 * tests compile with plain cc.
 *
 * TRUTH CONTRACT: a raw row is "a transmitter heard at this RSSI", never
 * "a person's device". The label a station carries here is its link-layer
 * address only — no name, no owner, no intent.
 */

#define MAX_RAW_DEVS 12

/* One parsed sniffraw line. */
typedef struct {
    int8_t rssi;
    uint8_t channel;
    char mac[18]; /* transmitter address as printed (normalized on upsert) */
    bool valid;
} RoomSweepRawRecord;

/* Bounded table row. rssi_min/max/sum fold via room_sweep_stats_absorb. */
typedef struct {
    char mac[18];
    int8_t rssi;      /* latest */
    uint8_t channel;
    uint32_t first_seen; /* ticks, injected by caller */
    uint32_t last_seen;
    uint16_t observations;
    int8_t rssi_min;
    int8_t rssi_max;
    uint32_t rssi_sum;
    bool valid;
} RawDev;

/* True iff `line` is a sniffraw frame line; fills *out.
 * Accepts the live "> "-prefixed form and the bare form; rejects the other
 * sources' lines (ESSID / Device: / Requesting: belong to beacon, BLE and
 * probe parsers respectively) so a misrouted line can never pollute the
 * transmitter table. */
static inline bool room_sweep_raw_parse_line(const char* line, RoomSweepRawRecord* out) {
    if(!out) return false;
    out->valid = false;
    if(!line) return false;
    line = room_sweep_uart_strip_prompt(line);
    if(line[0] == '\0' || line[0] == '#') return false;
    if(strstr(line, "ESSID") || strstr(line, "Device:") || strstr(line, "Requesting:")) {
        return false;
    }
    /* The BSSID key + address is the shape's spine — a line without it is
     * not a frame line from this source. */
    if(!strstr(line, "BSSID")) return false;

    char mac[18] = {0};
    if(!room_sweep_marauder_copy_mac(line, mac)) return false;

    int rssi_val = 0;
    bool found_rssi = false;
    if(line[0] == '-' && line[1] >= '0' && line[1] <= '9') {
        /* Bare "-44 Ch: …" (defensive; not yet observed on this build). */
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

    int ch_val = 0;
    room_sweep_marauder_int_after(line, "Ch:", &ch_val);

    out->rssi = (int8_t)rssi_val;
    out->channel = (uint8_t)ch_val;
    memcpy(out->mac, mac, sizeof(out->mac));
    out->valid = true;
    return true;
}

/*
 * MAC-first upsert (same rules as the AP/BLE tables: an address never
 * merges into a different address). Returns the row index, or -1 when the
 * table is full. `now` is the caller's tick so this stays host-testable.
 */
static inline int room_sweep_raw_upsert(
    RawDev* table,
    int max,
    const RoomSweepRawRecord* obs,
    uint32_t now) {
    if(!table || max <= 0 || !obs || !obs->valid) return -1;
    for(int i = 0; i < max; i++) {
        if(table[i].valid &&
           room_sweep_wireless_identity_matches(
               table[i].mac, table[i].mac, obs->mac, NULL)) {
            /* Fold stats BEFORE the counter bump (pre-increment count). */
            room_sweep_stats_absorb(
                &table[i].rssi_min,
                &table[i].rssi_max,
                &table[i].rssi_sum,
                obs->rssi,
                table[i].observations);
            table[i].rssi = obs->rssi;
            table[i].channel = obs->channel;
            table[i].last_seen = now;
            if(table[i].observations < UINT16_MAX) table[i].observations++;
            return i;
        }
    }
    for(int i = 0; i < max; i++) {
        if(!table[i].valid) {
            memset(&table[i], 0, sizeof(table[i]));
            memcpy(table[i].mac, obs->mac, sizeof(table[i].mac));
            table[i].rssi = obs->rssi;
            table[i].channel = obs->channel;
            table[i].first_seen = now;
            table[i].last_seen = now;
            /* First observation initializes the evidence stats (obs=0). */
            room_sweep_stats_absorb(
                &table[i].rssi_min,
                &table[i].rssi_max,
                &table[i].rssi_sum,
                obs->rssi,
                0);
            table[i].observations = 1;
            table[i].valid = true; /* publish the completed row last */
            return i;
        }
    }
    return -1;
}

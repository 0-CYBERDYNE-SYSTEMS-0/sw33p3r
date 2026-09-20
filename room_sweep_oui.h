#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Curated OUI vendor lookup for MACs the app already captures (Wi-Fi BSSID,
 * BLE address). Header-only and Flipper-header-free so host tests compile
 * with plain cc. Integer math only: the project builds with
 * -Wdouble-promotion as an error.
 *
 * TRUTH CONTRACT: the table is CURATED, not exhaustive. Every entry was
 * verified against the IEEE registry (standards-oui.ieee.org/oui/oui.txt,
 * downloaded 2026-09-20); prefixes that could not be verified were left out.
 * An unlisted MAC prints "unlisted" — the caller never guesses a vendor.
 * A MAC is never an owner: the label names the registrant of the prefix,
 * nothing else.
 */

/* One flash-resident table row: 3-byte OUI + short display label. */
typedef struct {
    const char* prefix; /* exactly 6 uppercase hex chars, no separators */
    const char* label;  /* <= 12 chars */
} RoomSweepOuiEntry;

/* Fixed evidence tokens shared by the UI, CSV, and report wiring. */
#define ROOM_SWEEP_OUI_UNLISTED   "unlisted"
#define ROOM_SWEEP_OUI_RANDOMIZED "randomized"

/* Entries are grouped by vendor family; order carries no lookup meaning
 * (linear scan). Each label is the IEEE registrant, trimmed to <= 12 chars. */
static const RoomSweepOuiEntry room_sweep_oui_entries[] = {
    /* Apple */
    {"000393", "Apple"},
    {"F01898", "Apple"},
    {"A4D18C", "Apple"},
    /* Samsung */
    {"001632", "Samsung"},
    {"48BCE1", "Samsung"},
    /* Google */
    {"001A11", "Google"},
    {"F4F5E8", "Google"},
    /* Espressif — ESP32/8266 dev boards and Marauder-class hardware */
    {"240AC4", "Espressif"},
    {"5CCF7F", "Espressif"},
    {"8CAAB5", "Espressif"},
    /* DJI */
    {"60601F", "DJI"},
    /* Cameras */
    {"4419B6", "Hikvision"},
    {"C056E3", "Hikvision"},
    {"3CEF8C", "Dahua"},
    {"9002A9", "Dahua"},
    {"EC71DB", "Reolink"},
    {"FC9C98", "Arlo"},
    {"00408C", "Axis"},
    {"000918", "Hanwha"},
    /* Networking */
    {"50C7BF", "TP-Link"},
    {"A42BB0", "TP-Link"},
    {"9C3DCF", "Netgear"},
    {"A040A0", "Netgear"},
    {"24A43C", "Ubiquiti"},
    {"788A20", "Ubiquiti"},
    {"004096", "Cisco"},
    {"001839", "Cisco"},
    {"00055D", "D-Link"},
    {"EC1A59", "Belkin"},
    /* Compute / phones */
    {"286C07", "Xiaomi"},
    {"640980", "Xiaomi"},
    {"00464B", "Huawei"},
    {"346BD3", "Huawei"},
    {"64BC0C", "LG"},
    {"10C595", "Lenovo"},
    {"AC220B", "Asus"},
    {"001422", "Dell"},
    {"F8DB88", "Dell"},
    {"00155D", "Microsoft"},
    {"001DD8", "Microsoft"},
    {"001B21", "Intel"},
    {"002710", "Intel"},
    {"00E04C", "Realtek"},
    {"B827EB", "Raspberry"},
    {"DCA632", "Raspberry"},
    {"E45F01", "Raspberry"},
    /* Printers */
    {"3CD92B", "HP"},
    {"008077", "Brother"},
    {"000085", "Canon"},
    /* Media / smart home */
    {"74C246", "Amazon"},
    {"6837E9", "Amazon"},
    {"CC6DA0", "Roku"},
    {"7CF666", "Tuya"},
    {"1C90FF", "Tuya"},
    {"949F3E", "Sonos"},
    /* Other namesakes the room survey cares about */
    {"B4C26A", "Garmin"},
    {"38F0C8", "Logitech"},
    {"00014A", "Sony"},
    {"0013A9", "Sony"},
    {"245EBE", "QNAP"},
    {"001132", "Synology"},
};

#define ROOM_SWEEP_OUI_COUNT \
    (sizeof(room_sweep_oui_entries) / sizeof(room_sweep_oui_entries[0]))

/* Host-testable invariants: every prefix is 6 hex chars and every entry is
 * globally administered (bit 0x01 multicast and bit 0x02 locally-
 * administered both clear) — a locally-administered MAC can never match a
 * real vendor, so "randomized" always wins over a table hit. */
static inline bool room_sweep_oui_prefix_well_formed(const char* prefix) {
    if(!prefix) return false;
    for(size_t i = 0; i < 6; i++) {
        char c = prefix[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        if(!hex) return false;
    }
    return prefix[6] == '\0';
}

static inline uint8_t room_sweep_oui_first_octet(const char* prefix) {
    /* prefix is validated by room_sweep_oui_prefix_well_formed */
    uint8_t hi = (uint8_t)(prefix[0] <= '9' ? prefix[0] - '0' : prefix[0] - 'A' + 10);
    uint8_t lo = (uint8_t)(prefix[1] <= '9' ? prefix[1] - '0' : prefix[1] - 'A' + 10);
    return (uint8_t)((hi << 4) | lo);
}

/*
 * Strict canonical MAC shape "XX:XX:XX:XX:XX:XX" (any case, colon
 * separators only), all 17 characters required — the app only handles
 * fully formed MAC strings. Anything malformed — empty, short, wrong
 * separators, non-hex — is rejected.
 */
static inline bool room_sweep_oui_mac_valid(const char* mac) {
    if(!mac) return false;
    for(size_t i = 0; i < 17; i++) {
        char c = mac[i];
        if(i % 3 == 2) {
            if(c != ':') return false;
        } else {
            bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                       (c >= 'a' && c <= 'f');
            if(!hex) return false;
        }
    }
    return true;
}

/*
 * True when the MAC is locally administered (first octet & 0x02) — the app
 * displays these as "randomized". Malformed MAC strings are never
 * randomized: they return false.
 */
static inline bool room_sweep_oui_is_randomized(const char* mac) {
    if(!room_sweep_oui_mac_valid(mac)) return false;
    char hi = mac[0];
    char lo = mac[1];
    uint8_t octet = (uint8_t)
        ((((hi <= '9') ? hi - '0' : ((hi | 0x20) - 'a' + 10)) << 4) |
         ((lo <= '9') ? lo - '0' : ((lo | 0x20) - 'a' + 10)));
    return (octet & 0x02) != 0;
}

/*
 * Lookup "aa:bb:cc:dd:ee:ff" (any case, colon-separated — the app's
 * canonical format). Returns the vendor label or NULL when unlisted;
 * callers print "unlisted", never guess. Malformed/short strings -> NULL.
 */
static inline const char* room_sweep_oui_lookup(const char* mac) {
    if(!room_sweep_oui_mac_valid(mac)) return NULL;
    /* MAC nibble positions inside "XX:XX:XX..." (colons at 2 and 5). */
    static const size_t nibble[6] = {0, 1, 3, 4, 6, 7};
    for(size_t e = 0; e < ROOM_SWEEP_OUI_COUNT; e++) {
        const char* prefix = room_sweep_oui_entries[e].prefix;
        bool match = true;
        for(size_t i = 0; i < 6; i++) {
            char c = mac[nibble[i]];
            if(c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
            if(c != prefix[i]) {
                match = false;
                break;
            }
        }
        if(match) return room_sweep_oui_entries[e].label;
    }
    return NULL;
}

/*
 * Single source of truth for the evidence token shown on detail pages and
 * written to the session CSV: the curated label, "randomized" for locally
 * administered addresses, or "unlisted" for a well-formed MAC with no
 * table entry. NULL when there is no usable MAC (caller omits the token).
 * Randomized wins over any table hit by table hygiene (see above), but the
 * explicit check keeps that precedence true regardless of table contents.
 */
static inline const char* room_sweep_oui_evidence(const char* mac) {
    if(!room_sweep_oui_mac_valid(mac)) return NULL;
    if(room_sweep_oui_is_randomized(mac)) return ROOM_SWEEP_OUI_RANDOMIZED;
    const char* label = room_sweep_oui_lookup(mac);
    return label ? label : ROOM_SWEEP_OUI_UNLISTED;
}

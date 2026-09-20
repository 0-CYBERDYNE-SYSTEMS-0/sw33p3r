#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Name-pattern class hints for identifiers the app already receives (Wi-Fi
 * SSID, BLE advertised name). Header-only and Flipper-header-free so host
 * tests compile with plain cc. Integer logic only: the project builds with
 * -Wdouble-promotion as an error.
 *
 * TRUTH CONTRACT: a hint is a NAME-PATTERN GUESS, never identification.
 * Every displayed hint text carries a trailing "?" — the question mark is
 * part of the API because the result is a lead, not a verdict. Patterns are
 * lowercase substrings matched case-insensitively with a word-boundary
 * guard; anything ambiguous or uncertain was left out of the tables.
 */

typedef enum {
    ClassHintNone = 0,
    ClassHintCamera = 1 << 0, /* ssid|name ~ ipc|cam(era)|hichip|cctv|dvr|nvr */
    ClassHintPrinter = 1 << 1, /* hp|laserjet|epson|canon|brother|printer|DIRECT-xx-HP */
    ClassHintPhoneHotspot = 1 << 2, /* androidap|iphone|mifi|hotspot */
    ClassHintIot = 1 << 3, /* tuya|smart|plug|bulb|switch|sensor */
    ClassHintDrone = 1 << 4, /* dji|mavic|fpv|drone */
    ClassHintDevboard = 1 << 5, /* esp-|esp_|marauder|flipper|devboard */
    ClassHintTrackerBle = 1 << 6, /* ble name ~ tile|smarttag|trackr|airtag|duo tag */
} RoomSweepClassHints;

/* The literal placeholder the parser stores when no real name was heard.
 * A placeholder is not a name and must never produce a hint. */
#define ROOM_SWEEP_CLASSIFY_NO_NAME "Hidden/unknown"

/* ---------------------------------------------------------------- */
/* pattern tables (private, static const, lowercase)                */
/* ---------------------------------------------------------------- */

static const char* const room_sweep_classify_camera[] = {
    "camera", "ipcamera", "ipcam", "webcam", "cam", "cctv", "dvr",
    "nvr",    "hichip",   "onvif", "arlo",   "hikvision", "dahua", "reolink",
};

static const char* const room_sweep_classify_printer[] = {
    "printer", "laserjet", "deskjet", "officejet", "photosmart", "pixma",
    "epson",   "canon",    "brother", "hp",
};

static const char* const room_sweep_classify_hotspot[] = {
    "androidap", "iphone", "mifi", "hotspot",
};

static const char* const room_sweep_classify_iot[] = {
    "tuya", "smart", "plug", "bulb", "switch", "sensor",
};

static const char* const room_sweep_classify_drone[] = {
    "dji", "mavic", "fpv", "drone",
};

static const char* const room_sweep_classify_devboard[] = {
    "esp32", "esp8266", "esp-", "esp_", "marauder", "flipper", "devboard",
};

/* BLE-only: self-declared tracker names (find-my-class tags that advertise
 * a name at all — most AirTags do not, so absence of the hint is not
 * evidence of absence). */
static const char* const room_sweep_classify_tracker[] = {
    "tile", "smarttag", "trackr", "airtag", "duo tag",
};

/* ---------------------------------------------------------------- */
/* matcher                                                          */
/* ---------------------------------------------------------------- */

static inline char room_sweep_classify_fold(char c) {
    if(c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static inline bool room_sweep_classify_is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/*
 * Boundary guards, kept minimal and documented (heuristic, not NLP):
 *  - RIGHT: a match is rejected when the next character is a letter
 *    (case-insensitive) — this is the false-positive guard the contract
 *    demands: "cameron" never hints CAM, "cameraman" never matches
 *    "camera", "phpmyadmin" never matches "hp", "Smarties" and
 *    "HPOffice" never glue-match. Digit continuations stay allowed so
 *    real advertised names still classify: "IPCAM2", "FPV450", "Switch2".
 *  - LEFT: only patterns that end in a separator ("esp-", "esp_") require
 *    a non-alphanumeric character before them.
 */
static inline bool room_sweep_classify_match_at(
    const char* name,
    size_t offset,
    const char* pattern,
    size_t plen) {
    for(size_t i = 0; i < plen; i++) {
        if(room_sweep_classify_fold(name[offset + i]) != pattern[i]) return false;
    }
    if(offset > 0 && !room_sweep_classify_is_alnum(pattern[plen - 1])) {
        if(room_sweep_classify_is_alnum(name[offset - 1])) return false;
    }
    char after = room_sweep_classify_fold(name[offset + plen]);
    if(room_sweep_classify_is_alnum(pattern[plen - 1]) && after >= 'a' && after <= 'z') {
        return false;
    }
    return true;
}

static inline bool room_sweep_classify_contains(const char* name, const char* pattern) {
    size_t plen = 0;
    while(pattern[plen] != '\0') plen++;
    for(size_t offset = 0; name[offset] != '\0'; offset++) {
        if(room_sweep_classify_match_at(name, offset, pattern, plen)) return true;
    }
    return false;
}

static inline RoomSweepClassHints room_sweep_classify_table(
    const char* name,
    const char* const* patterns,
    size_t count,
    RoomSweepClassHints bit) {
    for(size_t i = 0; i < count; i++) {
        if(room_sweep_classify_contains(name, patterns[i])) return bit;
    }
    return ClassHintNone;
}

#define CLASSIFY_TABLE(name, table, bit) \
    room_sweep_classify_table( \
        name, table, sizeof(table) / sizeof(table[0]), bit)

/* ---------------------------------------------------------------- */
/* public API                                                       */
/* ---------------------------------------------------------------- */

/* Classify a Wi-Fi SSID. Never hints from the "Hidden/unknown" placeholder
 * (that string is the parser's no-name marker, not a name). */
static inline RoomSweepClassHints room_sweep_classify_ssid(const char* ssid) {
    if(!ssid || ssid[0] == '\0') return ClassHintNone;
    RoomSweepClassHints h = ClassHintNone;
    h |= CLASSIFY_TABLE(ssid, room_sweep_classify_camera, ClassHintCamera);
    h |= CLASSIFY_TABLE(ssid, room_sweep_classify_printer, ClassHintPrinter);
    h |= CLASSIFY_TABLE(ssid, room_sweep_classify_hotspot, ClassHintPhoneHotspot);
    h |= CLASSIFY_TABLE(ssid, room_sweep_classify_iot, ClassHintIot);
    h |= CLASSIFY_TABLE(ssid, room_sweep_classify_drone, ClassHintDrone);
    h |= CLASSIFY_TABLE(ssid, room_sweep_classify_devboard, ClassHintDevboard);
    return h;
}

/* Classify a BLE advertised name: the SSID tables plus tracker names. */
static inline RoomSweepClassHints room_sweep_classify_ble_name(const char* name) {
    if(!name || name[0] == '\0') return ClassHintNone;
    RoomSweepClassHints h = room_sweep_classify_ssid(name);
    h |= CLASSIFY_TABLE(name, room_sweep_classify_tracker, ClassHintTrackerBle);
    return h;
}

/*
 * First hint as fixed UI text WITH question mark (e.g. "CAM?") — the "?"
 * is part of the contract (heuristic, not identification). None -> "".
 */
static inline const char* room_sweep_classify_hint_text(RoomSweepClassHints h) {
    if(h & ClassHintCamera) return "CAM?";
    if(h & ClassHintPrinter) return "PRT?";
    if(h & ClassHintPhoneHotspot) return "HS?";
    if(h & ClassHintIot) return "IOT?";
    if(h & ClassHintDrone) return "DRN?";
    if(h & ClassHintDevboard) return "DEV?";
    if(h & ClassHintTrackerBle) return "TRK?";
    return "";
}

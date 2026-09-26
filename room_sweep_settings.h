#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Host-testable settings / wireless window helpers.
 * Keep independent of Flipper headers.
 */

/* Settings groups — Long L/R jumps groups; U/D moves within a group. */
typedef enum {
    RoomSweepSetGroupFeedback = 0,
    RoomSweepSetGroupWireless,
    RoomSweepSetGroupRadio,
    RoomSweepSetGroupGps,
    RoomSweepSetGroupSession,
    RoomSweepSetGroupCount,
} RoomSweepSetGroup;

/* Flat setting indices — keep order stable for host tests. */
enum {
    RoomSweepSetSound = 0,
    RoomSweepSetVibro,
    RoomSweepSetRescan,
    RoomSweepSetScanWin,
    RoomSweepSetWifiSrc,
    RoomSweepSetRecord,
    RoomSweepSetExtBand,
    RoomSweepSetSpiPath,
    RoomSweepSetGpsSrc,
    RoomSweepSetGpsLog,
    RoomSweepSetBaseline,
    RoomSweepSetDump,
    RoomSweepSetTxDur,
    RoomSweepSetFullSweep,
    /* Phase 9: opt-in cross-session watchlist. OFF by construction on every
     * launch (the app keeps no settings file), so "never persisted ON" is
     * guaranteed the same way as the Wi capture source default. */
    RoomSweepSetWatchlist,
    RoomSweepSetCount,
};

/*
 * Wi capture source (Settings → Wi Src, Phase 4/5/10 of the 2026-09-20
 * expansion). BEACON is the factory default and the only value ever
 * persisted implicitly — the app keeps no settings file, so a default
 * WiSourceBeacon on every launch is guaranteed by construction.
 */
typedef enum {
    WiSourceBeacon = 0, /* sniffbeacon — AP beacons (existing behavior) */
    WiSourceRaw,        /* sniffraw — every 802.11 transmitter on channel */
    WiSourceProbe,      /* sniffprobe — client probe requests */
    WiSourceTool,       /* sniffesp + sniffpwn, alternating windows */
    WiSourceCount,
} WiSource;

static inline WiSource room_sweep_wi_source_step(WiSource source, bool down) {
    if((uint8_t)source >= WiSourceCount) source = WiSourceBeacon;
    if(down) {
        return (WiSource)(((uint8_t)source + 1U) % WiSourceCount);
    }
    return (WiSource)(((uint8_t)source + WiSourceCount - 1U) % WiSourceCount);
}

/* Settings value text ("BEACON" / "RAW" / "PROBE" / "TOOL"). */
static inline const char* room_sweep_wi_source_label(WiSource source) {
    switch(source) {
    case WiSourceRaw:
        return "RAW";
    case WiSourceProbe:
        return "PROBE";
    case WiSourceTool:
        return "TOOL";
    case WiSourceBeacon:
    default:
        return "BEACON";
    }
}

/* Wi-tab header tag: BEACON keeps the historical "SCAN" wording. */
static inline const char* room_sweep_wi_source_tag(WiSource source) {
    switch(source) {
    case WiSourceRaw:
        return "RAW";
    case WiSourceProbe:
        return "PROBE";
    case WiSourceTool:
        return "TOOL";
    case WiSourceBeacon:
    default:
        return "SCAN";
    }
}

static inline RoomSweepSetGroup room_sweep_set_group_of(uint8_t setting) {
    switch(setting) {
    case RoomSweepSetSound:
    case RoomSweepSetVibro:
        return RoomSweepSetGroupFeedback;
    case RoomSweepSetRescan:
    case RoomSweepSetScanWin:
    case RoomSweepSetWifiSrc:
        return RoomSweepSetGroupWireless;
    case RoomSweepSetExtBand:
    case RoomSweepSetSpiPath:
    case RoomSweepSetTxDur:
        return RoomSweepSetGroupRadio;
    case RoomSweepSetGpsSrc:
    case RoomSweepSetGpsLog:
        return RoomSweepSetGroupGps;
    case RoomSweepSetRecord:
    case RoomSweepSetBaseline:
    case RoomSweepSetDump:
    case RoomSweepSetFullSweep:
    case RoomSweepSetWatchlist:
        return RoomSweepSetGroupSession;
    default:
        return RoomSweepSetGroupFeedback;
    }
}

static inline const char* room_sweep_set_group_label(RoomSweepSetGroup group) {
    switch(group) {
    case RoomSweepSetGroupFeedback:
        return "Feedback";
    case RoomSweepSetGroupWireless:
        return "Wireless";
    case RoomSweepSetGroupRadio:
        return "Radio";
    case RoomSweepSetGroupGps:
        return "GPS";
    case RoomSweepSetGroupSession:
        return "Session";
    default:
        return "Settings";
    }
}

/* First setting index in a group. */
static inline uint8_t room_sweep_set_group_first(RoomSweepSetGroup group) {
    for(uint8_t i = 0; i < RoomSweepSetCount; i++) {
        if(room_sweep_set_group_of(i) == group) return i;
    }
    return 0;
}

/* Step selection within the current group (wrap). */
static inline uint8_t room_sweep_set_cursor_step(uint8_t sel, bool down) {
    if(sel >= RoomSweepSetCount) sel = 0;
    RoomSweepSetGroup group = room_sweep_set_group_of(sel);
    if(down) {
        for(uint8_t i = 1; i <= RoomSweepSetCount; i++) {
            uint8_t next = (uint8_t)((sel + i) % RoomSweepSetCount);
            if(room_sweep_set_group_of(next) == group) return next;
        }
    } else {
        for(uint8_t i = 1; i <= RoomSweepSetCount; i++) {
            uint8_t prev = (uint8_t)((sel + RoomSweepSetCount - i) % RoomSweepSetCount);
            if(room_sweep_set_group_of(prev) == group) return prev;
        }
    }
    return sel;
}

/* Long L/R group change; land on first item of the new group. */
static inline uint8_t room_sweep_set_group_step(uint8_t sel, bool next_group) {
    if(sel >= RoomSweepSetCount) sel = 0;
    RoomSweepSetGroup group = room_sweep_set_group_of(sel);
    if(next_group) {
        group = (RoomSweepSetGroup)((group + 1) % RoomSweepSetGroupCount);
    } else {
        group = (RoomSweepSetGroup)((group + RoomSweepSetGroupCount - 1) %
                                   RoomSweepSetGroupCount);
    }
    return room_sweep_set_group_first(group);
}

/* Wi-Fi/BLE scan window presets (ms). */
#define ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT 3

static inline uint32_t room_sweep_scan_timeout_ms(uint8_t idx) {
    static const uint32_t presets[ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT] = {
        15000U,
        30000U,
        60000U,
    };
    if(idx >= ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT) idx = 1;
    return presets[idx];
}

static inline uint8_t room_sweep_scan_timeout_step(uint8_t idx, bool longer) {
    if(idx >= ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT) idx = 1;
    if(longer) {
        return (uint8_t)((idx + 1U) % ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT);
    }
    return (uint8_t)((idx + ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT - 1U) %
                     ROOM_SWEEP_SCAN_TIMEOUT_PRESET_COUNT);
}

static inline uint8_t room_sweep_scan_timeout_seconds(uint8_t idx) {
    return (uint8_t)(room_sweep_scan_timeout_ms(idx) / 1000U);
}

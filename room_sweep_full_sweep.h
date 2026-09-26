#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Host-testable full-room-sweep phase machine.
 * Detect-only order: RF → Wi-Fi → BLE → Wi-raw → Wi-probe → nRF24 → GPS → done.
 * The Wi-raw pass (Phase 4, 2026-09-20 expansion) runs a short sniffraw
 * transmitter-radar window after BLE — that pass IS the client/station view
 * on this firmware, since sniffraw reports one line per 802.11 frame with
 * stations included. The Wi-probe pass (Phase 11) runs a sniffprobe window
 * for hidden-SSID recovery. A dedicated scanap+scansta pass stays out until
 * the station-line format is pinned live (format gate, docs/BFFB_MOMENTUM.md).
 * Both passes are skipped honestly (bit unset) when no UART scanner exists.
 * Never arms TX. Every phase has a hard wall-clock timeout.
 */

typedef enum {
    RoomSweepFullIdle = 0,
    RoomSweepFullRf,
    RoomSweepFullWifi,
    RoomSweepFullBle,
    RoomSweepFullRaw,
    RoomSweepFullProbe,
    RoomSweepFullNrf24,
    RoomSweepFullGps,
    RoomSweepFullDone,
    RoomSweepFullAborted,
} RoomSweepFullPhase;

typedef struct {
    RoomSweepFullPhase phase;
    uint8_t phases_completed; /* bitmask of finished sensor steps (not idle/done/abort) */
    bool active;
} RoomSweepFullSweepState;

#define ROOM_SWEEP_FULL_BIT_RF (1U << 0)
#define ROOM_SWEEP_FULL_BIT_WIFI (1U << 1)
#define ROOM_SWEEP_FULL_BIT_BLE (1U << 2)
#define ROOM_SWEEP_FULL_BIT_NRF24 (1U << 3)
#define ROOM_SWEEP_FULL_BIT_GPS (1U << 4)
#define ROOM_SWEEP_FULL_BIT_RAW (1U << 5)
#define ROOM_SWEEP_FULL_BIT_PROBE (1U << 6)
#define ROOM_SWEEP_FULL_BIT_ALL \
    (ROOM_SWEEP_FULL_BIT_RF | ROOM_SWEEP_FULL_BIT_WIFI | ROOM_SWEEP_FULL_BIT_BLE | \
     ROOM_SWEEP_FULL_BIT_RAW | ROOM_SWEEP_FULL_BIT_PROBE | ROOM_SWEEP_FULL_BIT_NRF24 | \
     ROOM_SWEEP_FULL_BIT_GPS)

/* Hard per-phase ceilings (ms). GPS always ends by this; no infinite wait. */
#define ROOM_SWEEP_FULL_RF_MS 10000U
#define ROOM_SWEEP_FULL_WIFI_MS 15000U
#define ROOM_SWEEP_FULL_BLE_MS 15000U
#define ROOM_SWEEP_FULL_RAW_MS 6000U
#define ROOM_SWEEP_FULL_PROBE_MS 8000U /* probes are bursty: longer than the raw pass */
#define ROOM_SWEEP_FULL_NRF24_MS 12000U
#define ROOM_SWEEP_FULL_GPS_MS 8000U
#define ROOM_SWEEP_FULL_GPS_MIN_MS 2000U /* earliest exit if a fix/sentence exists */

static inline void room_sweep_full_sweep_init(RoomSweepFullSweepState* s) {
    if(!s) return;
    s->phase = RoomSweepFullIdle;
    s->phases_completed = 0;
    s->active = false;
}

static inline bool room_sweep_full_sweep_start(RoomSweepFullSweepState* s) {
    if(!s) return false;
    s->phase = RoomSweepFullRf;
    s->phases_completed = 0;
    s->active = true;
    return true;
}

static inline uint8_t room_sweep_full_sweep_bit_for_phase(RoomSweepFullPhase phase) {
    switch(phase) {
    case RoomSweepFullRf:
        return ROOM_SWEEP_FULL_BIT_RF;
    case RoomSweepFullWifi:
        return ROOM_SWEEP_FULL_BIT_WIFI;
    case RoomSweepFullBle:
        return ROOM_SWEEP_FULL_BIT_BLE;
    case RoomSweepFullRaw:
        return ROOM_SWEEP_FULL_BIT_RAW;
    case RoomSweepFullProbe:
        return ROOM_SWEEP_FULL_BIT_PROBE;
    case RoomSweepFullNrf24:
        return ROOM_SWEEP_FULL_BIT_NRF24;
    case RoomSweepFullGps:
        return ROOM_SWEEP_FULL_BIT_GPS;
    default:
        return 0;
    }
}

static inline uint32_t room_sweep_full_sweep_phase_limit_ms(RoomSweepFullPhase phase) {
    switch(phase) {
    case RoomSweepFullRf:
        return ROOM_SWEEP_FULL_RF_MS;
    case RoomSweepFullWifi:
        return ROOM_SWEEP_FULL_WIFI_MS;
    case RoomSweepFullBle:
        return ROOM_SWEEP_FULL_BLE_MS;
    case RoomSweepFullRaw:
        return ROOM_SWEEP_FULL_RAW_MS;
    case RoomSweepFullProbe:
        return ROOM_SWEEP_FULL_PROBE_MS;
    case RoomSweepFullNrf24:
        return ROOM_SWEEP_FULL_NRF24_MS;
    case RoomSweepFullGps:
        return ROOM_SWEEP_FULL_GPS_MS;
    default:
        return ROOM_SWEEP_FULL_GPS_MS;
    }
}

/* Hard ceiling: always true when elapsed hits the phase limit. */
static inline bool room_sweep_full_sweep_hard_timeout(
    RoomSweepFullPhase phase,
    uint32_t elapsed_ms) {
    return elapsed_ms >= room_sweep_full_sweep_phase_limit_ms(phase);
}

/*
 * GPS may finish early after min dwell when real NMEA data exists.
 * has_pos = usable parsed coordinates in the latest nav data (nmea.h);
 * a receiver "fix" alone is NOT accepted — a fix without parsed position is
 * the NO POS state and must not read as GPS success. has_sentences counts
 * as data (the phase produced evidence) but never as a position claim.
 * Nothing here is rendered as a position; this gate only sequences phases
 * and sets the phase-completed bit. Always forced done at hard timeout.
 */
static inline bool room_sweep_full_sweep_gps_ready(
    uint32_t elapsed_ms,
    bool has_pos,
    bool has_sentences) {
    if(room_sweep_full_sweep_hard_timeout(RoomSweepFullGps, elapsed_ms)) return true;
    if(elapsed_ms >= ROOM_SWEEP_FULL_GPS_MIN_MS && (has_pos || has_sentences)) return true;
    return false;
}

static inline RoomSweepFullPhase room_sweep_full_sweep_next_after(RoomSweepFullPhase phase) {
    switch(phase) {
    case RoomSweepFullRf:
        return RoomSweepFullWifi;
    case RoomSweepFullWifi:
        return RoomSweepFullBle;
    case RoomSweepFullBle:
        return RoomSweepFullRaw;
    case RoomSweepFullRaw:
        return RoomSweepFullProbe;
    case RoomSweepFullProbe:
        return RoomSweepFullNrf24;
    case RoomSweepFullNrf24:
        return RoomSweepFullGps;
    case RoomSweepFullGps:
        return RoomSweepFullDone;
    default:
        return RoomSweepFullDone;
    }
}

/* Mark current phase complete and advance. ok=false still advances (honest skip). */
static inline bool room_sweep_full_sweep_advance(RoomSweepFullSweepState* s, bool ok) {
    if(!s || !s->active) return false;
    if(s->phase == RoomSweepFullIdle || s->phase == RoomSweepFullDone ||
       s->phase == RoomSweepFullAborted)
        return false;
    if(ok) s->phases_completed |= room_sweep_full_sweep_bit_for_phase(s->phase);
    s->phase = room_sweep_full_sweep_next_after(s->phase);
    if(s->phase == RoomSweepFullDone) s->active = false;
    return true;
}

static inline bool room_sweep_full_sweep_abort(RoomSweepFullSweepState* s) {
    if(!s || !s->active) return false;
    s->phase = RoomSweepFullAborted;
    s->active = false;
    return true;
}

static inline bool room_sweep_full_sweep_is_running(const RoomSweepFullSweepState* s) {
    return s && s->active && s->phase != RoomSweepFullDone && s->phase != RoomSweepFullAborted &&
           s->phase != RoomSweepFullIdle;
}

static inline bool room_sweep_full_sweep_completed_all(const RoomSweepFullSweepState* s) {
    return s && s->phase == RoomSweepFullDone &&
           (s->phases_completed & ROOM_SWEEP_FULL_BIT_ALL) == ROOM_SWEEP_FULL_BIT_ALL;
}

static inline const char* room_sweep_full_sweep_phase_label(RoomSweepFullPhase phase) {
    switch(phase) {
    case RoomSweepFullIdle:
        return "idle";
    case RoomSweepFullRf:
        return "RF";
    case RoomSweepFullWifi:
        return "Wi-Fi";
    case RoomSweepFullBle:
        return "BLE";
    case RoomSweepFullRaw:
        return "Wi-raw";
    case RoomSweepFullProbe:
        return "Probe";
    case RoomSweepFullNrf24:
        return "nRF24";
    case RoomSweepFullGps:
        return "GPS";
    case RoomSweepFullDone:
        return "done";
    case RoomSweepFullAborted:
        return "aborted";
    default:
        return "?";
    }
}

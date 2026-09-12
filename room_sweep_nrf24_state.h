#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "room_sweep_analyzer.h"

/*
 * Host-testable nRF24 channel-activity survey state (detect / RPD only).
 * Channel count matches nRF24L01+ RF_CH range 0..125.
 *
 * TRUTH CONTRACT (sigint audit 2026-09-11):
 * This mode is 2.4 GHz ENERGY DETECTION, not nRF24 protocol detection.
 * The hardware bit read is the nRF24L01+ RPD (register 0x09 bit 0), which
 * per the Nordic nRF24L01+ product spec simply reports "received power
 * above -64 dBm in the current RF channel" — a 1-bit energy snapshot that
 * fires on ANY emitter (WiFi, BLE, another nRF24, microwave leakage), not
 * on nRF24 packets. No packet, address, or device identity is ever read.
 *
 * Packet-based identification is out of scope by design: decoding a packet
 * requires knowing the 40-bit address a priori, and passive discovery of
 * unknown addresses requires attack-style techniques (mousejack-class
 * sniffing) that MISSION.md/PROMPT.md explicitly ban. So hits are labelled
 * as channel ENERGY/hits everywhere, never as devices or dBm.
 */

#define ROOM_SWEEP_NRF24_CHANNELS 126U
#define ROOM_SWEEP_NRF24_TOP_N 5U

/*
 * Recent-activity integrator for live feedback (Geiger/vibro/LED on the nR
 * tab). Sustained RPD traffic at the ~500 samples/s cadence saturates the
 * score in well under a second; idle time decays it lazily so the feedback
 * peak falls back to silence instead of sticking. Integer math only.
 */
#define ROOM_SWEEP_NRF24_ACTIVITY_MAX 1000U
#define ROOM_SWEEP_NRF24_ACTIVITY_STEP 8U

typedef enum {
    RoomSweepNrf24Idle = 0,
    RoomSweepNrf24Scanning,
    RoomSweepNrf24Done,
    RoomSweepNrf24Error,
} RoomSweepNrf24Phase;

typedef struct {
    RoomSweepNrf24Phase phase;
    uint8_t channel; /* next channel to sample while scanning */
    uint8_t hits[ROOM_SWEEP_NRF24_CHANNELS]; /* RPD hit counts (saturated) */
    uint32_t total_hits;
    uint16_t active_channels; /* channels with hits > 0 */
    uint8_t top_channels[ROOM_SWEEP_NRF24_TOP_N];
    uint8_t top_hits[ROOM_SWEEP_NRF24_TOP_N];
    bool module_present;
    bool present_checked;
    /* Recent RPD activity 0..ROOM_SWEEP_NRF24_ACTIVITY_MAX (feedback peak). */
    uint16_t activity_score;
    uint32_t activity_last_tick; /* ms stamp of last decay accounting */
} RoomSweepNrf24State;

static inline void room_sweep_nrf24_init(RoomSweepNrf24State* s) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->phase = RoomSweepNrf24Idle;
    s->activity_score = 0;
    s->activity_last_tick = 0;
}

static inline void room_sweep_nrf24_clear_hits(RoomSweepNrf24State* s) {
    if(!s) return;
    memset(s->hits, 0, sizeof(s->hits));
    memset(s->top_channels, 0, sizeof(s->top_channels));
    memset(s->top_hits, 0, sizeof(s->top_hits));
    s->total_hits = 0;
    s->active_channels = 0;
    s->channel = 0;
    s->activity_score = 0;
    s->activity_last_tick = 0;
}

static inline bool room_sweep_nrf24_start(RoomSweepNrf24State* s) {
    if(!s) return false;
    room_sweep_nrf24_clear_hits(s);
    s->phase = RoomSweepNrf24Scanning;
    return true;
}

static inline void room_sweep_nrf24_set_present(RoomSweepNrf24State* s, bool present) {
    if(!s) return;
    s->module_present = present;
    s->present_checked = true;
    if(!present && s->phase == RoomSweepNrf24Scanning) s->phase = RoomSweepNrf24Error;
}

static inline void room_sweep_nrf24_recompute_top(RoomSweepNrf24State* s) {
    if(!s) return;
    memset(s->top_channels, 0, sizeof(s->top_channels));
    memset(s->top_hits, 0, sizeof(s->top_hits));
    s->active_channels = 0;
    s->total_hits = 0;
    for(uint16_t ch = 0; ch < ROOM_SWEEP_NRF24_CHANNELS; ch++) {
        uint8_t h = s->hits[ch];
        if(h == 0) continue;
        s->active_channels++;
        s->total_hits += h;
        for(uint8_t slot = 0; slot < ROOM_SWEEP_NRF24_TOP_N; slot++) {
            if(h > s->top_hits[slot]) {
                for(uint8_t m = ROOM_SWEEP_NRF24_TOP_N - 1; m > slot; m--) {
                    s->top_hits[m] = s->top_hits[m - 1];
                    s->top_channels[m] = s->top_channels[m - 1];
                }
                s->top_hits[slot] = h;
                s->top_channels[slot] = (uint8_t)ch;
                break;
            }
        }
    }
}

/* Record one RPD sample for channel. active=true means energy above detector. */
static inline bool room_sweep_nrf24_note_sample(
    RoomSweepNrf24State* s,
    uint8_t channel,
    bool active) {
    if(!s || s->phase != RoomSweepNrf24Scanning) return false;
    if(channel >= ROOM_SWEEP_NRF24_CHANNELS) return false;
    if(active) {
        if(s->hits[channel] < 255U) s->hits[channel]++;
        uint32_t raised = (uint32_t)s->activity_score + ROOM_SWEEP_NRF24_ACTIVITY_STEP;
        if(raised > ROOM_SWEEP_NRF24_ACTIVITY_MAX) raised = ROOM_SWEEP_NRF24_ACTIVITY_MAX;
        s->activity_score = (uint16_t)raised;
    }
    return true;
}

/*
 * Lazy decay of the activity integrator; call from the feedback path.
 * Per full 100 ms elapsed the score drops by score/10 + 2 (integer), so a
 * saturated score falls to silence in roughly 4 s of idle. The loop is
 * bounded (anything older than the cap simply goes to 0) and the stamp only
 * advances by consumed time, so sub-100 ms calls are exact no-ops. Elapsed
 * uses unsigned subtraction, matching the codebase's tick-wrap handling.
 */
static inline void room_sweep_nrf24_activity_tick(RoomSweepNrf24State* s, uint32_t now_ms) {
    if(!s) return;
    uint32_t elapsed = now_ms - s->activity_last_tick;
    uint32_t steps = elapsed / 100U;
    if(steps == 0U) return;
    if(steps > 40U) {
        /* Older than the bounded decay window: silence, consume everything. */
        s->activity_score = 0;
        s->activity_last_tick = now_ms;
        return;
    }
    for(uint32_t i = 0; i < steps; i++) {
        uint32_t decay = (uint32_t)(s->activity_score / 10U) + 2U;
        if(decay > (uint32_t)s->activity_score) decay = s->activity_score;
        s->activity_score = (uint16_t)(s->activity_score - decay);
    }
    s->activity_last_tick += steps * 100U;
}

/*
 * Map the current activity score into a shared meter level (int dBm-scale
 * units) used ONLY to pace feedback (sound/vibro/LED curves take a dBm-like
 * input) and to position shared analyzer bars. The result is a synthetic
 * meter position (0 -> -110, hot -> ~-30), NOT a measured RSSI: the RPD is
 * a 1-bit threshold detector and produces no signal-strength reading.
 * Screens must never print this value with "dBm" — use
 * room_sweep_nrf24_activity_units_label() instead.
 */
static inline int room_sweep_nrf24_activity_to_rssi(const RoomSweepNrf24State* s) {
    if(!s) return -127;
    uint16_t scaled = (uint16_t)(s->activity_score / 4U);
    if(scaled > 255U) scaled = 255U;
    return room_sweep_analyzer_activity_to_rssi((uint8_t)scaled);
}

/*
 * Units label for every numeric nRF24 readout on screen: the value is
 * ACTIVITY on the shared meter scale (arbitrary units), never measured dBm.
 * Host-tested so the label cannot silently drift back to "dBm".
 */
static inline const char* room_sweep_nrf24_activity_units_label(void) {
    return "ACT";
}

/* Advance scan cursor; returns true when a full pass finished. */
static inline bool room_sweep_nrf24_step_channel(RoomSweepNrf24State* s) {
    if(!s || s->phase != RoomSweepNrf24Scanning) return false;
    if(s->channel + 1U >= ROOM_SWEEP_NRF24_CHANNELS) {
        room_sweep_nrf24_recompute_top(s);
        s->phase = RoomSweepNrf24Done;
        s->channel = 0;
        return true;
    }
    s->channel = (uint8_t)(s->channel + 1U);
    return false;
}

static inline void room_sweep_nrf24_stop(RoomSweepNrf24State* s) {
    if(!s) return;
    if(s->phase == RoomSweepNrf24Scanning) {
        room_sweep_nrf24_recompute_top(s);
        s->phase = RoomSweepNrf24Done;
    }
}

static inline const char* room_sweep_nrf24_phase_label(RoomSweepNrf24Phase phase) {
    switch(phase) {
    case RoomSweepNrf24Scanning:
        return "scan";
    case RoomSweepNrf24Done:
        return "done";
    case RoomSweepNrf24Error:
        return "err";
    case RoomSweepNrf24Idle:
    default:
        return "idle";
    }
}

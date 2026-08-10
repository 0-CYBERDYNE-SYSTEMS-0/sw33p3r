#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Host-testable nRF24 channel-activity survey state (detect / RPD only).
 * Channel count matches nRF24L01+ RF_CH range 0..125.
 */

#define ROOM_SWEEP_NRF24_CHANNELS 126U
#define ROOM_SWEEP_NRF24_TOP_N 5U

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
} RoomSweepNrf24State;

static inline void room_sweep_nrf24_init(RoomSweepNrf24State* s) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->phase = RoomSweepNrf24Idle;
}

static inline void room_sweep_nrf24_clear_hits(RoomSweepNrf24State* s) {
    if(!s) return;
    memset(s->hits, 0, sizeof(s->hits));
    memset(s->top_channels, 0, sizeof(s->top_channels));
    memset(s->top_hits, 0, sizeof(s->top_hits));
    s->total_hits = 0;
    s->active_channels = 0;
    s->channel = 0;
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
    }
    return true;
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

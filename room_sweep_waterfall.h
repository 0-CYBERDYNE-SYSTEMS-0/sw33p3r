#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Host-testable waterfall history for scanner tabs.
 * Holds up to COLS snapshots of CHANNELS dBm samples, newest-first,
 * with a per-channel peak-hold.  Flipper-header-free for host tests.
 */

#define ROOM_SWEEP_WATERFALL_COLS 24
#define ROOM_SWEEP_WATERFALL_CHANNELS 16

typedef struct {
    int8_t col[ROOM_SWEEP_WATERFALL_COLS][ROOM_SWEEP_WATERFALL_CHANNELS];
    uint8_t newest; /* column index holding the newest snapshot */
    uint8_t count;  /* how many snapshots have been pushed (≤ COLS) */
    int8_t peak[ROOM_SWEEP_WATERFALL_CHANNELS]; /* peak-hold per channel */
} RoomSweepWaterfallState;

static inline void room_sweep_waterfall_init(RoomSweepWaterfallState* s) {
    if(!s) return;
    for(uint8_t i = 0; i < ROOM_SWEEP_WATERFALL_COLS; i++) {
        for(uint8_t ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) {
            s->col[i][ch] = -127;
        }
    }
    s->newest = 0;
    s->count = 0;
    for(uint8_t ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) s->peak[ch] = -127;
}

static inline void room_sweep_waterfall_push(
    RoomSweepWaterfallState* s,
    const int8_t* snapshot16) {
    if(!s) return;
    s->newest = (uint8_t)((s->newest + 1U) % ROOM_SWEEP_WATERFALL_COLS);
    if(s->count < ROOM_SWEEP_WATERFALL_COLS) s->count++;
    for(uint8_t ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) {
        int8_t sample = -127;
        if(snapshot16) {
            sample = snapshot16[ch];
            if(sample < -127) sample = -127;
            if(sample > 0) sample = 0;
        }
        s->col[s->newest][ch] = sample;
        if(sample > -127 && sample > s->peak[ch]) s->peak[ch] = sample;
    }
}

/* age 0 = newest column, age 1 = previous, ... age COLS-1 = oldest. */
static inline const int8_t* room_sweep_waterfall_col_at(
    const RoomSweepWaterfallState* s,
    uint8_t age) {
    if(!s || age >= s->count) return NULL;
    uint8_t idx = (uint8_t)(
        (s->newest + ROOM_SWEEP_WATERFALL_COLS - age) % ROOM_SWEEP_WATERFALL_COLS);
    return s->col[idx];
}

static inline int8_t room_sweep_waterfall_channel_at(
    const RoomSweepWaterfallState* s,
    uint8_t age,
    uint8_t ch) {
    if(ch >= ROOM_SWEEP_WATERFALL_CHANNELS) return -127;
    const int8_t* c = room_sweep_waterfall_col_at(s, age);
    if(!c) return -127;
    return c[ch];
}

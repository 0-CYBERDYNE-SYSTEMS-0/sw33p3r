#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Host-testable proximity / analyzer meter for scanner tabs.
 * Maps RSSI (or activity) into bar height + closer/farther trend.
 * Flipper-header-free for host tests.
 */

#define ROOM_SWEEP_ANALYZER_HISTORY 48U
#define ROOM_SWEEP_ANALYZER_FLOOR_DBM (-110)
#define ROOM_SWEEP_ANALYZER_CEIL_DBM (-30)

typedef enum {
    RoomSweepAnalyzerTrendStable = 0,
    RoomSweepAnalyzerTrendCloser,
    RoomSweepAnalyzerTrendFarther,
} RoomSweepAnalyzerTrend;

typedef struct {
    int8_t history[ROOM_SWEEP_ANALYZER_HISTORY];
    uint8_t hist_head; /* next write index */
    uint8_t hist_count;
    int8_t live_rssi;
    int8_t peak_rssi;
    int8_t trough_rssi;
    RoomSweepAnalyzerTrend trend;
    bool has_signal;
} RoomSweepAnalyzerState;

static inline void room_sweep_analyzer_init(RoomSweepAnalyzerState* s) {
    if(!s) return;
    for(uint8_t i = 0; i < ROOM_SWEEP_ANALYZER_HISTORY; i++) s->history[i] = -127;
    s->hist_head = 0;
    s->hist_count = 0;
    s->live_rssi = -127;
    s->peak_rssi = -127;
    s->trough_rssi = 0;
    s->trend = RoomSweepAnalyzerTrendStable;
    s->has_signal = false;
}

static inline void room_sweep_analyzer_reset(RoomSweepAnalyzerState* s) {
    room_sweep_analyzer_init(s);
}

/* Clamp RSSI into display range used for height math. */
static inline int8_t room_sweep_analyzer_clamp_rssi(int rssi) {
    if(rssi < ROOM_SWEEP_ANALYZER_FLOOR_DBM) return (int8_t)ROOM_SWEEP_ANALYZER_FLOOR_DBM;
    if(rssi > ROOM_SWEEP_ANALYZER_CEIL_DBM) return (int8_t)ROOM_SWEEP_ANALYZER_CEIL_DBM;
    return (int8_t)rssi;
}

/* 0..max_h pixels from floor..ceil dBm. */
static inline uint8_t room_sweep_analyzer_bar_height(int rssi, uint8_t max_h) {
    if(max_h == 0) return 0;
    int clamped = room_sweep_analyzer_clamp_rssi(rssi);
    int span = ROOM_SWEEP_ANALYZER_CEIL_DBM - ROOM_SWEEP_ANALYZER_FLOOR_DBM;
    if(span <= 0) return 0;
    int raised = clamped - ROOM_SWEEP_ANALYZER_FLOOR_DBM;
    if(raised < 0) raised = 0;
    int h = (raised * (int)max_h) / span;
    if(h < 0) h = 0;
    if(h > (int)max_h) h = (int)max_h;
    return (uint8_t)h;
}

/* 0..100 percent for big meter labels. */
static inline uint8_t room_sweep_analyzer_level_pct(int rssi) {
    return room_sweep_analyzer_bar_height(rssi, 100);
}

static inline int room_sweep_analyzer_avg_window(
    const RoomSweepAnalyzerState* s,
    uint8_t start_age,
    uint8_t len) {
    if(!s || s->hist_count == 0 || len == 0) return -127;
    int sum = 0;
    uint8_t n = 0;
    for(uint8_t i = 0; i < len; i++) {
        uint8_t age = (uint8_t)(start_age + i);
        if(age >= s->hist_count) break;
        /* age 0 = newest */
        uint8_t idx =
            (uint8_t)((s->hist_head + ROOM_SWEEP_ANALYZER_HISTORY - 1U - age) %
                      ROOM_SWEEP_ANALYZER_HISTORY);
        sum += s->history[idx];
        n++;
    }
    if(n == 0) return -127;
    return sum / (int)n;
}

static inline RoomSweepAnalyzerTrend room_sweep_analyzer_compute_trend(
    const RoomSweepAnalyzerState* s) {
    if(!s || s->hist_count < 8) return RoomSweepAnalyzerTrendStable;
    /* Compare newest quarter vs older quarter (higher RSSI = closer). */
    uint8_t half = s->hist_count / 2U;
    if(half < 3) return RoomSweepAnalyzerTrendStable;
    int recent = room_sweep_analyzer_avg_window(s, 0, half);
    int older = room_sweep_analyzer_avg_window(s, half, half);
    int delta = recent - older;
    if(delta >= 3) return RoomSweepAnalyzerTrendCloser;
    if(delta <= -3) return RoomSweepAnalyzerTrendFarther;
    return RoomSweepAnalyzerTrendStable;
}

static inline void room_sweep_analyzer_push(RoomSweepAnalyzerState* s, int rssi) {
    if(!s) return;
    int8_t sample = (rssi < -127) ? -127 : (rssi > 0) ? 0 : (int8_t)rssi;
    s->history[s->hist_head] = sample;
    s->hist_head = (uint8_t)((s->hist_head + 1U) % ROOM_SWEEP_ANALYZER_HISTORY);
    if(s->hist_count < ROOM_SWEEP_ANALYZER_HISTORY) s->hist_count++;
    s->live_rssi = sample;
    s->has_signal = sample > ROOM_SWEEP_ANALYZER_FLOOR_DBM;
    if(s->peak_rssi < sample) s->peak_rssi = sample;
    if(s->trough_rssi == 0 || sample < s->trough_rssi) s->trough_rssi = sample;
    s->trend = room_sweep_analyzer_compute_trend(s);
}

static inline int8_t room_sweep_analyzer_history_at(
    const RoomSweepAnalyzerState* s,
    uint8_t age) {
    if(!s || age >= s->hist_count) return -127;
    uint8_t idx =
        (uint8_t)((s->hist_head + ROOM_SWEEP_ANALYZER_HISTORY - 1U - age) %
                  ROOM_SWEEP_ANALYZER_HISTORY);
    return s->history[idx];
}

static inline const char* room_sweep_analyzer_trend_text(RoomSweepAnalyzerTrend t) {
    switch(t) {
    case RoomSweepAnalyzerTrendCloser:
        return "CLOSER";
    case RoomSweepAnalyzerTrendFarther:
        return "FARTHER";
    case RoomSweepAnalyzerTrendStable:
    default:
        return "STABLE";
    }
}

static inline const char* room_sweep_analyzer_trend_arrow(RoomSweepAnalyzerTrend t) {
    switch(t) {
    case RoomSweepAnalyzerTrendCloser:
        return "^";
    case RoomSweepAnalyzerTrendFarther:
        return "v";
    case RoomSweepAnalyzerTrendStable:
    default:
        return "=";
    }
}

/* Map raw activity count (0..255) into synthetic RSSI for shared meter. */
static inline int room_sweep_analyzer_activity_to_rssi(uint8_t activity) {
    /* 0 → -110, 255 → -30 */
    int r = ROOM_SWEEP_ANALYZER_FLOOR_DBM +
            ((int)activity * (ROOM_SWEEP_ANALYZER_CEIL_DBM - ROOM_SWEEP_ANALYZER_FLOOR_DBM)) / 255;
    return r;
}

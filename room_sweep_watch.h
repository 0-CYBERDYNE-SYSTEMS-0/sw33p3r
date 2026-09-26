#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "room_sweep_state.h"

/*
 * Phase 8 — RF burst watch (lock-and-log). The watch state machine measures
 * bursts of energy on ONE frequency: it opens a burst on a rising edge above
 * ROOM_SWEEP_SIGNAL_THRESHOLD_DBM and closes it after WATCH_BURST_GAP_MS of
 * silence. This is timing evidence for duty-cycled transmitters (the kind of
 * pattern an intermittent emitter leaves) — energy on a frequency, never
 * protocol, identity, distance, or direction. RSSI is not distance.
 *
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * INTEGER MATH ONLY: the project builds with -Wdouble-promotion as an error;
 * the shared threshold exposes an integer companion so this state machine
 * never evaluates a floating-point expression.
 */

/* Silence span that closes a burst. A burst's own ACTIVE span (first to
 * last sample above threshold) is what gets logged; this gap is detection
 * latency, not transmission time. */
#define WATCH_BURST_GAP_MS 600U

/* ENERGY gate only (same contract as ROOM_SWEEP_SIGNAL_THRESHOLD_DBM):
 * energy above -75 dBm, no identification of any kind. */
#define WATCH_RSSI_THRESHOLD_DB ROOM_SWEEP_SIGNAL_THRESHOLD_DBM_INT
_Static_assert(
    WATCH_RSSI_THRESHOLD_DB == ROOM_SWEEP_SIGNAL_THRESHOLD_DBM_INT,
    "watch threshold tracks the shared energy gate");

/* "RF WATCH 433.9M" label: "928.0M" worst case (6 chars + NUL). */
#define ROOM_SWEEP_WATCH_LABEL_MAX 7

/* CSV detail "duty=100% bursts=4294967295" worst case (27 chars + NUL). */
#define ROOM_SWEEP_WATCH_DETAIL_MAX 32

typedef struct {
    bool open;
    uint32_t open_tick, last_above_tick;
    int8_t max_rssi;
    uint32_t bursts, total_open_ms; /* for duty% */
    /* Added beyond the spec sketch so the UI helpers have honest anchors:
     * window_start_tick is the duty-percent denominator (set by
     * room_sweep_watch_reset / auto-armed on the first tick) and
     * last_close_tick stamps when the newest burst closed. The five fields
     * above match the spec contract byte for byte. */
    uint32_t window_start_tick;
    uint32_t last_close_tick;
} RoomSweepWatch;

/* Fresh measurement window: zero the counters and anchor the duty
 * denominator at `now`. Call when the watch arms (entering the sub-mode or
 * re-locking the frequency); a mid-window re-anchor would corrupt the duty
 * percentage, so callers must not call this while keeping the old window. */
static inline void room_sweep_watch_reset(RoomSweepWatch* w, uint32_t now) {
    if(!w) return;
    RoomSweepWatch fresh = {0};
    fresh.window_start_tick = now;
    *w = fresh;
}

/*
 * Feed one RSSI sample taken at `now` while the receiver was tuned to the
 * watched frequency. Caller owns timing (a ~5 ms cadence is intended) and
 * must feed monotonically increasing ticks. rx_active says the receiver is
 * actually tuned to the watched frequency; while it is false the sample
 * counts as silence (an open burst decays and closes — the watch never
 * holds a burst open across time it could not observe).
 *
 * Returns +1 exactly when a burst CLOSES (caller logs it), 0 otherwise.
 * A NULL watch is refused with 0, not a crash.
 */
static inline int room_sweep_watch_tick(
    RoomSweepWatch* w,
    uint32_t now,
    int8_t rssi,
    bool rx_active) {
    if(!w) return 0;
    if(w->window_start_tick == 0) w->window_start_tick = now; /* auto-arm */

    bool above = rx_active && (rssi > WATCH_RSSI_THRESHOLD_DB);

    if(w->open) {
        if(above) {
            w->last_above_tick = now;
            if(rssi > w->max_rssi) w->max_rssi = rssi;
        } else if((uint32_t)(now - w->last_above_tick) >= WATCH_BURST_GAP_MS) {
            /* Close: bank the burst's ACTIVE span (first to last above-
             * threshold sample; the 600 ms gap is latency, not TX time). */
            w->total_open_ms += (w->last_above_tick - w->open_tick);
            w->last_close_tick = now;
            w->bursts++;
            w->open = false;
            return 1;
        }
        return 0;
    }

    if(above) {
        /* Rising edge opens the burst; max restarts at this sample. */
        w->open = true;
        w->open_tick = now;
        w->last_above_tick = now;
        w->max_rssi = rssi;
    }
    return 0;
}

/*
 * TRUE once any burst energy has been seen (open right now or at least one
 * closed burst). "LAST" has no meaning before this is true.
 */
static inline bool room_sweep_watch_has_burst(const RoomSweepWatch* w) {
    return w && (w->open || w->bursts > 0U);
}

/*
 * Seconds since the watched frequency was last ABOVE the energy threshold —
 * the newest burst's most recent energy (for a closed burst the close itself
 * lands WATCH_BURST_GAP_MS later by construction). Returns 0 when nothing
 * has ever been observed; gate the display on room_sweep_watch_has_burst.
 * Unsigned subtraction stays correct across a uint32 tick wrap.
 */
static inline uint32_t room_sweep_watch_last_age_s(const RoomSweepWatch* w, uint32_t now) {
    if(!w) return 0;
    if(!w->open && w->bursts == 0U) return 0;
    return (uint32_t)(now - w->last_above_tick) / 1000U;
}

/*
 * Integer duty percent: share of the current measurement window
 * (now - window_start_tick) during which energy was above the threshold.
 * Closed bursts contribute their banked spans; an open burst contributes its
 * span so far. Saturates at 100; the overflow-safe two-step keeps the
 * multiply inside uint32 (the shift-down path only runs past ~12 h of
 * accumulated energy in one window, where 1% resolution is honest).
 */
static inline uint32_t room_sweep_watch_duty_percent(const RoomSweepWatch* w, uint32_t now) {
    if(!w) return 0;
    uint32_t span = (uint32_t)(now - w->window_start_tick);
    if(span == 0U) return 0U;
    uint32_t open_ms = w->total_open_ms;
    if(w->open) open_ms += (w->last_above_tick - w->open_tick);
    if(open_ms >= span) return 100U;
    while(open_ms > (0xFFFFFFFFU / 100U)) {
        span >>= 1;
        open_ms >>= 1;
        if(span == 0U) return 100U; /* unreachable; keeps the division safe */
    }
    uint32_t percent = (open_ms * 100U) / span;
    if(percent > 100U) percent = 100U;
    return percent;
}

/*
 * Watch page frequency label, rounded to 0.1 MHz in integer math with
 * carry ("303875000" -> "303.9M", "928000000" -> "928.0M"). hz == 0 (no
 * lock yet) renders "-". Output is always NUL-terminated and the worst case
 * fits ROOM_SWEEP_WATCH_LABEL_MAX.
 */
static inline void room_sweep_watch_freq_label(char* output, size_t capacity, uint32_t hz) {
    if(!output || capacity == 0U) return;
    if(hz == 0U) {
        output[0] = '-';
        output[1] = '\0';
        return;
    }
    uint32_t mhz = hz / 1000000U;
    uint32_t tenth = (hz % 1000000U + 50000U) / 100000U;
    if(tenth >= 10U) { /* 0.95..0.99 rounds up into the whole MHz */
        tenth = 0U;
        mhz++;
    }
    snprintf(output, capacity, "%lu.%luM", (unsigned long)mhz, (unsigned long)tenth);
}

/*
 * CSV detail token for one closed burst: "duty=N% bursts=M" (the values are
 * read under the app mutex by the caller). Always NUL-terminated; the worst
 * case fits ROOM_SWEEP_WATCH_DETAIL_MAX.
 */
static inline void
    room_sweep_watch_detail(char* output, size_t capacity, uint32_t duty_percent, uint32_t bursts) {
    if(!output || capacity == 0U) return;
    snprintf(output, capacity, "duty=%lu%% bursts=%lu", (unsigned long)duty_percent, (unsigned long)bursts);
}

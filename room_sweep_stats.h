#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Phase 7 — per-device RSSI evidence stats. Rows stop throwing away signal
 * history: min/max/avg are kept per WifiAp/BleDev row and surface in the
 * CSV observation detail, the Wi/BT detail pages, and the Room Report.
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * INTEGER MATH ONLY: the project builds with -Wdouble-promotion as an
 * error — no float anywhere in this header.
 *
 * Overflow analysis (spec 2026-09-20): the sum stores SIGMA(rssi + 120) so
 * every addend is a small non-negative integer. Worst case with the
 * observation counter at its uint16 ceiling is 65535 * 127 < 2^24 — two
 * orders of magnitude inside uint32.
 */

/* Sum offset base: rssi + 120 >= 0 for every realistic dBm sample. */
#define ROOM_SWEEP_STATS_SUM_BASE 120

/*
 * Longest stats line the detail pages draw: "-128dBm -128..-127 n>9k" = 23
 * chars. At UI_FONT_KEYBOARD_PX (5px) from UI_MARGIN_X (2) that is
 * 2 + 23*5 = 117 <= UI_TEXT_RIGHT_EDGE (126) — the budget is verified
 * arithmetically in tests/test_ui_layout.c.
 */
#define ROOM_SWEEP_STATS_LINE_MAX 23

/*
 * Fold one observation into a row's running stats. `obs` is the PRE-
 * increment observation count: 0 means "first sample" and initializes the
 * row (min = max = rssi); obs > 0 folds monotonically. Never call with a
 * NULL field pointer from the same row partially — all three pointers must
 * be valid.
 *
 * Truth note: min/max always hold the true extreme dBm values. Only the
 * SUM clamps: a sample below -120 dBm (rssi + 120 would go negative and
 * break the unsigned-sum arithmetic) contributes the -120 floor offset, so
 * avg can read slightly weaker-than-true at the very bottom of the scale —
 * it never invents a stronger signal.
 */
static inline void room_sweep_stats_absorb(
    int8_t* min,
    int8_t* max,
    uint32_t* sum,
    int8_t rssi,
    uint16_t obs) {
    if(!min || !max || !sum) return;
    if(obs == 0) {
        *min = rssi;
        *max = rssi;
        int addend = rssi + ROOM_SWEEP_STATS_SUM_BASE;
        if(addend < 0) addend = 0; /* clamp at the -120 floor */
        *sum = (uint32_t)addend;
        return;
    }
    if(rssi < *min) *min = rssi;
    if(rssi > *max) *max = rssi;
    int addend = rssi + ROOM_SWEEP_STATS_SUM_BASE;
    if(addend < 0) addend = 0;
    *sum += (uint32_t)addend;
}

/*
 * Rounded average dBm over `obs` observations whose sum stored
 * SIGMA(rssi + 120). Round-to-nearest in pure integer math (half rounds
 * up). obs == 0 yields 0 — callers only draw the average for rows with at
 * least one observation.
 */
static inline int room_sweep_stats_avg(int32_t sum, uint16_t obs) {
    if(obs == 0 || sum < 0) return 0;
    int32_t numerator = sum * 2 + (int32_t)obs;
    int32_t quotient = numerator / ((int32_t)obs * 2);
    return (int)quotient - ROOM_SWEEP_STATS_SUM_BASE;
}

/*
 * The detail-page stats line: "-38dBm -72..-38 n14". Collapses the range
 * when min == max (a single sighting or a rock-steady source) and caps the
 * observation count at "n>9k" so the 23-char/117px budget holds for every
 * int8/uint16 input. Output is always NUL-terminated.
 */
static inline void room_sweep_stats_line(
    char* output,
    size_t capacity,
    int rssi,
    int min,
    int max,
    unsigned observations) {
    if(!output || capacity == 0) return;
    output[0] = '\0';
    char count[8];
    if(observations > 9999U) {
        strncpy(count, "n>9k", sizeof(count) - 1U);
        count[sizeof(count) - 1U] = '\0';
    } else {
        snprintf(count, sizeof(count), "n%u", observations);
    }
    if(min >= max) {
        snprintf(output, capacity, "%ddBm %s", rssi, count);
    } else {
        snprintf(output, capacity, "%ddBm %d..%d %s", rssi, min, max, count);
    }
}

/*
 * Compose the bounded WIFI/BLE observation detail (fits a 48-byte buffer):
 *
 *   with MAC:  "oui=<evidence>"[: hints][: rogue][: watch][: stats][: tail]
 *   no MAC:    "<tail>; no MAC"[: hints][: stats]
 *
 * Token order is fixed; every token is ALL-OR-NOTHING — a token that does
 * not fully fit is skipped, so snprintf-style mid-value truncation can
 * never fabricate evidence. The stats token takes "; min=<a> max=<b>
 * avg=<c>" and is attempted before the tail, so in tight buffers the tail
 * phrase yields first (per the phase contract). The rogue token applies
 * only to MAC-bearing rows (a row without a BSSID can never be in a
 * duplicate group). The Phase 9 watch token (user-flagged watchlist hit)
 * likewise applies only to MAC-bearing rows — identity comes from the
 * address, never the label.
 */
static inline void room_sweep_stats_detail(
    char* output,
    size_t capacity,
    bool has_mac,
    const char* evidence,
    const char* hints,
    int rogue,
    bool watched,
    int8_t rssi_min,
    int8_t rssi_max,
    uint32_t rssi_sum,
    uint16_t observations,
    const char* tail) {
    if(!output || capacity == 0) return;
    output[0] = '\0';
    size_t used = 0;
    char token[64];
    bool tail_pending = true;

#define RS_STATS_TRY(TEXT)                                    \
    do {                                                      \
        const char* text_ = (TEXT);                           \
        if(used == 0 && text_[0] == ';' && text_[1] == ' ') { \
            text_ += 2; /* first token needs no separator */  \
        }                                                     \
        size_t len_ = strlen(text_);                          \
        if(used + len_ < capacity) {                          \
            memcpy(output + used, text_, len_ + 1U);          \
            used += len_;                                     \
        }                                                     \
    } while(0)

    if(has_mac && evidence && evidence[0] != '\0') {
        snprintf(token, sizeof(token), "oui=%s", evidence);
        RS_STATS_TRY(token);
    } else if(!has_mac && tail && tail[0] != '\0') {
        snprintf(token, sizeof(token), "%s; no MAC", tail);
        RS_STATS_TRY(token);
        tail_pending = false;
    }
    if(hints && hints[0] != '\0') {
        snprintf(token, sizeof(token), "; hints=%s", hints);
        RS_STATS_TRY(token);
    }
    if(has_mac && rogue >= 2) {
        snprintf(token, sizeof(token), "; rogue=%d", rogue);
        RS_STATS_TRY(token);
    }
    if(has_mac && watched) {
        RS_STATS_TRY("; watch=1");
    }
    if(observations > 0U) {
        int avg = room_sweep_stats_avg((int32_t)rssi_sum, observations);
        snprintf(
            token,
            sizeof(token),
            "; min=%d max=%d avg=%d",
            (int)rssi_min,
            (int)rssi_max,
            avg);
        RS_STATS_TRY(token);
    }
    if(tail_pending && tail && tail[0] != '\0') {
        snprintf(token, sizeof(token), "; %s", tail);
        RS_STATS_TRY(token);
    }
#undef RS_STATS_TRY
}

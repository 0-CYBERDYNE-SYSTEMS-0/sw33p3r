#include <stdio.h>

#include "../room_sweep_analyzer.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    RoomSweepAnalyzerState s;
    room_sweep_analyzer_init(&s);
    check(s.hist_count == 0, "empty history");
    check(room_sweep_analyzer_bar_height(-110, 40) == 0, "floor is zero height");
    check(room_sweep_analyzer_bar_height(-30, 40) == 40, "ceil is full height");
    check(room_sweep_analyzer_level_pct(-70) == 50, "mid RSSI is ~50%");

    /* Rising sequence → STRONGER (RSSI up, not a distance claim) */
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -90);
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -50);
    check(s.hist_count == ROOM_SWEEP_ANALYZER_HISTORY, "history fills");
    check(s.trend == RoomSweepAnalyzerTrendCloser, "rising RSSI is stronger");
    check(s.peak_rssi == -50, "peak tracks max");
    check(s.live_rssi == -50, "live is newest");

    room_sweep_analyzer_reset(&s);
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -40);
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -95);
    check(s.trend == RoomSweepAnalyzerTrendFarther, "falling RSSI is weaker");

    room_sweep_analyzer_reset(&s);
    for(int i = 0; i < 16; i++) room_sweep_analyzer_push(&s, -70);
    check(s.trend == RoomSweepAnalyzerTrendStable, "flat is stable");

    check(
        room_sweep_analyzer_activity_to_rssi(0) == ROOM_SWEEP_ANALYZER_FLOOR_DBM,
        "zero activity is floor");
    check(
        room_sweep_analyzer_activity_to_rssi(255) == ROOM_SWEEP_ANALYZER_CEIL_DBM,
        "max activity is ceil");
    check(
        room_sweep_analyzer_trend_text(RoomSweepAnalyzerTrendCloser)[0] == 'S',
        "stronger label is honest (no distance claim)");
    check(
        room_sweep_analyzer_trend_text(RoomSweepAnalyzerTrendFarther)[0] == 'W',
        "weaker label is honest (no distance claim)");
    check(room_sweep_analyzer_history_at(&s, 0) == -70, "newest sample");

    /* Age-out: fresh holds; dead is empty; mid fades down. */
    check(room_sweep_analyzer_aged_rssi(-50, 0) == -50, "fresh age keeps RSSI");
    check(
        room_sweep_analyzer_aged_rssi(-50, ROOM_SWEEP_ANALYZER_STALE_MS - 1) == -50,
        "just under stale still fresh");
    check(
        room_sweep_analyzer_aged_rssi(-50, ROOM_SWEEP_ANALYZER_DEAD_MS) == -127,
        "dead age is empty");
    check(
        room_sweep_analyzer_aged_rssi(-50, ROOM_SWEEP_ANALYZER_DEAD_MS + 1000) == -127,
        "past dead stays empty");
    int mid = room_sweep_analyzer_aged_rssi(
        -50,
        ROOM_SWEEP_ANALYZER_STALE_MS +
            (ROOM_SWEEP_ANALYZER_DEAD_MS - ROOM_SWEEP_ANALYZER_STALE_MS) / 2U);
    check(mid < -50 && mid > -127, "mid age fades between last and empty");
    check(room_sweep_analyzer_bar_height(-127, 100) == 0, "empty sample is zero bar");
    check(room_sweep_analyzer_is_stale(ROOM_SWEEP_ANALYZER_STALE_MS), "stale flag");
    check(room_sweep_analyzer_is_dead(ROOM_SWEEP_ANALYZER_DEAD_MS), "dead flag");

    room_sweep_analyzer_reset(&s);
    room_sweep_analyzer_push(&s, -60);
    room_sweep_analyzer_push(&s, -127);
    check(s.live_rssi == -127, "silent push becomes live empty");
    check(!s.has_signal, "silent sample clears has_signal");
    check(room_sweep_analyzer_level_pct(s.live_rssi) == 0, "silent is 0 percent");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

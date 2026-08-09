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

    /* Rising sequence → CLOSER */
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -90);
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -50);
    check(s.hist_count == ROOM_SWEEP_ANALYZER_HISTORY, "history fills");
    check(s.trend == RoomSweepAnalyzerTrendCloser, "rising RSSI is closer");
    check(s.peak_rssi == -50, "peak tracks max");
    check(s.live_rssi == -50, "live is newest");

    room_sweep_analyzer_reset(&s);
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -40);
    for(int i = 0; i < 24; i++) room_sweep_analyzer_push(&s, -95);
    check(s.trend == RoomSweepAnalyzerTrendFarther, "falling RSSI is farther");

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
        room_sweep_analyzer_trend_text(RoomSweepAnalyzerTrendCloser)[0] == 'C',
        "closer label");
    check(room_sweep_analyzer_history_at(&s, 0) == -70, "newest sample");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

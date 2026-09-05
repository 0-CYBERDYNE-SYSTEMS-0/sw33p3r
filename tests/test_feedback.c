/* test_feedback.c — host unit tests for room_sweep_feedback.h. Compile with cc. */
#include "room_sweep_feedback.h"

#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if(cond) {                                              \
            printf("  PASS: %s\n", msg);                        \
        } else {                                                \
            printf("  FAIL: %s\n", msg);                        \
            failures++;                                         \
        }                                                       \
    } while(0)

/* float-only compare (no double promotion) */
static int near_eq(float a, float b) {
    float d = a - b;
    if(d < 0.0f) d = -d;
    return d < 0.01f;
}

/* Pick with only the target candidate present (others absent). */
static float pick_target_only(int8_t rssi, uint32_t age_ms, bool* valid) {
    return room_sweep_feedback_pick_wireless(
        true, rssi, age_ms, false, 0, 0, false, 0, 0, valid);
}

int main(void) {
    /* --- Priority: target > selected > strongest --- */
    printf("Test 1: pick priority\n");
    bool valid = false;
    float peak = room_sweep_feedback_pick_wireless(
        true, -60, 0, true, -80, 0, true, -90, 0, &valid);
    CHECK(valid, "all present: valid");
    CHECK(near_eq(peak, -60.0f), "target beats selected beats strongest");

    peak = room_sweep_feedback_pick_wireless(
        false, 0, 0, true, -80, 0, true, -90, 0, &valid);
    CHECK(valid && near_eq(peak, -80.0f), "absent target falls to selected");

    peak = room_sweep_feedback_pick_wireless(
        false, 0, 0, false, 0, 0, true, -90, 0, &valid);
    CHECK(valid && near_eq(peak, -90.0f), "only strongest present wins");

    peak = room_sweep_feedback_pick_wireless(
        false, 0, 0, false, 0, 0, false, 0, 0, &valid);
    CHECK(!valid, "all absent: out_valid false");
    CHECK(near_eq(peak, -120.0f), "all absent: silent sentinel");

    /* Dead candidates (aged to -127) fall through like absent ones. */
    peak = room_sweep_feedback_pick_wireless(
        true, -60, ROOM_SWEEP_ANALYZER_DEAD_MS, true, -80, 0, true, -90, 0, &valid);
    CHECK(valid && near_eq(peak, -80.0f), "dead target falls to selected");

    peak = room_sweep_feedback_pick_wireless(
        true, -60, ROOM_SWEEP_ANALYZER_DEAD_MS, true, -80,
        ROOM_SWEEP_ANALYZER_DEAD_MS + 1U, true, -90, 0, &valid);
    CHECK(valid && near_eq(peak, -90.0f), "dead target+selected fall to strongest");

    peak = room_sweep_feedback_pick_wireless(
        true, -60, ROOM_SWEEP_ANALYZER_DEAD_MS, false, 0, 0, true, -90,
        ROOM_SWEEP_ANALYZER_DEAD_MS, &valid);
    CHECK(!valid, "all dead: out_valid false");

    /* NULL out_valid tolerated. */
    peak = pick_target_only(-55, 0, NULL);
    CHECK(near_eq(peak, -55.0f), "NULL out_valid tolerated");

    /* --- Aging: fresh raw, linear fade, dead at DEAD_MS --- */
    printf("Test 2: aging\n");
    peak = pick_target_only(-60, 0, &valid);
    CHECK(valid && near_eq(peak, -60.0f), "age 0 returns raw");

    peak = pick_target_only(-60, ROOM_SWEEP_ANALYZER_STALE_MS - 1U, &valid);
    CHECK(valid && near_eq(peak, -60.0f), "below STALE_MS still raw");

    peak = pick_target_only(-60, ROOM_SWEEP_ANALYZER_STALE_MS, &valid);
    CHECK(valid && near_eq(peak, -60.0f), "at STALE_MS fade just starts");

    /* Mid-fade: aged(-60, 4000) = -60 + (-67 * 2000 / 4000) = -93 (int math). */
    peak = pick_target_only(-60, 4000U, &valid);
    CHECK(valid, "mid-age still valid");
    {
        float expected = -93.0f;
        float d = peak - expected;
        if(d < 0.0f) d = -d;
        CHECK(d < 1.5f, "mid-age fades linearly toward -127");
    }

    peak = pick_target_only(-60, ROOM_SWEEP_ANALYZER_DEAD_MS, &valid);
    CHECK(!valid, "age >= DEAD_MS invalid (-127)");

    peak = pick_target_only(-60, ROOM_SWEEP_ANALYZER_DEAD_MS * 3U, &valid);
    CHECK(!valid, "very old invalid");

    /* --- Sound ladder: scanner buckets --- */
    printf("Test 3: sound ladder\n");
    CHECK(room_sweep_feedback_sound_interval_ms(-49.5f, false) == 60, ">-50 -> 60ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-50.0f, false) == 100, "-50 -> 100ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-59.5f, false) == 100, ">-60 -> 100ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-60.0f, false) == 180, "-60 -> 180ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-69.5f, false) == 180, ">-70 -> 180ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-70.0f, false) == 350, "-70 -> 350ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-79.5f, false) == 350, ">-80 -> 350ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-80.0f, false) == 700, "-80 -> 700ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-89.5f, false) == 700, ">-90 -> 700ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-90.0f, false) == 1200, "-90 -> 1200ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-99.5f, false) == 1200, ">-100 -> 1200ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-100.0f, false) == 2000, "-100 -> 2000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-120.0f, false) == 2000, "silent -> 2000ms");

    /* --- Sound ladder: GPS buckets --- */
    CHECK(room_sweep_feedback_sound_interval_ms(-69.5f, true) == 200, "gps >-70 -> 200ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-70.0f, true) == 500, "gps -70 -> 500ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-89.5f, true) == 500, "gps >-90 -> 500ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-90.0f, true) == 1000, "gps -90 -> 1000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-109.5f, true) == 1000, "gps >-110 -> 1000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-110.0f, true) == 2000, "gps -110 -> 2000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-120.0f, true) == 2000, "gps silent -> 2000ms");

    /* --- Vibro ladder: buckets --- */
    printf("Test 4: vibro ladder\n");
    CHECK(room_sweep_feedback_vibro_interval_ms(-49.5f) == 150, ">-50 -> 150ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-50.0f) == 300, "-50 -> 300ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-59.5f) == 300, ">-60 -> 300ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-60.0f) == 600, "-60 -> 600ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-69.5f) == 600, ">-70 -> 600ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-70.0f) == 1200, "-70 -> 1200ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-79.5f) == 1200, ">-80 -> 1200ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-80.0f) == 2500, "-80 -> 2500ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-89.5f) == 2500, ">-90 -> 2500ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-90.0f) == 5000, "-90 -> 5000ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-120.0f) == 5000, "silent -> 5000ms");

    /* --- Vibro ladder: monotonic non-increasing as peak rises --- */
    {
        uint32_t prev = room_sweep_feedback_vibro_interval_ms(-45.0f);
        int mono = 1;
        const float probes[] = {
            -45.0f, -55.0f, -65.0f, -75.0f, -85.0f, -95.0f, -115.0f};
        for(unsigned i = 1; i < sizeof(probes) / sizeof(probes[0]); i++) {
            uint32_t cur = room_sweep_feedback_vibro_interval_ms(probes[i]);
            if(cur < prev) mono = 0;
            prev = cur;
        }
        CHECK(mono, "documented probes are monotonic");
    }
    {
        /* Full sweep: colder peak must never shorten the interval. */
        int mono = 1;
        for(int p = -30; p >= -125; p -= 5) {
            uint32_t hotter = room_sweep_feedback_vibro_interval_ms((float)(p));
            uint32_t colder = room_sweep_feedback_vibro_interval_ms((float)(p - 5));
            if(colder < hotter) mono = 0;
        }
        CHECK(mono, "full -30..-125 sweep monotonic");
    }

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

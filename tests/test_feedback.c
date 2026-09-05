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

    /* --- Continuous sound curve --- */
    printf("Test 3: sound curve\n");
    CHECK(room_sweep_feedback_sound_interval_ms(-110.0f) == 2000, "-110 -> 2000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-120.0f) == 2000, "silent sentinel clamps -> 2000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-115.0f) == 2000, "below range clamps -> 2000ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-30.0f) == 60, "-30 -> 60ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-25.0f) == 60, "above range clamps -> 60ms");
    CHECK(room_sweep_feedback_sound_interval_ms(-70.0f) == 1030, "-70 -> 1030ms spot value");
    CHECK(room_sweep_feedback_sound_interval_ms(-69.6f) == 1030, "-69.6 rounds to -70 -> 1030ms");

    /* --- Continuous GPS curve (peak = -100 + 4 * sats) --- */
    CHECK(room_sweep_feedback_gps_interval_ms(-100.0f) == 2000, "0 sats (-100) -> 2000ms");
    CHECK(room_sweep_feedback_gps_interval_ms(-120.0f) == 2000, "silent clamps -> 2000ms");
    CHECK(room_sweep_feedback_gps_interval_ms(-60.0f) == 200, "10 sats (-60) -> 200ms");
    CHECK(room_sweep_feedback_gps_interval_ms(-50.0f) == 200, "above range clamps -> 200ms");
    CHECK(room_sweep_feedback_gps_interval_ms(-80.0f) == 1100, "5 sats (-80) -> 1100ms spot value");

    /* --- Continuous vibro curve --- */
    printf("Test 4: vibro curve\n");
    CHECK(room_sweep_feedback_vibro_interval_ms(-110.0f) == 5000, "-110 -> 5000ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-120.0f) == 5000, "silent sentinel clamps -> 5000ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-30.0f) == 150, "-30 -> 150ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-25.0f) == 150, "above range clamps -> 150ms");
    CHECK(room_sweep_feedback_vibro_interval_ms(-70.0f) == 2575, "-70 -> 2575ms spot value");

    /* --- Monotonic non-increasing for every 1 dB step, -115..-25, all three --- */
    {
        int mono_sound = 1, mono_vibro = 1, mono_gps = 1;
        for(int db = -115; db < -25; db++) {
            if(room_sweep_feedback_sound_interval_ms((float)db) <
               room_sweep_feedback_sound_interval_ms((float)(db + 1)))
                mono_sound = 0;
            if(room_sweep_feedback_vibro_interval_ms((float)db) <
               room_sweep_feedback_vibro_interval_ms((float)(db + 1)))
                mono_vibro = 0;
            if(room_sweep_feedback_gps_interval_ms((float)db) <
               room_sweep_feedback_gps_interval_ms((float)(db + 1)))
                mono_gps = 0;
        }
        CHECK(mono_sound, "sound monotonic non-increasing per 1 dB step");
        CHECK(mono_vibro, "vibro monotonic non-increasing per 1 dB step");
        CHECK(mono_gps, "gps monotonic non-increasing per 1 dB step");
    }

    /* --- Smoothness: per-dB delta inside the clamped range --- */
    {
        int smooth_sound = 1, smooth_vibro = 1;
        for(int db = -110; db < -30; db++) {
            uint32_t d_sound = room_sweep_feedback_sound_interval_ms((float)db) -
                               room_sweep_feedback_sound_interval_ms((float)(db + 1));
            uint32_t d_vibro = room_sweep_feedback_vibro_interval_ms((float)db) -
                               room_sweep_feedback_vibro_interval_ms((float)(db + 1));
            if(d_sound == 0 || d_sound > 30U) smooth_sound = 0;
            if(d_vibro == 0 || d_vibro > 65U) smooth_vibro = 0;
        }
        CHECK(smooth_sound, "sound per-dB delta in (0, 30] ms");
        CHECK(smooth_vibro, "vibro per-dB delta in (0, 65] ms");
    }

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

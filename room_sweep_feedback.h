#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "room_sweep_analyzer.h"

/*
 * Host-testable feedback cadence state: per-tab Geiger peak selection and
 * sound/vibro interval curves. Flipper-header-free (host suites compile
 * this with plain cc and -Werror -pedantic).
 *
 * Contract (specs/per-tab-feedback.md):
 *  - WiFi/BLE peak source priority: locked target > selected scroll row >
 *    strongest table row. The chosen RSSI is aged with the shared analyzer
 *    fade (fresh -> linear fade to -127 across STALE_MS..DEAD_MS) so walking
 *    out of range decays the click/vibro/LED rate instead of freezing it.
 *  - Sound/vibro cadence is a CONTINUOUS dB-linear curve, not a step ladder:
 *    every 1 dB closer smoothly shortens the interval (~80 distinct speeds
 *    across the clamped range). GPS cadence is the same idea on the
 *    satellite-count proxy (peak = -100 + 4 * sats).
 *  - All three curves are pure uint32 integer math and are monotonic
 *    non-increasing by construction: a hotter peak can only shorten (never
 *    lengthen) the interval.
 */

/*
 * Pick the feedback peak from three candidates in priority order
 * (target > selected > strongest; first present candidate wins).
 * The winning RSSI is aged by its own age_ms; a candidate that has faded to
 * -127 (silent for >= DEAD_MS) no longer counts as present and the next
 * candidate is tried. *out_valid is false when nothing usable remains and
 * the silent sentinel -120 dBm is returned (no alerts, slowest cadence).
 */
static inline float room_sweep_feedback_pick_wireless(
    bool target_present,
    int8_t target_rssi,
    uint32_t target_age_ms,
    bool selected_present,
    int8_t selected_rssi,
    uint32_t selected_age_ms,
    bool strongest_present,
    int8_t strongest_rssi,
    uint32_t strongest_age_ms,
    bool* out_valid) {
    bool valid = false;
    float peak = -120.0f;

    if(target_present) {
        int aged = room_sweep_analyzer_aged_rssi((int)target_rssi, target_age_ms);
        if(aged > -127) {
            peak = (float)aged;
            valid = true;
        }
    }
    if(!valid && selected_present) {
        int aged = room_sweep_analyzer_aged_rssi((int)selected_rssi, selected_age_ms);
        if(aged > -127) {
            peak = (float)aged;
            valid = true;
        }
    }
    if(!valid && strongest_present) {
        int aged = room_sweep_analyzer_aged_rssi((int)strongest_rssi, strongest_age_ms);
        if(aged > -127) {
            peak = (float)aged;
            valid = true;
        }
    }

    if(out_valid) *out_valid = valid;
    return valid ? peak : -120.0f;
}

/*
 * Continuous sound Geiger interval (scanner/info modes).
 * Peak is rounded to the nearest integer dBm, clamped to [-110, -30], and
 * mapped linearly s = clamped + 110 (0..80):
 *   2000U - (1940U * s) / 80U  ->  2000 ms (-110) sliding to 60 ms (-30),
 * ~24 ms per dB (~80 distinct speeds). Pure uint32 integer math; monotonic
 * non-increasing by construction (s only grows as the peak rises, and the
 * interpolated term never shrinks the interval).
 */
static inline uint32_t room_sweep_feedback_sound_interval_ms(float peak) {
    int db = (int)(peak + (peak >= 0.0f ? 0.5f : -0.5f));
    if(db < -110) db = -110;
    if(db > -30) db = -30;
    uint32_t s = (uint32_t)(db + 110);
    return 2000U - (1940U * s) / 80U;
}

/*
 * Continuous GPS Geiger interval. "Proximity" here is satellite count: the
 * caller maps sats to peak = -100 + 4 * sats. Peak is rounded/clamped to
 * [-100, -60], s = clamped + 100 (0..40):
 *   2000U - (1800U * s) / 40U  ->  2000 ms sliding to 200 ms (45 ms per dB).
 * Same integer math / monotonicity guarantees as the sound curve.
 */
static inline uint32_t room_sweep_feedback_gps_interval_ms(float peak) {
    int db = (int)(peak + (peak >= 0.0f ? 0.5f : -0.5f));
    if(db < -100) db = -100;
    if(db > -60) db = -60;
    uint32_t s = (uint32_t)(db + 100);
    return 2000U - (1800U * s) / 40U;
}

/*
 * Continuous graded vibro interval (coarser than sound). Same rounding and
 * clamping as the sound curve, s = clamped + 110 (0..80):
 *   5000U - (4850U * s) / 80U  ->  5000 ms (-110) sliding to 150 ms (-30),
 * ~61 ms per dB. Monotonic non-increasing by construction: a hotter peak may
 * only shorten (never lengthen) the interval. Peak -120 (silent / Info idle)
 * clamps to the 5000 ms heartbeat.
 */
static inline uint32_t room_sweep_feedback_vibro_interval_ms(float peak) {
    int db = (int)(peak + (peak >= 0.0f ? 0.5f : -0.5f));
    if(db < -110) db = -110;
    if(db > -30) db = -30;
    uint32_t s = (uint32_t)(db + 110);
    return 5000U - (4850U * s) / 80U;
}

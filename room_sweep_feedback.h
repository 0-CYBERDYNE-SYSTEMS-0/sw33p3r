#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "room_sweep_analyzer.h"

/*
 * Host-testable feedback cadence state: per-tab Geiger peak selection and
 * sound/vibro interval ladders. Flipper-header-free (host suites compile
 * this with plain cc and -Werror -pedantic).
 *
 * Contract (specs/per-tab-feedback.md):
 *  - WiFi/BLE peak source priority: locked target > selected scroll row >
 *    strongest table row. The chosen RSSI is aged with the shared analyzer
 *    fade (fresh -> linear fade to -127 across STALE_MS..DEAD_MS) so walking
 *    out of range decays the click/vibro/LED rate instead of freezing it.
 *  - Sound ladder = the historical scanner + GPS ladders, verbatim.
 *  - Vibro ladder = graded (coarser than sound); MUST stay monotonic
 *    non-increasing as peak rises (closer never vibrates less often).
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
 * Sound Geiger interval for a given peak.
 * Scanner ladder (verbatim from the original inline code):
 *   >-50 -> 60, >-60 -> 100, >-70 -> 180, >-80 -> 350, >-90 -> 700,
 *   >-100 -> 1200, else 2000 ms.
 * GPS ladder (sats mapped onto peak by the caller):
 *   >-70 -> 200, >-90 -> 500, >-110 -> 1000, else 2000 ms.
 */
static inline uint32_t room_sweep_feedback_sound_interval_ms(float peak, bool gps_mode) {
    if(gps_mode) {
        if(peak > -70.0f) return 200;
        if(peak > -90.0f) return 500;
        if(peak > -110.0f) return 1000;
        return 2000;
    }
    if(peak > -50.0f) return 60;
    if(peak > -60.0f) return 100;
    if(peak > -70.0f) return 180;
    if(peak > -80.0f) return 350;
    if(peak > -90.0f) return 700;
    if(peak > -100.0f) return 1200;
    return 2000;
}

/*
 * Graded vibro interval (coarser than the sound ladder):
 *   >-50 -> 150, >-60 -> 300, >-70 -> 600, >-80 -> 1200, >-90 -> 2500,
 *   else 5000 ms.
 * Must stay monotonic non-increasing as peak rises: a hotter peak may only
 * shorten (never lengthen) the interval. Peak -120 (silent / Info idle)
 * lands in the 5000 ms heartbeat bucket.
 */
static inline uint32_t room_sweep_feedback_vibro_interval_ms(float peak) {
    if(peak > -50.0f) return 150;
    if(peak > -60.0f) return 300;
    if(peak > -70.0f) return 600;
    if(peak > -80.0f) return 1200;
    if(peak > -90.0f) return 2500;
    return 5000;
}

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* Pure helpers for Marauder scan timeout + GPS distance.
 * Host-testable (no Flipper deps). */

/* True when a scan should enter MarauderError.
 * - scanning: current state is MarauderScanning
 * - now / scan_start_tick / timeout_ms: wall-clock window since start
 * - result_count: parsed APs or BLE devices this scan
 *
 * Rules:
 * 1. Never time out solely because last_data_tick is 0.
 * 2. If result_count > 0, silence is normal (Marauder BLE dedups prints).
 * 3. ERR only when zero results for the full timeout since scan_start.
 */
static inline bool marauder_scan_should_error(
    bool scanning,
    uint32_t now,
    uint32_t scan_start_tick,
    uint32_t timeout_ms,
    uint8_t result_count) {
    if(!scanning) return false;
    if(result_count > 0) return false;
    if(timeout_ms == 0) return false;
    return (now - scan_start_tick) >= timeout_ms;
}

/* Haversine distance in meters. Lat/lon in decimal degrees. */
static inline float geo_distance_m(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371000.0f;
    const float DEG2RAD = 0.017453292519943295f;
    float p1 = lat1 * DEG2RAD;
    float p2 = lat2 * DEG2RAD;
    float dp = (lat2 - lat1) * DEG2RAD;
    float dl = (lon2 - lon1) * DEG2RAD;
    float sdp = sinf(dp * 0.5f);
    float sdl = sinf(dl * 0.5f);
    float a = sdp * sdp + cosf(p1) * cosf(p2) * sdl * sdl;
    if(a < 0.0f) a = 0.0f;
    if(a > 1.0f) a = 1.0f;
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return R * c;
}

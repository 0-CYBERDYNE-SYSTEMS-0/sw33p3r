#include <stdio.h>
#include <string.h>

#include "../room_sweep_radar.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

static int near_int(int got, int want, int tol) {
    int d = got - want;
    if(d < 0) d = -d;
    return d <= tol;
}

static void check_sin(int16_t deg, int16_t want, int16_t tol, const char* message) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s (deg %d)", message, (int)deg);
    check(near_int(room_sweep_radar_sin128(deg), want, tol), buf);
}

static void check_atan(int32_t y, int32_t x, uint16_t want, uint16_t tol, const char* message) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s (y %ld, x %ld)", message, (long)y, (long)x);
    check(near_int((int)room_sweep_radar_atan2_deg(y, x), (int)want, (int)tol), buf);
}

static void check_bearing(
    int32_t lat1,
    int32_t lon1,
    int32_t lat2,
    int32_t lon2,
    uint16_t want,
    uint16_t tol,
    const char* message) {
    char buf[112];
    snprintf(
        buf,
        sizeof(buf),
        "%s (%ld,%ld)->(%ld,%ld)",
        message,
        (long)lat1,
        (long)lon1,
        (long)lat2,
        (long)lon2);
    check(
        near_int(
            (int)room_sweep_radar_bearing_deg(lat1, lon1, lat2, lon2),
            (int)want,
            (int)tol),
        buf);
}

int main(void) {
    /* --- sin128: cardinals, wraparound, and reference values --- */
    check_sin(0, 0, 0, "sin128(0) is zero");
    check_sin(90, 128, 0, "sin128(90) is full scale");
    check_sin(180, 0, 1, "sin128(180) is zero");
    check_sin(270, -128, 0, "sin128(270) is negative full scale");
    check_sin(360, 0, 0, "sin128(360) wraps to zero");
    check_sin(-90, -128, 0, "sin128(-90) normalizes to -128");
    check_sin(-450, -128, 0, "sin128(-450) normalizes with repeated +360");
    check_sin(810, 128, 0, "sin128(810) normalizes downward");

    {
        /* round(128*sin(deg)) reference values for non-cardinal angles. */
        static const struct {
            int16_t deg;
            int16_t ref;
        } k_refs[] = {
            {30, 64},
            {45, 91},
            {60, 111},
            {120, 111},
            {210, -64},
            {315, -91},
        };
        for(uint8_t i = 0; i < 6; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "sin128(%d) matches round(128*sin(%d))", (int)k_refs[i].deg, (int)k_refs[i].deg);
            check(near_int(room_sweep_radar_sin128(k_refs[i].deg), k_refs[i].ref, 1), buf);
        }
    }

    /* --- cos128 --- */
    check(near_int(room_sweep_radar_cos128(0), 128, 0), "cos128(0) is full scale");
    check(near_int(room_sweep_radar_cos128(90), 0, 0), "cos128(90) is zero");
    check(near_int(room_sweep_radar_cos128(180), -128, 0), "cos128(180) is negative full scale");
    check(near_int(room_sweep_radar_cos128(270), 0, 1), "cos128(270) is zero");
    check(near_int(room_sweep_radar_cos128(360), 128, 0), "cos128(360) wraps to full scale");

    /* --- atan2_deg: compass points, diagonals, and scale invariance --- */
    check_atan(0, 1, 0, 2, "atan2 north");
    check_atan(1, 0, 90, 2, "atan2 east");
    check_atan(0, -1, 180, 2, "atan2 south");
    check_atan(-1, 0, 270, 2, "atan2 west");
    check_atan(1, 1, 45, 2, "atan2 northeast");
    check_atan(1, -1, 135, 2, "atan2 southeast");
    check_atan(-1, -1, 225, 2, "atan2 southwest");
    check_atan(-1, 1, 315, 2, "atan2 northwest");
    check_atan(0, 0, 0, 0, "atan2 origin is zero");
    check_atan(123456, 7, 90, 2, "atan2 large east component");
    check_atan(-123456, 7, 270, 2, "atan2 large west component");
    check_atan(7, 123456, 0, 2, "atan2 large north component");
    check_atan(7, -123456, 180, 2, "atan2 large south component");

    /* --- bearing_deg: cardinals from the equator and pole clamp --- */
    check_bearing(0, 0, 0, 1000000, 90, 2, "equator east bearing");
    check_bearing(0, 0, 1000000, 0, 0, 2, "north bearing");
    check_bearing(0, 0, -1000000, 0, 180, 2, "south bearing");
    check_bearing(0, 0, 0, -1000000, 270, 2, "west bearing");
    check_bearing(0, 0, 1000000, 1000000, 45, 2, "northeast bearing");
    check_bearing(0, 0, -1000000, 1000000, 135, 2, "southeast bearing");
    check_bearing(0, 0, -1000000, -1000000, 225, 2, "southwest bearing");
    check_bearing(0, 0, 1000000, -1000000, 315, 2, "northwest bearing");
    check_bearing(5, 7, 5, 7, 0, 0, "coincident points bear zero");
    check_bearing(90000000, 0, 90000000, 1000000, 90, 2, "clamped polar latitude keeps east");
    check_bearing(-90000000, 0, -90000000, 1000000, 90, 2, "clamped south polar latitude keeps east");

    /* --- rssi_radius: endpoints, midpoint, and clamping --- */
    check(room_sweep_radar_rssi_radius(-127, 100) == 0, "floor dBm maps to radius 0");
    check(room_sweep_radar_rssi_radius(-20, 100) == 100, "ceil dBm maps to max radius");
    check(near_int((int)room_sweep_radar_rssi_radius(-73, 100), 50, 1), "midpoint -73 dBm is near half radius");
    check(near_int((int)room_sweep_radar_rssi_radius(-74, 100), 50, 1), "midpoint -74 dBm is near half radius");
    check(room_sweep_radar_rssi_radius(-128, 100) == 0, "below floor clamps to radius 0");
    check(room_sweep_radar_rssi_radius(0, 100) == 100, "above ceil clamps to max radius");
    check(room_sweep_radar_rssi_radius(-50, 0) == 0, "zero max radius stays zero");

    /* --- angle_for_channel: full wrap, zero count, and 45-degree grid --- */
    check(room_sweep_radar_angle_for_channel(3, 0) == 0, "zero channel count returns zero");
    {
        int16_t grid[8] = {0, 45, 90, 135, 180, 225, 270, 315};
        for(uint8_t ch = 0; ch < 8; ch++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "8 channels spread %d at 45-degree steps", (int)ch);
            check(room_sweep_radar_angle_for_channel(ch, 8) == (uint16_t)grid[ch], buf);
        }
        for(uint8_t ch = 0; ch < 16; ch++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "16 channels follow (ch*360)/16 for ch %d", (int)ch);
            check(
                room_sweep_radar_angle_for_channel(ch, 16) == (uint16_t)((ch * 360) / 16),
                buf);
        }
        check(room_sweep_radar_angle_for_channel(2, 16) == 45, "channel 2 of 16 is 45 degrees");
        check(room_sweep_radar_angle_for_channel(4, 16) == 90, "channel 4 of 16 is 90 degrees");
        check(room_sweep_radar_angle_for_channel(14, 16) == 315, "channel 14 of 16 is 315 degrees");
        check(room_sweep_radar_angle_for_channel(16, 16) == 0, "channel 16 of 16 wraps to zero");
        check(room_sweep_radar_angle_for_channel(17, 16) == 22, "channel 17 of 16 wraps to channel 1");
    }

    /* --- ring_unit_m: documented rule (largest unit <= dist/2, min 5 m) --- */
    check(room_sweep_radar_ring_unit_m(0) == 2, "dist 0 uses 2 m rings");
    check(room_sweep_radar_ring_unit_m(1) == 5, "dist 1 clamps to the 5 m minimum");
    check(room_sweep_radar_ring_unit_m(5) == 5, "dist 5 uses 5 m rings");
    check(room_sweep_radar_ring_unit_m(10) == 5, "dist 10 uses 5 m rings");
    check(room_sweep_radar_ring_unit_m(15) == 5, "dist 15 uses 5 m rings");
    check(room_sweep_radar_ring_unit_m(20) == 10, "dist 20 uses 10 m rings");
    check(room_sweep_radar_ring_unit_m(30) == 10, "dist 30 uses 10 m rings");
    check(room_sweep_radar_ring_unit_m(200) == 100, "dist 200 uses 100 m rings");
    check(room_sweep_radar_ring_unit_m(300) == 100, "dist 300 uses 100 m rings");
    check(room_sweep_radar_ring_unit_m(900) == 250, "dist 900 uses 250 m rings");
    check(room_sweep_radar_ring_unit_m(1000) == 500, "dist 1000 uses 500 m rings");
    check(room_sweep_radar_ring_unit_m(2000) == 1000, "dist 2000 uses 1000 m rings");
    check(room_sweep_radar_ring_unit_m(3000) == 1000, "dist 3000 uses 1000 m rings");
    check(room_sweep_radar_ring_unit_m(3001) == 1000, "dist above 3000 caps at 1000 m");
    check(room_sweep_radar_ring_unit_m(5000) == 1000, "dist 5000 caps at 1000 m");
    check(room_sweep_radar_ring_unit_m(100000) == 1000, "very large dist caps at 1000 m");

    /* --- ring_label --- */
    check(strcmp(room_sweep_radar_ring_label(2), "2m") == 0, "2 m label");
    check(strcmp(room_sweep_radar_ring_label(5), "5m") == 0, "5 m label");
    check(strcmp(room_sweep_radar_ring_label(10), "10m") == 0, "10 m label");
    check(strcmp(room_sweep_radar_ring_label(20), "20m") == 0, "20 m label");
    check(strcmp(room_sweep_radar_ring_label(50), "50m") == 0, "50 m label");
    check(strcmp(room_sweep_radar_ring_label(100), "100m") == 0, "100 m label");
    check(strcmp(room_sweep_radar_ring_label(250), "250m") == 0, "250 m label");
    check(strcmp(room_sweep_radar_ring_label(500), "500m") == 0, "500 m label");
    check(strcmp(room_sweep_radar_ring_label(1000), "1km") == 0, "1000 m label is 1km");
    check(strcmp(room_sweep_radar_ring_label(3), "m") == 0, "unknown unit falls back to plain m");

    /* --- blip_xy: 0 deg is up, screen y grows downward --- */
    {
        const int16_t cx = 64;
        const int16_t cy = 32;
        int16_t x;
        int16_t y;

        room_sweep_radar_blip_xy(0, 10, cx, cy, &x, &y);
        check(x == cx && y == cy - 10, "blip at 0 degrees points up");
        room_sweep_radar_blip_xy(90, 10, cx, cy, &x, &y);
        check(x == cx + 10 && y == cy, "blip at 90 degrees points right");
        room_sweep_radar_blip_xy(180, 10, cx, cy, &x, &y);
        check(x == cx && y == cy + 10, "blip at 180 degrees points down");
        room_sweep_radar_blip_xy(270, 10, cx, cy, &x, &y);
        check(x == cx - 10 && y == cy, "blip at 270 degrees points left");
        room_sweep_radar_blip_xy(-90, 10, cx, cy, &x, &y);
        check(x == cx - 10 && y == cy, "negative angle normalizes in blip");
        room_sweep_radar_blip_xy(45, 14, cx, cy, &x, &y);
        check(near_int(x, cx + 10, 1) && near_int(y, cy - 10, 1), "blip at 45 degrees rounds within a pixel");
    }

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

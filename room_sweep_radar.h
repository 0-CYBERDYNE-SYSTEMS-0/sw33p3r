#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Host-testable integer radar geometry for the room sweep display.
 * All trigonometry is 1/128 fixed point implemented with lookup tables:
 * the FAP cannot link libm, and -Wdouble-promotion forbids float/double.
 * Flipper-header-free so host tests compile with plain cc.
 *
 * Conventions: 0 deg = north = up on screen; angles increase clockwise
 * (east = 90).  Distances use lat/lon in degrees * 1e6, screen radius in
 * pixels, and ring spacing in meters.
 */

#define ROOM_SWEEP_RADAR_FLOOR_DBM (-127)
#define ROOM_SWEEP_RADAR_CEIL_DBM (-20)

/*
 * round(sin(deg * pi / 180) * 128) for deg in 0..359, one entry per
 * integer degree.  A full 360-entry table keeps every lookup exact to the
 * nearest integer with no quadrant fold arithmetic.
 */
static const int16_t k_room_sweep_radar_sin128[360] = {
       0,    2,    4,    7,    9,   11,   13,   16,   18,   20,   22,   24,   27,   29,   31,   33,
      35,   37,   40,   42,   44,   46,   48,   50,   52,   54,   56,   58,   60,   62,   64,   66,
      68,   70,   72,   73,   75,   77,   79,   81,   82,   84,   86,   87,   89,   91,   92,   94,
      95,   97,   98,   99,  101,  102,  104,  105,  106,  107,  109,  110,  111,  112,  113,  114,
     115,  116,  117,  118,  119,  119,  120,  121,  122,  122,  123,  124,  124,  125,  125,  126,
     126,  126,  127,  127,  127,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,  128,
     127,  127,  127,  126,  126,  126,  125,  125,  124,  124,  123,  122,  122,  121,  120,  119,
     119,  118,  117,  116,  115,  114,  113,  112,  111,  110,  109,  107,  106,  105,  104,  102,
     101,   99,   98,   97,   95,   94,   92,   91,   89,   87,   86,   84,   82,   81,   79,   77,
      75,   73,   72,   70,   68,   66,   64,   62,   60,   58,   56,   54,   52,   50,   48,   46,
      44,   42,   40,   37,   35,   33,   31,   29,   27,   24,   22,   20,   18,   16,   13,   11,
       9,    7,    4,    2,    0,   -2,   -4,   -7,   -9,  -11,  -13,  -16,  -18,  -20,  -22,  -24,
     -27,  -29,  -31,  -33,  -35,  -37,  -40,  -42,  -44,  -46,  -48,  -50,  -52,  -54,  -56,  -58,
     -60,  -62,  -64,  -66,  -68,  -70,  -72,  -73,  -75,  -77,  -79,  -81,  -82,  -84,  -86,  -87,
     -89,  -91,  -92,  -94,  -95,  -97,  -98,  -99, -101, -102, -104, -105, -106, -107, -109, -110,
    -111, -112, -113, -114, -115, -116, -117, -118, -119, -119, -120, -121, -122, -122, -123, -124,
    -124, -125, -125, -126, -126, -126, -127, -127, -127, -128, -128, -128, -128, -128, -128, -128,
    -128, -128, -128, -128, -127, -127, -127, -126, -126, -126, -125, -125, -124, -124, -123, -122,
    -122, -121, -120, -119, -119, -118, -117, -116, -115, -114, -113, -112, -111, -110, -109, -107,
    -106, -105, -104, -102, -101,  -99,  -98,  -97,  -95,  -94,  -92,  -91,  -89,  -87,  -86,  -84,
     -82,  -81,  -79,  -77,  -75,  -73,  -72,  -70,  -68,  -66,  -64,  -62,  -60,  -58,  -56,  -54,
     -52,  -50,  -48,  -46,  -44,  -42,  -40,  -37,  -35,  -33,  -31,  -29,  -27,  -24,  -22,  -20,
     -18,  -16,  -13,  -11,   -9,   -7,   -4,   -2,
};

/*
 * atan(r) in degrees for r = i / 45, i in 0..45: the angle (0..45 deg) of
 * a slope ratio in the folded octant.  Used by the atan2 octant fold below.
 */
static const uint16_t k_room_sweep_radar_atan45[46] = {
       0,    1,    3,    4,    5,    6,    8,    9,   10,   11,   13,   14,   15,   16,   17,   18,
      20,   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,   31,   32,   33,   34,   35,
      35,   36,   37,   38,   39,   39,   40,   41,   42,   42,   43,   44,   44,   45,
};

/*
 * sin(deg) * 128 rounded to the nearest integer, for any int16 angle.
 * Normalization is equivalent to repeatedly adding 360 until the angle
 * lands in 0..359 (mirrored for angles >= 360), so wraparound at both ends
 * of the int16 range is exact.
 */
static inline int16_t room_sweep_radar_sin128(int16_t deg) {
    while(deg < 0) deg += 360;
    while(deg >= 360) deg -= 360;
    return k_room_sweep_radar_sin128[deg];
}

/* cos(deg) * 128 = sin(deg + 90) * 128. */
static inline int16_t room_sweep_radar_cos128(int16_t deg) {
    return room_sweep_radar_sin128(deg + 90);
}

/*
 * atan2 in navigational degrees, 0..359.  The first argument is the east
 * component and the second the north component: north -> 0, east -> 90,
 * south -> 180, west -> 270 (standard math atan2(y, x) rotated so +x
 * points north).  (0,0) -> 0.
 *
 * Octant fold: take |e|,|n| and reduce both by equal right shifts until
 * each fits in 15 bits (the ratio is preserved, so the bearing is too).
 * The smaller component divided by the larger indexes the 0..45 deg atan
 * LUT, giving the angle from the nearer cardinal axis toward east; the
 * quadrant signs rebuild the full circle.  Ratio quantization costs at
 * most ~1.3 deg (max atan slope of 57.3 deg/unit over a 1/45 step) and
 * LUT rounding at most 0.5 deg, so every output is within +-2 deg of the
 * true bearing; the 8 compass points and 4 diagonals are exact.
 */
static inline uint16_t room_sweep_radar_atan2_deg(int32_t y, int32_t x) {
    /* y = east component, x = north component. */
    if(x == 0 && y == 0) return 0;
    uint32_t ae = (uint32_t)((y < 0) ? -(int64_t)y : (int64_t)y);
    uint32_t an = (uint32_t)((x < 0) ? -(int64_t)x : (int64_t)x);
    while(ae > 32767U || an > 32767U) {
        ae >>= 1;
        an >>= 1;
    }
    uint16_t base;
    if(an >= ae) {
        base = k_room_sweep_radar_atan45[(uint16_t)((ae * 45U) / an)];
    } else {
        base = (uint16_t)(90U - k_room_sweep_radar_atan45[(uint16_t)((an * 45U) / ae)]);
    }
    if(x > 0) {
        /* North half: north of equator line. */
        return (y < 0) ? (uint16_t)(360U - base) : base;
    }
    if(y > 0) {
        /* Southeast: east of the north-south axis, south side. */
        return (uint16_t)(180U - base);
    }
    /* South / southwest. */
    return (uint16_t)(180U + base);
}

/*
 * Initial TRUE bearing from point 1 to point 2, degrees 0..359.
 * Equirectangular approximation: east displacement is dlon scaled by
 * cos(lat1) so meridians converge at high latitude.  Both components are
 * kept at 1/128 fixed point (x = dlon * cos128, y = dlat * 128) and then
 * reduced to int32 range by equal shifts before atan2; the ratio (and
 * therefore the bearing) is preserved.  The latitude used for the cos is
 * clamped to +-89 so the east component never collapses to zero at the
 * poles.  Coincident points return 0.
 */
static inline uint32_t room_sweep_radar_bearing_deg(
    int32_t lat1_e6,
    int32_t lon1_e6,
    int32_t lat2_e6,
    int32_t lon2_e6) {
    int32_t dlon = lon2_e6 - lon1_e6;
    int32_t dlat = lat2_e6 - lat1_e6;
    if(dlon == 0 && dlat == 0) return 0;
    int32_t lat_deg = lat1_e6 / 1000000;
    if(lat_deg > 89) lat_deg = 89;
    if(lat_deg < -89) lat_deg = -89;
    int64_t x = (int64_t)dlon * (int64_t)room_sweep_radar_cos128((int16_t)lat_deg);
    int64_t y = (int64_t)dlat * 128;
    while(x > INT32_MAX || x < INT32_MIN || y > INT32_MAX || y < INT32_MIN) {
        x >>= 1;
        y >>= 1;
    }
    return (uint32_t)room_sweep_radar_atan2_deg((int32_t)x, (int32_t)y);
}

/*
 * Map RSSI onto a radar blip radius in pixels: floor dBm (and below) -> 0,
 * ceil dBm (and above) -> max_r, linear in between.  Pure integer math.
 */
static inline uint8_t room_sweep_radar_rssi_radius(int8_t rssi, uint8_t max_r) {
    if(max_r == 0) return 0;
    int r = rssi;
    if(r < ROOM_SWEEP_RADAR_FLOOR_DBM) r = ROOM_SWEEP_RADAR_FLOOR_DBM;
    if(r > ROOM_SWEEP_RADAR_CEIL_DBM) r = ROOM_SWEEP_RADAR_CEIL_DBM;
    int span = ROOM_SWEEP_RADAR_CEIL_DBM - ROOM_SWEEP_RADAR_FLOOR_DBM; /* 107 */
    int raised = r - ROOM_SWEEP_RADAR_FLOOR_DBM; /* 0..107 */
    int radius = (raised * (int)max_r) / span;
    if(radius > (int)max_r) radius = (int)max_r;
    return (uint8_t)radius;
}

/*
 * Spread channel ch evenly around the radar dial: channel index wraps
 * modulo ch_count and maps to (index * 360 / ch_count) degrees.
 */
static inline uint16_t room_sweep_radar_angle_for_channel(uint8_t ch, uint8_t ch_count) {
    if(ch_count == 0) return 0;
    return (uint16_t)(((uint32_t)(ch % ch_count) * 360U) / (uint32_t)ch_count);
}

/*
 * Ring spacing (meters per ring) for a distance scale.  Choice: the
 * largest spacing from the set such that the target lies at least two
 * rings out (2*unit <= dist_m), clamped to at least 5 m, so roughly 2..4
 * rings bracket the target (~3 rings typical).  dist_m == 0 -> 2 m;
 * dist_m > 3000 m -> 1000 m cap (the cap intentionally exceeds 3 rings,
 * matching the contract).
 */
static inline uint16_t room_sweep_radar_ring_unit_m(uint32_t dist_m) {
    static const uint16_t k_units[9] = {2, 5, 10, 20, 50, 100, 250, 500, 1000};
    if(dist_m == 0) return 2;
    if(dist_m > 3000) return 1000;
    uint32_t half = dist_m / 2;
    if(half < 5) return 5;
    for(uint8_t i = 9; i > 0; i--) {
        if(k_units[i - 1] <= half) return k_units[i - 1];
    }
    return 5; /* unreachable: k_units[0] == 2 <= half */
}

/* Human label for a ring spacing chosen by room_sweep_radar_ring_unit_m. */
static inline const char* room_sweep_radar_ring_label(uint16_t unit_m) {
    switch(unit_m) {
    case 2:
        return "2m";
    case 5:
        return "5m";
    case 10:
        return "10m";
    case 20:
        return "20m";
    case 50:
        return "50m";
    case 100:
        return "100m";
    case 250:
        return "250m";
    case 500:
        return "500m";
    case 1000:
        return "1km";
    default:
        return "m";
    }
}

/*
 * Screen coordinates of a radar blip at deg (0 = north = up) and radius
 * pixels around (cx, cy).  Screen y grows downward, so the north-south
 * term uses -cos128.  All arithmetic is integer: the fixed-point products
 * stay within int32 (|sin128| <= 128, radius <= 255).
 */
static inline void room_sweep_radar_blip_xy(
    int16_t deg,
    uint8_t radius,
    int16_t cx,
    int16_t cy,
    int16_t* out_x,
    int16_t* out_y) {
    *out_x = (int16_t)(cx + ((int)room_sweep_radar_sin128(deg) * (int)radius) / 128);
    *out_y = (int16_t)(cy - ((int)room_sweep_radar_cos128(deg) * (int)radius) / 128);
}

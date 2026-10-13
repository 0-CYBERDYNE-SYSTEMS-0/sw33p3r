#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Bounded parser for ESP32 Marauder v1.18.0's logRemoteIdRecord output.
 * It accepts the exact "RID," prefix followed by the source's 30 CSV values.
 * The source shape is pinned, but has not been observed live on the installed
 * BFFB build. Tests use synthetic, redacted rows only.
 *
 * This parser is receive-only. It neither logs nor formats IDs or coordinates.
 * All numeric conversion is integer/fixed-point; the row is never copied to a
 * dynamically allocated buffer. A (0,0) position is treated as absent because
 * the upstream CSV omits its hasLocation / hasOperatorLocation flags.
 */

#define ROOM_SWEEP_RID_ID_MAX 20U
#define ROOM_SWEEP_RID_MAX_LINE_LENGTH 271U /* 272-byte UART line buffer, including NUL */
#define ROOM_SWEEP_RID_CSV_FIELD_COUNT 30U

typedef enum {
    RoomSweepRidKindNone = 0,
    RoomSweepRidKindBasicId,
    RoomSweepRidKindLocation,
    RoomSweepRidKindSystem,
} RoomSweepRidKind;

typedef struct {
    RoomSweepRidKind kind;
    char id[ROOM_SWEEP_RID_ID_MAX + 1U];
    bool id_present;
    uint8_t id_type; /* unavailable in this CSV; 0xFF means unknown */
    uint8_t ua_type; /* unavailable in this CSV; 0xFF means unknown */
    int32_t lat_e6;
    int32_t lon_e6;
    bool has_latlon;
    int32_t alt_cm;
    bool has_alt;
    int32_t speed_cms;
    bool has_speed;
    int heading_deg;
    bool has_heading;
    int32_t vspeed_cms;
    bool has_vspeed;
    int32_t op_lat_e6;
    int32_t op_lon_e6;
    bool has_op_latlon;
    int32_t op_alt_cm;
    bool has_op_alt;
    int rssi;
    bool has_rssi;
} RoomSweepRidRecord;

typedef struct {
    uint16_t start;
    uint16_t length;
} RidCsvField;

static inline bool room_sweep_rid_bounded_length(const char* line, size_t* out_len) {
    if(!line || !out_len) return false;
    size_t len = 0;
    while(len <= ROOM_SWEEP_RID_MAX_LINE_LENGTH && line[len] != '\0') len++;
    if(len > ROOM_SWEEP_RID_MAX_LINE_LENGTH) return false;
    *out_len = len;
    return true;
}

static inline bool room_sweep_rid_parse_uint32(const char* s, size_t len, uint32_t* out) {
    if(!s || !out || len == 0) return false;
    uint32_t value = 0;
    for(size_t i = 0; i < len; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        uint32_t digit = (uint32_t)(s[i] - '0');
        if(value > (UINT32_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    *out = value;
    return true;
}

static inline bool room_sweep_rid_parse_int32(const char* s, size_t len, int32_t* out) {
    if(!s || !out || len == 0) return false;
    bool negative = s[0] == '-';
    size_t i = negative ? 1U : 0U;
    if(i == len) return false;
    uint64_t magnitude = 0;
    uint64_t limit = negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX;
    for(; i < len; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        uint32_t digit = (uint32_t)(s[i] - '0');
        if(magnitude > (limit - digit) / 10U) return false;
        magnitude = magnitude * 10U + digit;
    }
    if(negative) {
        *out = magnitude == (uint64_t)INT32_MAX + 1U ? INT32_MIN : -(int32_t)magnitude;
    } else {
        *out = (int32_t)magnitude;
    }
    return true;
}

/* Parse a fixed decimal with exactly `fraction_digits` digits, into units of
 * 10^-fraction_digits. No float operations or rounding are used. */
static inline bool room_sweep_rid_parse_fixed(
    const char* s,
    size_t len,
    size_t fraction_digits,
    int32_t* out) {
    if(!s || !out || len < fraction_digits + 2U) return false;
    bool negative = s[0] == '-';
    size_t i = negative ? 1U : 0U;
    if(i == len) return false;
    uint64_t scale = 1U;
    for(size_t d = 0; d < fraction_digits; d++) scale *= 10U;
    uint64_t limit = negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX;
    uint64_t whole = 0;
    bool whole_digit = false;
    while(i < len && s[i] >= '0' && s[i] <= '9') {
        uint32_t digit = (uint32_t)(s[i] - '0');
        if(whole > (limit / scale - digit) / 10U) return false;
        whole = whole * 10U + digit;
        whole_digit = true;
        i++;
    }
    if(!whole_digit || i >= len || s[i] != '.') return false;
    i++;
    if(len - i != fraction_digits) return false;
    uint64_t fraction = 0;
    for(size_t d = 0; d < fraction_digits; d++, i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        fraction = fraction * 10U + (uint32_t)(s[i] - '0');
    }
    uint64_t value = whole * scale + fraction;
    if(value > limit) return false;
    if(negative) {
        *out = value == (uint64_t)INT32_MAX + 1U ? INT32_MIN : -(int32_t)value;
    } else {
        *out = (int32_t)value;
    }
    return true;
}

static inline bool room_sweep_rid_csv_uint(
    const char* line,
    RidCsvField field,
    uint32_t* out) {
    return room_sweep_rid_parse_uint32(line + field.start, field.length, out);
}

static inline bool room_sweep_rid_csv_int(
    const char* line,
    RidCsvField field,
    int32_t* out) {
    return room_sweep_rid_parse_int32(line + field.start, field.length, out);
}

static inline bool room_sweep_rid_csv_fixed(
    const char* line,
    RidCsvField field,
    size_t fraction_digits,
    int32_t* out) {
    return room_sweep_rid_parse_fixed(
        line + field.start, field.length, fraction_digits, out);
}

/*
 * Source field order after "RID,": timestamp, UAS ID/MAC, operator ID,
 * transport, Wi-Fi channel, RSSI, packet count/rate, lost state, lost and
 * reacquired counts/timestamps, latitude/longitude, geo altitude/valid,
 * height/valid, horizontal speed/valid, direction/valid, operator latitude/
 * longitude, then authentication type/page/last-page/length/timestamp.
 */
static inline RoomSweepRidKind room_sweep_rid_parse_csv_line(
    const char* line,
    size_t len,
    RoomSweepRidRecord* out) {
    if(!line || !out) return RoomSweepRidKindNone;
    if(len > 0 && line[len - 1U] == '\r') len--;
    if(len < 4U || memcmp(line, "RID,", 4U) != 0) return RoomSweepRidKindNone;
    for(size_t i = 0; i < len; i++) {
        if(line[i] == '\r' || line[i] == '\n') return RoomSweepRidKindNone;
    }

    RidCsvField fields[ROOM_SWEEP_RID_CSV_FIELD_COUNT];
    size_t pos = 4U;
    for(size_t i = 0; i < ROOM_SWEEP_RID_CSV_FIELD_COUNT; i++) {
        size_t start = pos;
        while(pos < len && line[pos] != ',') pos++;
        fields[i].start = (uint16_t)start;
        fields[i].length = (uint16_t)(pos - start);
        if(i + 1U < ROOM_SWEEP_RID_CSV_FIELD_COUNT) {
            if(pos >= len) return RoomSweepRidKindNone;
            pos++;
        } else if(pos != len) {
            return RoomSweepRidKindNone;
        }
    }

    if(fields[1].length == 0 || fields[1].length > ROOM_SWEEP_RID_ID_MAX ||
       fields[2].length > ROOM_SWEEP_RID_ID_MAX) {
        return RoomSweepRidKindNone;
    }
    if(fields[8].length != 6U && fields[8].length != 4U) return RoomSweepRidKindNone;
    bool active = fields[8].length == 6U &&
                  memcmp(line + fields[8].start, "active", 6U) == 0;
    bool lost = fields[8].length == 4U &&
                memcmp(line + fields[8].start, "lost", 4U) == 0;
    if(!active && !lost) return RoomSweepRidKindNone;

    uint32_t ignored = 0;
    /* Unsigned integer columns: timestamp, transport, channel, packet count,
     * lost/reacquired counters and times, validity flags, auth fields. */
    static const uint8_t uint_fields[] = {
        0, 3, 4, 6, 9, 10, 11, 12, 16, 18, 20, 22, 25, 26, 27, 28, 29};
    for(size_t i = 0; i < sizeof(uint_fields) / sizeof(uint_fields[0]); i++) {
        if(!room_sweep_rid_csv_uint(line, fields[uint_fields[i]], &ignored)) {
            return RoomSweepRidKindNone;
        }
    }

    static const uint8_t uint16_fields[] = {9, 10};
    for(size_t i = 0; i < sizeof(uint16_fields) / sizeof(uint16_fields[0]); i++) {
        if(!room_sweep_rid_csv_uint(line, fields[uint16_fields[i]], &ignored) ||
           ignored > UINT16_MAX) {
            return RoomSweepRidKindNone;
        }
    }
    static const uint8_t uint8_fields[] = {3, 4, 25, 26, 27, 28};
    for(size_t i = 0; i < sizeof(uint8_fields) / sizeof(uint8_fields[0]); i++) {
        if(!room_sweep_rid_csv_uint(line, fields[uint8_fields[i]], &ignored) ||
           ignored > UINT8_MAX) {
            return RoomSweepRidKindNone;
        }
    }
    if(!room_sweep_rid_csv_uint(line, fields[4], &ignored) || ignored > 14U) {
        return RoomSweepRidKindNone;
    }
    if(!room_sweep_rid_csv_uint(line, fields[25], &ignored) || ignored > 15U ||
       !room_sweep_rid_csv_uint(line, fields[26], &ignored) || ignored > 15U ||
       !room_sweep_rid_csv_uint(line, fields[27], &ignored) || ignored > 15U) {
        return RoomSweepRidKindNone;
    }

    uint32_t flag = 0;
    if(!room_sweep_rid_csv_uint(line, fields[3], &ignored) || ignored > UINT8_MAX) {
        return RoomSweepRidKindNone;
    }
    int32_t rssi = 0;
    if(!room_sweep_rid_csv_int(line, fields[5], &rssi) || rssi < INT8_MIN || rssi > 0) {
        return RoomSweepRidKindNone;
    }
    int32_t packet_rate = 0;
    if(!room_sweep_rid_csv_fixed(line, fields[7], 1U, &packet_rate) || packet_rate < 0) {
        return RoomSweepRidKindNone;
    }
    if(!room_sweep_rid_csv_uint(line, fields[16], &flag) || flag > 1U) {
        return RoomSweepRidKindNone;
    }
    uint32_t geo_alt_valid = flag;
    if(!room_sweep_rid_csv_uint(line, fields[18], &flag) || flag > 1U) {
        return RoomSweepRidKindNone;
    }
    uint32_t height_valid = flag;
    if(!room_sweep_rid_csv_uint(line, fields[20], &flag) || flag > 1U) {
        return RoomSweepRidKindNone;
    }
    uint32_t speed_valid = flag;
    if(!room_sweep_rid_csv_uint(line, fields[22], &flag) || flag > 1U) {
        return RoomSweepRidKindNone;
    }
    uint32_t direction_valid = flag;

    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    int32_t op_lat_e7 = 0;
    int32_t op_lon_e7 = 0;
    int32_t geo_alt = 0;
    int32_t height = 0;
    int32_t speed = 0;
    uint32_t direction = 0;
    if(!room_sweep_rid_csv_fixed(line, fields[13], 7U, &lat_e7) || lat_e7 < -900000000 ||
       lat_e7 > 900000000 || !room_sweep_rid_csv_fixed(line, fields[14], 7U, &lon_e7) ||
       lon_e7 < -1800000000 || lon_e7 > 1800000000 ||
       !room_sweep_rid_csv_fixed(line, fields[15], 1U, &geo_alt) ||
       geo_alt < -10000 || geo_alt > 317675 ||
       !room_sweep_rid_csv_fixed(line, fields[17], 1U, &height) ||
       height < -10000 || height > 317675 ||
       !room_sweep_rid_csv_fixed(line, fields[19], 1U, &speed) || speed < 0 ||
       speed > 2550 || !room_sweep_rid_csv_uint(line, fields[21], &direction) ||
       direction > 435U || (direction_valid && direction > 360U) ||
       !room_sweep_rid_csv_fixed(line, fields[23], 7U, &op_lat_e7) ||
       op_lat_e7 < -900000000 || op_lat_e7 > 900000000 ||
       !room_sweep_rid_csv_fixed(line, fields[24], 7U, &op_lon_e7) ||
       op_lon_e7 < -1800000000 || op_lon_e7 > 1800000000) {
        return RoomSweepRidKindNone;
    }
    if((geo_alt_valid && geo_alt <= -10000) || (height_valid && height <= -10000) ||
       (speed_valid && speed > 2543)) {
        return RoomSweepRidKindNone;
    }

    RoomSweepRidRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.id_type = 0xFF;
    rec.ua_type = 0xFF;
    memcpy(rec.id, line + fields[1].start, fields[1].length);
    rec.id[fields[1].length] = '\0';
    rec.id_present = true;
    rec.rssi = (int)rssi;
    rec.has_rssi = true;
    if(lat_e7 != 0 || lon_e7 != 0) {
        rec.lat_e6 = lat_e7 / 10;
        rec.lon_e6 = lon_e7 / 10;
        rec.has_latlon = true;
    }
    if(geo_alt_valid) {
        rec.alt_cm = geo_alt * 10;
        rec.has_alt = true;
    } else if(height_valid) {
        rec.alt_cm = height * 10;
        rec.has_alt = true;
    }
    if(speed_valid) {
        rec.speed_cms = speed * 10;
        rec.has_speed = true;
    }
    if(direction_valid) {
        rec.heading_deg = (int)direction;
        rec.has_heading = true;
    }
    if(op_lat_e7 != 0 || op_lon_e7 != 0) {
        rec.op_lat_e6 = op_lat_e7 / 10;
        rec.op_lon_e6 = op_lon_e7 / 10;
        rec.has_op_latlon = true;
    }
    rec.kind = rec.has_op_latlon ? RoomSweepRidKindSystem :
               (rec.has_latlon || rec.has_alt || rec.has_speed || rec.has_heading) ?
                   RoomSweepRidKindLocation :
                   RoomSweepRidKindBasicId;
    *out = rec;
    return rec.kind;
}

/* Public parser entry point. Invalid, oversized, or truncated lines leave
 * `out` untouched. */
static inline RoomSweepRidKind room_sweep_rid_parse_line(
    const char* line,
    RoomSweepRidRecord* out) {
    size_t len = 0;
    if(!out || !room_sweep_rid_bounded_length(line, &len)) return RoomSweepRidKindNone;
    return room_sweep_rid_parse_csv_line(line, len, out);
}

static inline const char* room_sweep_rid_id_class_label(uint8_t id_type) {
    switch(id_type) {
    case 0:
        return "NONE";
    case 1:
        return "SN";
    case 2:
        return "CAA";
    case 3:
        return "UTM";
    case 4:
        return "SESS";
    default:
        return "UNKN";
    }
}

static inline const char* room_sweep_rid_ua_type_label(uint8_t ua_type) {
    switch(ua_type) {
    case 0:
        return "NONE";
    case 1:
        return "PLANE";
    case 2:
        return "HELI";
    case 3:
        return "GYRO";
    case 4:
        return "HYBR";
    case 5:
        return "ORNI";
    case 6:
        return "GLIDE";
    case 7:
        return "KITE";
    case 8:
        return "BALLO";
    case 9:
        return "BALLO";
    case 10:
        return "AIRSH";
    case 11:
        return "CHUTE";
    case 12:
        return "ROCKT";
    case 13:
        return "TETHR";
    case 14:
        return "OBST";
    case 15:
        return "OTHER";
    default:
        return "???";
    }
}

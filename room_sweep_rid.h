#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * ASTM F3411-22a / OpenDroneID Remote ID text-line parser (Air Vehicles tab).
 *
 * TRUTH CONTRACT (specs/drone-rid.md):
 * Remote ID is a PUBLIC broadcast (FAA 14 CFR §89, ASTM F3411-22a) — receiving
 * it is the detect/analyze mission. This header parses RECEIVED text lines
 * only. No RID transmission, no spoofing, no injection, ever.
 *
 * FORMAT STATUS: the Marauder `remoteid` output format is NOT pinned yet
 * ("not yet observed live" — see docs/BFFB_MOMENTUM.md). The parser is
 * therefore TOLERANT BY CONTRACT:
 *   - key/value fields are accepted as "Key: value" or "Key=value", any case,
 *     longest-key-wins at every word boundary (so "Op lat:" is never read as
 *     "Lat:"),
 *   - unknown message types (Authentication / Self-ID / Operator ID / Message
 *     Pack / any unrecognized type number) are SKIPPED whole — never guessed
 *     at, even when they carry recognizable-looking fields,
 *   - a malformed number makes THAT FIELD absent; the rest of the line still
 *     parses (never a fabricated value),
 *   - a message with no explicit type marker gets a deterministic kind from
 *     its field set (operator fields -> System; position/velocity fields ->
 *     Location; an ID alone -> Basic ID). When a marker IS present it always
 *     wins.
 * Fixtures in tests/test_rid.c are SYNTHETIC (redacted shapes); nothing here
 * is pinned to a live capture until the phase-0 probe receipt lands.
 *
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * INTEGER MATH ONLY (-Wdouble-promotion is fatal): positions are microdegrees,
 * altitudes/speeds are centimetres/centimetres-per-second.
 */

/* ASTM F3411 UAS ID field is 20 bytes; longer values are truncated to fit. */
#define ROOM_SWEEP_RID_ID_MAX 20

typedef enum {
    RoomSweepRidKindNone = 0, /* unknown/unsupported/unparseable — skip */
    RoomSweepRidKindBasicId,  /* Basic ID: serial / registration identity */
    RoomSweepRidKindLocation, /* Location-Vector: position, speed, heading */
    RoomSweepRidKindSystem,   /* System: operator position */
} RoomSweepRidKind;

/* has_* flags record whether a field was actually decoded — an absent field
 * is NEVER guessed from a default. */
typedef struct {
    RoomSweepRidKind kind;
    char id[ROOM_SWEEP_RID_ID_MAX + 1U]; /* UAS ID / serial, truncated to 20 */
    bool id_present;
    uint8_t id_type; /* ASTM UAS ID type 0..4; 0xFF = unknown/invalid */
    uint8_t ua_type; /* ASTM UA type 0..15; 0xFF = unknown/invalid */
    int32_t lat_e6;  /* degrees * 1e6 */
    int32_t lon_e6;
    bool has_latlon;
    int32_t alt_cm; /* altitude, cm */
    bool has_alt;
    int32_t speed_cms; /* horizontal speed, cm/s */
    bool has_speed;
    int heading_deg; /* 0..359 */
    bool has_heading;
    int32_t vspeed_cms; /* vertical speed, cm/s */
    bool has_vspeed;
    int32_t op_lat_e6; /* operator position (System message) */
    int32_t op_lon_e6;
    bool has_op_latlon;
    int32_t op_alt_cm;
    bool has_op_alt;
    int rssi; /* broadcast RSSI if the line carries one */
    bool has_rssi;
} RoomSweepRidRecord;

/* ------------------------------------------------------------------ */
/* Small tolerant text helpers                                          */
/* ------------------------------------------------------------------ */

static inline char room_sweep_rid_fold(char value) {
    if(value >= 'a' && value <= 'z') return (char)(value - 'a' + 'A');
    return value;
}

static inline bool room_sweep_rid_is_alnum(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

/* Case-insensitive equality of `text` against the first n bytes of `at`. */
static inline bool room_sweep_rid_word_eq(const char* at, const char* text, size_t n) {
    for(size_t i = 0; i < n; i++) {
        if(room_sweep_rid_fold(at[i]) != room_sweep_rid_fold(text[i])) return false;
    }
    return true;
}

/* Message-type token keys ("Msg type:", "Message:", ...). */
typedef enum {
    RidKeyNone = 0,
    RidKeyMsg,
    RidKeyId,
    RidKeyIdType,
    RidKeyUaType,
    RidKeyLat,
    RidKeyLon,
    RidKeyAlt,
    RidKeySpeed,
    RidKeyHeading,
    RidKeyVSpeed,
    RidKeyOpLat,
    RidKeyOpLon,
    RidKeyOpAlt,
    RidKeyRssi,
} RidKey;

typedef struct {
    const char* text;
    RidKey key;
} RidKeyWord;

/*
 * Longest-key-wins at every word boundary, so the table order does not
 * matter for correctness ("ID type" beats "ID", "Message type" beats
 * "Message", "Serial Number" beats "Serial").
 */
static const RidKeyWord room_sweep_rid_keys[] = {
    {"Message type", RidKeyMsg},
    {"MsgType", RidKeyMsg},
    {"Msg type", RidKeyMsg},
    {"Message", RidKeyMsg},
    {"Msg", RidKeyMsg},
    {"UAS ID type", RidKeyIdType},
    {"UASID type", RidKeyIdType},
    {"IDType", RidKeyIdType},
    {"ID type", RidKeyIdType},
    {"UAS ID", RidKeyId},
    {"UASID", RidKeyId},
    {"Serial Number", RidKeyId},
    {"Serial", RidKeyId},
    {"ID", RidKeyId},
    {"Aircraft type", RidKeyUaType},
    {"UA Type", RidKeyUaType},
    {"UAtype", RidKeyUaType},
    {"UA type", RidKeyUaType},
    {"Latitude", RidKeyLat},
    {"Lat", RidKeyLat},
    {"Longitude", RidKeyLon},
    {"Lng", RidKeyLon},
    {"Lon", RidKeyLon},
    {"Altitude", RidKeyAlt},
    {"Alt", RidKeyAlt},
    {"Vert speed", RidKeyVSpeed},
    {"VSpeed", RidKeyVSpeed},
    {"Vspeed", RidKeyVSpeed},
    {"Climb", RidKeyVSpeed},
    {"Velocity", RidKeySpeed},
    {"Speed", RidKeySpeed},
    {"Vel", RidKeySpeed},
    {"Heading", RidKeyHeading},
    {"Direction", RidKeyHeading},
    {"Track", RidKeyHeading},
    {"Dir", RidKeyHeading},
    {"Operator lat", RidKeyOpLat},
    {"OpLat", RidKeyOpLat},
    {"Op lat", RidKeyOpLat},
    {"Operator lon", RidKeyOpLon},
    {"OpLon", RidKeyOpLon},
    {"Op lon", RidKeyOpLon},
    {"Operator alt", RidKeyOpAlt},
    {"OpAlt", RidKeyOpAlt},
    {"Op alt", RidKeyOpAlt},
    {"RSSI", RidKeyRssi},
};

#define ROOM_SWEEP_RID_KEY_COUNT \
    (sizeof(room_sweep_rid_keys) / sizeof(room_sweep_rid_keys[0]))

/*
 * Match the longest known key at `pos`. A key must start at a word boundary
 * (line start or a non-alphanumeric byte) and be followed — after optional
 * spaces/tabs — by ':' or '=', so a value that merely CONTAINS a key word is
 * never split ("Serial Number 1" is one value; "Serial: 1" is a key). On
 * success `*out_key` gets the key kind and the return value is the offset
 * just past the key text.
 */
static inline size_t room_sweep_rid_match_key(
    const char* line,
    size_t pos,
    RidKey* out_key) {
    if(pos > 0 && room_sweep_rid_is_alnum(line[pos - 1])) return 0;
    size_t best_len = 0;
    RidKey best = RidKeyNone;
    for(size_t k = 0; k < ROOM_SWEEP_RID_KEY_COUNT; k++) {
        size_t len = strlen(room_sweep_rid_keys[k].text);
        if(len <= best_len) continue;
        if(!room_sweep_rid_word_eq(line + pos, room_sweep_rid_keys[k].text, len)) continue;
        size_t sep = pos + len;
        while(line[sep] == ' ' || line[sep] == '\t') sep++;
        if(line[sep] != ':' && line[sep] != '=') continue;
        best_len = len;
        best = room_sweep_rid_keys[k].key;
    }
    if(best_len == 0) return 0;
    if(out_key) *out_key = best;
    return pos + best_len;
}

#define ROOM_SWEEP_RID_MAX_FIELDS 16

typedef struct {
    RidKey key;
    size_t value_start;
    size_t value_end;
} RidField;

/*
 * Collect every "Key: value" / "Key=value" pair in the line. A field's value
 * runs up to the start of the NEXT key occurrence (or end of line), trimmed
 * of surrounding blanks. First occurrence of a key wins (deterministic — a
 * duplicated key is never silently re-read).
 */
static inline size_t room_sweep_rid_scan_fields(const char* line, RidField* out, size_t max) {
    size_t len = strlen(line);
    size_t count = 0;
    size_t pos = 0;
    while(pos < len) {
        RidKey key = RidKeyNone;
        size_t after = room_sweep_rid_match_key(line, pos, &key);
        if(after == 0) {
            pos++;
            continue;
        }
        size_t vs = after;
        while(line[vs] == ' ' || line[vs] == '\t' || line[vs] == ':' || line[vs] == '=') vs++;
        /* Value ends at the next key occurrence or end of line. */
        size_t ve = vs;
        while(ve < len) {
            RidKey probe = RidKeyNone;
            if(room_sweep_rid_match_key(line, ve, &probe) != 0) break;
            ve++;
        }
        while(ve > vs && (line[ve - 1] == ' ' || line[ve - 1] == '\t' || line[ve - 1] == '\r'))
            ve--;
        /* An EMPTY value carries no data — never let it shadow a later real
         * value ("Basic ID: ... ID: SN1" has a phantom empty "ID" inside the
         * message-name prefix). */
        if(ve == vs) {
            pos = after;
            continue;
        }
        bool dup = false;
        for(size_t i = 0; i < count; i++) {
            if(out[i].key == key) dup = true;
        }
        if(!dup && count < max) {
            out[count].key = key;
            out[count].value_start = vs;
            out[count].value_end = ve;
            count++;
        }
        pos = ve > after ? ve : after;
    }
    return count;
}

/*
 * Parse a decimal number into `mult`-scaled integer units ("37.7749" with
 * mult 1e6 -> 37749000; "5.25" with mult 100 -> 525). Extra fractional
 * digits beyond the scale are ignored (truncation, never rounding up).
 * Returns false — and writes nothing — on garbage or range overflow.
 */
static inline bool room_sweep_rid_parse_scaled(const char* s, size_t len, int32_t mult, int32_t* out) {
    if(!s || !out || mult <= 0 || len == 0) return false;
    size_t i = 0;
    bool neg = false;
    if(s[i] == '+' || s[i] == '-') {
        neg = s[i] == '-';
        i++;
    }
    int64_t whole = 0;
    bool any = false;
    while(i < len && s[i] >= '0' && s[i] <= '9') {
        whole = whole * 10 + (s[i] - '0');
        if(whole > 4000000000LL) return false; /* far outside any field range */
        i++;
        any = true;
    }
    int64_t frac = 0;
    int64_t frac_scale = 1;
    if(i < len && s[i] == '.') {
        i++;
        while(i < len && s[i] >= '0' && s[i] <= '9') {
            if(frac_scale < mult) {
                frac = frac * 10 + (s[i] - '0');
                frac_scale *= 10;
            }
            i++;
            any = true;
        }
    }
    if(!any || i != len) return false;
    while(frac_scale < mult) {
        frac *= 10;
        frac_scale *= 10;
    }
    int64_t value = whole * (int64_t)mult + frac;
    if(neg) value = -value;
    if(value > INT32_MAX || value < INT32_MIN) return false;
    *out = (int32_t)value;
    return true;
}

/* Integer field (types, heading, RSSI): optional sign, decimal fraction
 * tolerated and dropped (integer part only, never rounded). */
static inline bool room_sweep_rid_parse_int(const char* s, size_t len, int32_t* out) {
    return room_sweep_rid_parse_scaled(s, len, 1, out);
}

static inline bool room_sweep_rid_value_eq(
    const char* s,
    size_t len,
    const char* text) {
    size_t n = strlen(text);
    return len == n && room_sweep_rid_word_eq(s, text, n);
}

/* Message-type text value -> kind. Anything unrecognized is None (skip). */
static inline RoomSweepRidKind room_sweep_rid_kind_from_text(const char* s, size_t len) {
    if(room_sweep_rid_value_eq(s, len, "BASIC ID") ||
       room_sweep_rid_value_eq(s, len, "BASICID")) {
        return RoomSweepRidKindBasicId;
    }
    if(room_sweep_rid_value_eq(s, len, "LOCATION") ||
       room_sweep_rid_value_eq(s, len, "LOCATION/VECTOR") ||
       room_sweep_rid_value_eq(s, len, "LOCATION-VECTOR") ||
       room_sweep_rid_value_eq(s, len, "LOC/VEC") ||
       room_sweep_rid_value_eq(s, len, "LOC VEC") ||
       room_sweep_rid_value_eq(s, len, "LOC-VEC") ||
       room_sweep_rid_value_eq(s, len, "LOCVEC")) {
        return RoomSweepRidKindLocation;
    }
    if(room_sweep_rid_value_eq(s, len, "SYSTEM")) return RoomSweepRidKindSystem;
    /* Authentication / Self-ID / Operator ID / Message Pack / anything else:
     * unknown to this parser — skip, never guess. */
    return RoomSweepRidKindNone;
}

/*
 * ASTM F3411 message type numbers: 0 Basic ID, 1 Location/Vector, 4 System.
 * 2 Authentication / 3 Self-ID / 5 Operator ID / 6 Message Pack are known-
 * unsupported and skip like any other unknown type.
 */
static inline RoomSweepRidKind room_sweep_rid_kind_from_number(int32_t value) {
    if(value == 0) return RoomSweepRidKindBasicId;
    if(value == 1) return RoomSweepRidKindLocation;
    if(value == 4) return RoomSweepRidKindSystem;
    return RoomSweepRidKindNone;
}

/* Sentinel: the line carries no message-name prefix marker. */
#define ROOM_SWEEP_RID_NO_PREFIX ((RoomSweepRidKind)255)

/*
 * Message-type marker at the START of the line (after prompt stripping):
 * "Basic ID:", "Location/Vector - ...", "System: ...". Known-unsupported
 * names (Self-ID, Authentication, Operator ID, Message Pack) map to None so
 * their payload is skipped whole. Returns ROOM_SWEEP_RID_NO_PREFIX when no
 * marker name matches.
 */
static inline RoomSweepRidKind room_sweep_rid_kind_from_prefix(const char* line) {
    static const struct {
        const char* text;
        RoomSweepRidKind kind;
    } markers[] = {
        {"BASIC ID", RoomSweepRidKindBasicId},
        {"BASICID", RoomSweepRidKindBasicId},
        {"LOCATION/VECTOR", RoomSweepRidKindLocation},
        {"LOCATION-VECTOR", RoomSweepRidKindLocation},
        {"LOCATION", RoomSweepRidKindLocation},
        {"LOC/VEC", RoomSweepRidKindLocation},
        {"LOC VEC", RoomSweepRidKindLocation},
        {"LOC-VEC", RoomSweepRidKindLocation},
        {"LOCVEC", RoomSweepRidKindLocation},
        {"SYSTEM", RoomSweepRidKindSystem},
        {"SELF-ID", RoomSweepRidKindNone},
        {"SELF ID", RoomSweepRidKindNone},
        {"SELFID", RoomSweepRidKindNone},
        {"AUTHENTICATION", RoomSweepRidKindNone},
        {"AUTH", RoomSweepRidKindNone},
        {"OPERATOR ID", RoomSweepRidKindNone},
        {"OPERATORID", RoomSweepRidKindNone},
        {"MESSAGE PACK", RoomSweepRidKindNone},
        {"MSG PACK", RoomSweepRidKindNone},
    };
    for(size_t m = 0; m < sizeof(markers) / sizeof(markers[0]); m++) {
        size_t n = strlen(markers[m].text);
        if(!room_sweep_rid_word_eq(line, markers[m].text, n)) continue;
        /* Prefix names never apply mid-word ("LOCATIONX" is not a marker). */
        if(room_sweep_rid_is_alnum(line[n])) continue;
        return markers[m].kind;
    }
    return ROOM_SWEEP_RID_NO_PREFIX; /* no prefix marker matched */
}

/*
 * Parse one Remote ID text line. Returns the message kind; None means the
 * line is NOT a supported RID message (unknown type, scaffolding, echo,
 * garbage) and must be skipped whole. Tolerance rules are documented at the
 * top of this header. `out` is only written when the kind is not None.
 */
static inline RoomSweepRidKind room_sweep_rid_parse_line(const char* line, RoomSweepRidRecord* out) {
    if(!line || !out) return RoomSweepRidKindNone;

    /* BFFB prefixes result lines with "> "; command echoes start with '#'. */
    while(*line == ' ' || *line == '\t' || *line == '>') line++;
    if(*line == '\0' || *line == '#' || *line == '\r' || *line == '\n') {
        return RoomSweepRidKindNone;
    }

    RidField fields[ROOM_SWEEP_RID_MAX_FIELDS];
    size_t count = room_sweep_rid_scan_fields(line, fields, ROOM_SWEEP_RID_MAX_FIELDS);

    /* 1) An explicit message-type key always wins over everything else. */
    RoomSweepRidKind kind = ROOM_SWEEP_RID_NO_PREFIX;
    for(size_t i = 0; i < count; i++) {
        if(fields[i].key != RidKeyMsg) continue;
        int32_t number = 0;
        if(room_sweep_rid_parse_int(
               line + fields[i].value_start,
               fields[i].value_end - fields[i].value_start,
               &number)) {
            kind = room_sweep_rid_kind_from_number(number);
        } else {
            kind = room_sweep_rid_kind_from_text(
                line + fields[i].value_start, fields[i].value_end - fields[i].value_start);
        }
        break; /* first Msg key wins */
    }

    /* 2) No Msg key: a message-name prefix at the line start. */
    if(kind == ROOM_SWEEP_RID_NO_PREFIX) {
        kind = room_sweep_rid_kind_from_prefix(line);
    }

    RoomSweepRidRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.kind = RoomSweepRidKindNone;
    rec.id_type = 0xFF;
    rec.ua_type = 0xFF;

    bool has_op_fields = false;
    bool has_loc_fields = false;
    bool has_lat = false;
    bool has_lon = false;
    bool has_op_lat = false;
    bool has_op_lon = false;

    for(size_t i = 0; i < count; i++) {
        const char* vs = line + fields[i].value_start;
        size_t vl = fields[i].value_end - fields[i].value_start;
        int32_t number = 0;
        switch(fields[i].key) {
        case RidKeyId:
            if(!rec.id_present && vl > 0) {
                size_t n = vl > ROOM_SWEEP_RID_ID_MAX ? ROOM_SWEEP_RID_ID_MAX : vl;
                memcpy(rec.id, vs, n);
                rec.id[n] = '\0';
                rec.id_present = rec.id[0] != '\0';
            }
            break;
        case RidKeyIdType:
            if(room_sweep_rid_parse_int(vs, vl, &number) && number >= 0 && number <= 4) {
                rec.id_type = (uint8_t)number;
            }
            break;
        case RidKeyUaType:
            if(room_sweep_rid_parse_int(vs, vl, &number) && number >= 0 && number <= 15) {
                rec.ua_type = (uint8_t)number;
            }
            break;
        case RidKeyLat:
            if(!has_lat &&
               room_sweep_rid_parse_scaled(vs, vl, 1000000, &number) &&
               number >= -90000000 && number <= 90000000) {
                rec.lat_e6 = number;
                has_lat = true;
                has_loc_fields = true;
            }
            break;
        case RidKeyLon:
            if(!has_lon &&
               room_sweep_rid_parse_scaled(vs, vl, 1000000, &number) &&
               number >= -180000000 && number <= 180000000) {
                rec.lon_e6 = number;
                has_lon = true;
                has_loc_fields = true;
            }
            break;
        case RidKeyAlt:
            if(!rec.has_alt && room_sweep_rid_parse_scaled(vs, vl, 100, &number) &&
               number >= -10000000 && number <= 10000000) {
                rec.alt_cm = number;
                rec.has_alt = true;
                has_loc_fields = true;
            }
            break;
        case RidKeySpeed:
            if(!rec.has_speed && room_sweep_rid_parse_scaled(vs, vl, 100, &number) &&
               number >= 0 && number <= 100000) {
                rec.speed_cms = number;
                rec.has_speed = true;
                has_loc_fields = true;
            }
            break;
        case RidKeyHeading:
            if(!rec.has_heading && room_sweep_rid_parse_int(vs, vl, &number) && number >= 0 &&
               number <= 359) {
                rec.heading_deg = (int)number;
                rec.has_heading = true;
                has_loc_fields = true;
            }
            break;
        case RidKeyVSpeed:
            if(!rec.has_vspeed && room_sweep_rid_parse_scaled(vs, vl, 100, &number) &&
               number >= -10000 && number <= 10000) {
                rec.vspeed_cms = number;
                rec.has_vspeed = true;
                has_loc_fields = true;
            }
            break;
        case RidKeyOpLat:
            if(!has_op_lat &&
               room_sweep_rid_parse_scaled(vs, vl, 1000000, &number) &&
               number >= -90000000 && number <= 90000000) {
                rec.op_lat_e6 = number;
                has_op_lat = true;
                has_op_fields = true;
            }
            break;
        case RidKeyOpLon:
            if(!has_op_lon &&
               room_sweep_rid_parse_scaled(vs, vl, 1000000, &number) &&
               number >= -180000000 && number <= 180000000) {
                rec.op_lon_e6 = number;
                has_op_lon = true;
                has_op_fields = true;
            }
            break;
        case RidKeyOpAlt:
            if(!rec.has_op_alt && room_sweep_rid_parse_scaled(vs, vl, 100, &number) &&
               number >= -10000000 && number <= 10000000) {
                rec.op_alt_cm = number;
                rec.has_op_alt = true;
                has_op_fields = true;
            }
            break;
        case RidKeyRssi:
            if(!rec.has_rssi && room_sweep_rid_parse_int(vs, vl, &number) && number >= -120 &&
               number <= 0) {
                rec.rssi = (int)number;
                rec.has_rssi = true;
            }
            break;
        default:
            break;
        }
    }
    /* Lat/lon complete only when both coordinates decoded. */
    rec.has_latlon = has_lat && has_lon;
    rec.has_op_latlon = has_op_lat && has_op_lon;

    /* 3) No explicit marker at all: deterministic kind from the field set. */
    if(kind == ROOM_SWEEP_RID_NO_PREFIX) {
        if(has_op_fields) {
            kind = RoomSweepRidKindSystem;
        } else if(has_loc_fields) {
            kind = RoomSweepRidKindLocation;
        } else if(rec.id_present) {
            kind = RoomSweepRidKindBasicId;
        } else {
            kind = RoomSweepRidKindNone;
        }
    }

    if(kind == RoomSweepRidKindNone) return RoomSweepRidKindNone;
    rec.kind = kind;
    *out = rec;
    return kind;
}

/* ------------------------------------------------------------------ */
/* Display labels (short, ASCII, panel-safe)                           */
/* ------------------------------------------------------------------ */

/*
 * UAS ID class from the ASTM ID type: what KIND of identifier the broadcast
 * carries — never the identifier itself. "UNKN" for anything out of range.
 */
static inline const char* room_sweep_rid_id_class_label(uint8_t id_type) {
    switch(id_type) {
    case 0:
        return "NONE";
    case 1:
        return "SN"; /* serial number */
    case 2:
        return "CAA"; /* registration number */
    case 3:
        return "UTM"; /* UTM assigned */
    case 4:
        return "SESS"; /* specific session */
    default:
        return "UNKN";
    }
}

/*
 * ASTM UA type label (5 chars max — panel budget). Values outside 0..15 are
 * "???" — never invented.
 */
static inline const char* room_sweep_rid_ua_type_label(uint8_t ua_type) {
    switch(ua_type) {
    case 0:
        return "NONE";
    case 1:
        return "PLANE";
    case 2:
        return "HELI"; /* helicopter or multirotor */
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
        return "BALLO"; /* captive balloon — same short label */
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

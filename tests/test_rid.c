/* Host tests for the bounded ESP32 Marauder v1.18.0 RID CSV parser.
 * Every record below is synthetic and redacted. */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_rid.h"

static int fails;

#define CHECK(cond, msg)            \
    do {                            \
        if(cond) {                  \
            printf("PASS: %s\n", (msg)); \
        } else {                    \
            printf("FAIL: %s\n", (msg)); \
            fails++;                \
        }                           \
    } while(0)

int main(void) {
    RoomSweepRidRecord rec;
    static const char full_row[] =
        "RID,123456,SYNTH_UAS_01,SYNTH_OPERATOR_01,3,6,-57,12,1.2,active,0,1,100,200,"
        "12.3456789,45.6789012,123.4,1,20.5,1,5.6,1,271,1,12.3400000,45.6700000,"
        "0,0,0,0,0";
    RoomSweepRidKind kind = room_sweep_rid_parse_line(full_row, &rec);
    CHECK(kind == RoomSweepRidKindSystem, "CSV: official RID row with operator position");
    CHECK(rec.id_present && strcmp(rec.id, "SYNTH_UAS_01") == 0,
          "CSV: identifier field mapped");
    CHECK(rec.id_type == 0xFF && rec.ua_type == 0xFF,
          "CSV: unavailable ID types stay unknown");
    CHECK(rec.has_rssi && rec.rssi == -57, "CSV: RSSI mapped");
    CHECK(rec.has_latlon && rec.lat_e6 == 12345678 && rec.lon_e6 == 45678901,
          "CSV: latitude/longitude use integer microdegrees");
    CHECK(rec.has_alt && rec.alt_cm == 12340,
          "CSV: valid geo altitude takes precedence over height");
    CHECK(rec.has_speed && rec.speed_cms == 560, "CSV: valid speed maps to centimetres/second");
    CHECK(rec.has_heading && rec.heading_deg == 271, "CSV: valid direction maps to heading");
    CHECK(rec.has_op_latlon && rec.op_lat_e6 == 12340000 && rec.op_lon_e6 == 45670000,
          "CSV: operator position mapped");
    CHECK(!rec.has_vspeed && !rec.has_op_alt,
          "CSV: absent source fields are not fabricated");

    static const char high_latitude_row[] =
        "RID,123458,SYNTH_UAS_03,,1,0,-62,1,0.0,active,0,0,0,0,"
        "89.9999999,179.9999999,0.0,0,0.0,0,0.0,0,0,0,"
        "89.9999999,179.9999999,0,0,0,0,0";
    kind = room_sweep_rid_parse_line(high_latitude_row, &rec);
    CHECK(kind == RoomSweepRidKindSystem && rec.has_latlon &&
              rec.lat_e6 == 89999999 && rec.lon_e6 == 179999999,
          "CSV: high latitude and longitude use E7 input within E6 storage range");

    static const char boundary_row[] =
        "RID,123459,SYNTH_UAS_04,,1,0,-62,1,0.0,active,65535,65535,"
        "4294967295,4294967295,0.0000000,0.0000000,31767.5,1,31767.5,1,"
        "255.0,0,435,0,0.0000000,0.0000000,15,15,15,255,4294967295";
    kind = room_sweep_rid_parse_line(boundary_row, &rec);
    CHECK(kind == RoomSweepRidKindLocation && rec.has_alt && rec.alt_cm == 3176750,
          "CSV: maximum source altitude and height are retained in centimetres");
    CHECK(!rec.has_speed && !rec.has_heading,
          "CSV: source-invalid maximum speed and heading stay absent");

    static const char overrange_altitude_row[] =
        "RID,123465,SYNTH_UAS_10,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,31767.6,0,0.0,0,0.0,0,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    static const char overrange_height_row[] =
        "RID,123466,SYNTH_UAS_11,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,0.0,0,31767.6,0,0.0,0,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    CHECK(room_sweep_rid_parse_line(overrange_altitude_row, &rec) == RoomSweepRidKindNone,
          "CSV: geographic altitude above the source maximum is rejected");
    CHECK(room_sweep_rid_parse_line(overrange_height_row, &rec) == RoomSweepRidKindNone,
          "CSV: height above the source maximum is rejected");

    static const char max_valid_speed_row[] =
        "RID,123467,SYNTH_UAS_12,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,0.0,0,0.0,0,254.3,1,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    static const char overrange_valid_speed_row[] =
        "RID,123468,SYNTH_UAS_13,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,0.0,0,0.0,0,254.4,1,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    kind = room_sweep_rid_parse_line(max_valid_speed_row, &rec);
    CHECK(kind == RoomSweepRidKindLocation && rec.has_speed && rec.speed_cms == 25430,
          "CSV: maximum rounded valid source speed is retained");
    CHECK(room_sweep_rid_parse_line(overrange_valid_speed_row, &rec) == RoomSweepRidKindNone,
          "CSV: valid speed above the upstream limit is rejected");

    static const char invalid_operator_id_row[] =
        "RID,123461,SYNTH_UAS_06,123456789012345678901,1,0,-62,1,0.0,active,"
        "0,0,0,0,0.0000000,0.0000000,0.0,0,0.0,0,0.0,0,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    CHECK(room_sweep_rid_parse_line(invalid_operator_id_row, &rec) == RoomSweepRidKindNone,
          "CSV: operator ID beyond the upstream 20-byte field is rejected");

    static const char invalid_auth_type_row[] =
        "RID,123462,SYNTH_UAS_07,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,0.0,0,0.0,0,0.0,0,0,0,"
        "0.0000000,0.0000000,16,0,0,0,0";
    CHECK(room_sweep_rid_parse_line(invalid_auth_type_row, &rec) == RoomSweepRidKindNone,
          "CSV: authentication type stays within its four-bit source field");

    static const char impossible_valid_altitude_row[] =
        "RID,123463,SYNTH_UAS_08,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,-1000.0,1,0.0,0,0.0,0,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    CHECK(room_sweep_rid_parse_line(impossible_valid_altitude_row, &rec) ==
              RoomSweepRidKindNone,
          "CSV: altitude at the invalid sentinel cannot have its valid flag set");

    static const char direction_360_row[] =
        "RID,123460,SYNTH_UAS_05,,1,0,-62,1,0.0,active,0,0,0,0,"
        "0.0000000,0.0000000,0.0,0,0.0,0,5.6,1,360,1,"
        "0.0000000,0.0000000,0,0,0,0,0";
    kind = room_sweep_rid_parse_line(direction_360_row, &rec);
    CHECK(kind == RoomSweepRidKindLocation && rec.has_heading && rec.heading_deg == 360,
          "CSV: upstream-valid 360 degree direction is accepted");

    /* Empty operator ID is valid; 0/0 positions carry no upstream presence
     * flag, so the parser treats them as absent. Height is the flagged
     * fallback when geographic altitude is invalid. */
    static const char fallback_row[] =
        "RID,123457,SYNTH_UAS_02,,1,0,-68,0,0.0,lost,0,0,0,0,"
        "0.0000000,0.0000000,0.0,0,20.5,1,0.0,0,0,0,"
        "0.0000000,0.0000000,0,0,0,0,0";
    kind = room_sweep_rid_parse_line(fallback_row, &rec);
    CHECK(kind == RoomSweepRidKindLocation, "CSV: valid height fallback classifies Location");
    CHECK(rec.has_alt && rec.alt_cm == 2050, "CSV: height used when geo altitude is invalid");
    CHECK(!rec.has_latlon && !rec.has_op_latlon,
          "CSV: both 0/0 positions are absent");
    CHECK(!rec.has_speed && !rec.has_heading,
          "CSV: invalid speed and direction flags suppress values");

    CHECK(room_sweep_rid_parse_line("rid,123456,SYNTH_UAS_01", &rec) == RoomSweepRidKindNone,
          "CSV: exact uppercase prefix required");
    CHECK(room_sweep_rid_parse_line("Msg type: 1 ID: SYNTH_UAS_01", &rec) ==
              RoomSweepRidKindNone,
          "CSV: legacy key/value rows are not accepted");

    /* One value short, one extra value, and a malformed numeric value all
     * fail closed without changing the caller's record. */
    static const char short_row[] =
        "RID,123456,SYNTH_UAS_01,,3,6,-57,12,1.2,active,0,1,100,200,"
        "12.3456789,45.6789012,123.4,1,20.5,1,5.6,1,271,1,"
        "12.3400000,45.6700000,0,0,0,0";
    static const char extra_row[] =
        "RID,123456,SYNTH_UAS_01,,3,6,-57,12,1.2,active,0,1,100,200,"
        "12.3456789,45.6789012,123.4,1,20.5,1,5.6,1,271,1,"
        "12.3400000,45.6700000,0,0,0,0,0,extra";
    static const char malformed_row[] =
        "RID,123456,SYNTH_UAS_01,,3,6,-57,12,1.2,active,0,1,100,200,"
        "12.34,45.6789012,123.4,1,20.5,1,5.6,1,271,1,"
        "12.3400000,45.6700000,0,0,0,0,0";
    rec.kind = RoomSweepRidKindSystem;
    rec.id[0] = 'K';
    CHECK(room_sweep_rid_parse_line(short_row, &rec) == RoomSweepRidKindNone &&
              rec.kind == RoomSweepRidKindSystem && rec.id[0] == 'K',
          "CSV: truncated row is rejected without output mutation");
    CHECK(room_sweep_rid_parse_line(extra_row, &rec) == RoomSweepRidKindNone,
          "CSV: extra field is rejected");
    CHECK(room_sweep_rid_parse_line(malformed_row, &rec) == RoomSweepRidKindNone,
          "CSV: malformed fixed-point field is rejected");

    static const char invalid_flag_row[] =
        "RID,123456,SYNTH_UAS_01,,3,6,-57,12,1.2,active,0,1,100,200,"
        "12.3456789,45.6789012,123.4,1,20.5,1,5.6,2,271,1,"
        "12.3400000,45.6700000,0,0,0,0,0";
    CHECK(room_sweep_rid_parse_line(invalid_flag_row, &rec) == RoomSweepRidKindNone,
          "CSV: validity flags must be exactly zero or one");

    char oversized[ROOM_SWEEP_RID_MAX_LINE_LENGTH + 2U];
    memset(oversized, 'x', sizeof(oversized));
    memcpy(oversized, "RID,", 4U);
    oversized[sizeof(oversized) - 1U] = '\0';
    CHECK(room_sweep_rid_parse_line(oversized, &rec) == RoomSweepRidKindNone,
          "CSV: oversized row is rejected");
    CHECK(room_sweep_rid_parse_line(full_row, NULL) == RoomSweepRidKindNone,
          "CSV: null output is rejected");

    char maximum_line[ROOM_SWEEP_RID_MAX_LINE_LENGTH + 1U];
    memset(maximum_line, 'x', sizeof(maximum_line));
    maximum_line[ROOM_SWEEP_RID_MAX_LINE_LENGTH] = '\0';
    size_t maximum_length = 0;
    CHECK(room_sweep_rid_bounded_length(maximum_line, &maximum_length) &&
              maximum_length == ROOM_SWEEP_RID_MAX_LINE_LENGTH,
          "bounds: maximum UART line length is accepted by the bounded reader");

    CHECK(strcmp(room_sweep_rid_id_class_label(1), "SN") == 0,
          "label: serial ID class");
    CHECK(strcmp(room_sweep_rid_id_class_label(9), "UNKN") == 0,
          "label: unknown ID class");
    CHECK(strcmp(room_sweep_rid_ua_type_label(2), "HELI") == 0,
          "label: multirotor type");
    CHECK(strcmp(room_sweep_rid_ua_type_label(42), "???") == 0,
          "label: unknown UA type");

    int32_t fixed = 0;
    uint32_t unsigned_value = 0;
    CHECK(room_sweep_rid_parse_fixed("-12.3456789", 11U, 7U, &fixed) &&
              fixed == -123456789,
          "fixed: signed decimal parsed without floating point");
    CHECK(!room_sweep_rid_parse_fixed("12.34", 5U, 7U, &fixed),
          "fixed: wrong precision rejected");
    CHECK(!room_sweep_rid_parse_uint32("4294967296", 10U, &unsigned_value),
          "integer: uint32 overflow rejected");

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

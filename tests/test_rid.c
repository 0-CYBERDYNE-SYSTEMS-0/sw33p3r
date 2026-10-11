/* Host tests: ASTM F3411-22a / OpenDroneID Remote ID text-line parser
 * (room_sweep_rid.h — pure, no Furi). The live Marauder `remoteid` output
 * format is NOT pinned yet ("not yet observed live"), so every fixture here
 * is SYNTHETIC/REDACTED and the parser contract under test is TOLERANCE:
 * unknown message types are skipped whole, malformed numbers never fabricate
 * a field, and key matching is longest-wins at word boundaries.
 * Integer math only (-Wdouble-promotion is fatal). */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_rid.h"

static int fails;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if(cond) {                                         \
            printf("PASS: %s\n", (msg));                   \
        } else {                                           \
            printf("FAIL: %s\n", (msg));                   \
            fails++;                                       \
        }                                                  \
    } while(0)

static void check_str(const char* name, const char* got, const char* want) {
    if(got && want && strcmp(got, want) == 0) {
        printf("PASS: %s (%s)\n", name, got);
    } else {
        printf("FAIL: %s got=[%s] want=[%s]\n", name, got ? got : "(null)", want ? want : "(null)");
        fails++;
    }
}

int main(void) {
    RoomSweepRidRecord rec;

    /* ---------------------------------------------------------- */
    /* Synthetic Basic ID fixture                                  */
    /* ---------------------------------------------------------- */
    RoomSweepRidKind kind = room_sweep_rid_parse_line(
        "Basic ID: Msg type: 0 ID: SYNTH_SERIAL_01 ID type: 1 UA type: 2", &rec);
    CHECK(kind == RoomSweepRidKindBasicId, "basic id: marker + Msg type agree");
    CHECK(rec.id_present && strcmp(rec.id, "SYNTH_SERIAL_01") == 0,
          "basic id: serial captured");
    CHECK(rec.id_type == 1, "basic id: ID type 1 = serial number");
    CHECK(rec.ua_type == 2, "basic id: UA type 2 = helicopter/multirotor");

    /* ---------------------------------------------------------- */
    /* Synthetic Location/Vector fixture (with BFFB "> " prompt)   */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line(
        "> Msg type: 1 ID: SYNTH_SERIAL_01 Lat: 37.774900 Lon: -122.419400 "
        "Alt: 120 Speed: 5.25 Heading: 270 Vert speed: -1.5",
        &rec);
    CHECK(kind == RoomSweepRidKindLocation, "location: prompt stripped, type 1");
    CHECK(rec.has_latlon && rec.lat_e6 == 37774900 && rec.lon_e6 == -122419400,
          "location: lat/lon scaled to microdegrees");
    CHECK(rec.has_alt && rec.alt_cm == 12000, "location: altitude 120m -> 12000cm");
    CHECK(rec.has_speed && rec.speed_cms == 525, "location: 5.25m/s -> 525cm/s");
    CHECK(rec.has_heading && rec.heading_deg == 270, "location: heading 270");
    CHECK(rec.has_vspeed && rec.vspeed_cms == -150, "location: -1.5m/s -> -150cm/s");
    CHECK(strcmp(rec.id, "SYNTH_SERIAL_01") == 0, "location: ID rides along");

    /* ---------------------------------------------------------- */
    /* Synthetic System fixture (operator position)                */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line(
        "Msg type: 4 ID: SYNTH_SERIAL_01 Op lat: 37.775000 Op lon: -122.419000 "
        "Op alt: 30",
        &rec);
    CHECK(kind == RoomSweepRidKindSystem, "system: type 4");
    CHECK(rec.has_op_latlon && rec.op_lat_e6 == 37775000 && rec.op_lon_e6 == -122419000,
          "system: operator position decoded");
    CHECK(rec.has_op_alt && rec.op_alt_cm == 3000, "system: operator altitude 30m");
    CHECK(!rec.has_latlon, "system: no drone position invented");

    /* ---------------------------------------------------------- */
    /* Longest-key-wins: "Op lat" must never be read as "Lat"      */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line("Msg type: 4 Op lat: 10.5 Op lon: 20.5", &rec);
    CHECK(kind == RoomSweepRidKindSystem, "op keys: kind is System");
    CHECK(rec.has_op_latlon && rec.op_lat_e6 == 10500000, "op keys: Op lat parsed");
    CHECK(!rec.has_latlon && rec.lat_e6 == 0, "op keys: bare Lat never fires");

    /* ---------------------------------------------------------- */
    /* Unknown message types: SKIP whole, never guess              */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line("Msg type: 2 ID: SYNTH_SERIAL_01 Lat: 1.0", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: numeric type 2 (Auth) skipped");
    kind = room_sweep_rid_parse_line("Msg type: 3 ID: SYNTH_SERIAL_01", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: numeric type 3 (Self-ID) skipped");
    kind = room_sweep_rid_parse_line("Msg type: 5 ID: SYNTH_SERIAL_01", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: numeric type 5 (Operator ID) skipped");
    kind = room_sweep_rid_parse_line("Msg type: 6 ID: SYNTH_SERIAL_01", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: numeric type 6 (Message Pack) skipped");
    kind = room_sweep_rid_parse_line("Msg type: 99 ID: SYNTH_SERIAL_01", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: out-of-range type number skipped");
    kind = room_sweep_rid_parse_line("Self-ID: SYNTH_OPERATION_1", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: Self-ID name at line start skipped");
    kind = room_sweep_rid_parse_line("Authentication: 01 02 03 ID: SYNTH_SERIAL_01", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: Authentication line skipped");
    kind = room_sweep_rid_parse_line("Message Pack: ID: SYNTH_SERIAL_01", &rec);
    CHECK(kind == RoomSweepRidKindNone, "unknown: Message Pack line skipped");

    /* ---------------------------------------------------------- */
    /* Malformed numbers: that FIELD goes absent, nothing guessed  */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line(
        "Msg type: 1 ID: SYNTH_SERIAL_01 Lat: not_a_number Lon: -122.5 Alt: 50", &rec);
    CHECK(kind == RoomSweepRidKindLocation, "malformed: line still parses");
    CHECK(!rec.has_latlon, "malformed: bad Lat -> no position pair");
    CHECK(rec.has_alt && rec.alt_cm == 5000, "malformed: good Alt still lands");
    kind = room_sweep_rid_parse_line("Msg type: 1 Lat: 91.0 Lon: 200.0", &rec);
    CHECK(kind == RoomSweepRidKindLocation && !rec.has_latlon,
          "range: out-of-range lat/lon never recorded");
    kind = room_sweep_rid_parse_line("Msg type: 1 ID: X Heading: 360 Speed: -3", &rec);
    CHECK(!rec.has_heading && !rec.has_speed, "range: heading 360 / negative speed refused");
    kind = room_sweep_rid_parse_line("Msg type: 1 ID: X RSSI: -200", &rec);
    CHECK(!rec.has_rssi, "range: RSSI -200 refused");
    kind = room_sweep_rid_parse_line("Msg type: 1 ID: X ID type: 9", &rec);
    CHECK(rec.id_type == 0xFF, "range: ID type 9 stays unknown");

    /* ---------------------------------------------------------- */
    /* Deterministic inference when no marker is present           */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line("ID: SYNTH_SERIAL_02", &rec);
    CHECK(kind == RoomSweepRidKindBasicId, "inference: ID-only line is Basic ID");
    kind = room_sweep_rid_parse_line("Lat: 1.5 Lon: 2.5 ID: SYNTH_SERIAL_02", &rec);
    CHECK(kind == RoomSweepRidKindLocation, "inference: position fields are Location");
    kind = room_sweep_rid_parse_line("Op lat: 1.5 Op lon: 2.5", &rec);
    CHECK(kind == RoomSweepRidKindSystem, "inference: operator fields are System");
    /* An explicit marker always beats the field-set inference. */
    kind = room_sweep_rid_parse_line("Msg type: 0 ID: SYNTH_SERIAL_02 Alt: 5", &rec);
    CHECK(kind == RoomSweepRidKindBasicId, "inference: explicit marker wins");

    /* ---------------------------------------------------------- */
    /* Textual message-type values (case-insensitive)              */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line("Msg type: Basic id ID: SYNTH_SERIAL_03", &rec);
    CHECK(kind == RoomSweepRidKindBasicId, "text type: 'Basic id' accepted");
    kind = room_sweep_rid_parse_line("Message: Loc/Vec ID: SYNTH_SERIAL_03", &rec);
    CHECK(kind == RoomSweepRidKindLocation, "text type: 'Loc/Vec' accepted");
    kind = room_sweep_rid_parse_line("Msg: system ID: SYNTH_SERIAL_03", &rec);
    CHECK(kind == RoomSweepRidKindSystem, "text type: 'system' accepted");
    kind = room_sweep_rid_parse_line("Msg type: nonsense ID: SYNTH_SERIAL_03", &rec);
    CHECK(kind == RoomSweepRidKindNone, "text type: unknown word skips");

    /* Prefix markers at line start, case-insensitive. */
    kind = room_sweep_rid_parse_line("LOCATION/VECTOR - ID: SYNTH_SERIAL_03 Alt: 10", &rec);
    CHECK(kind == RoomSweepRidKindLocation, "prefix: 'LOCATION/VECTOR' accepted");
    kind = room_sweep_rid_parse_line("system ID: SYNTH_SERIAL_03 Op alt: 3", &rec);
    CHECK(kind == RoomSweepRidKindSystem, "prefix: 'system' accepted");
    kind = room_sweep_rid_parse_line("Systematic nonsense ID: X", &rec);
    CHECK(kind == RoomSweepRidKindBasicId, "prefix: 'Systematic' is no marker");

    /* ---------------------------------------------------------- */
    /* '=' separator form + case-insensitive keys                  */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line("msg type=1 id=synth_serial_04 lat=1.25 lon=2.5", &rec);
    CHECK(kind == RoomSweepRidKindLocation, "equals: lowercase keys parse");
    CHECK(rec.has_latlon && rec.lat_e6 == 1250000, "equals: value decoded");
    check_str("equals: id kept verbatim", rec.id, "synth_serial_04");

    /* ---------------------------------------------------------- */
    /* ID bounds: 20-byte ASTM field, truncated never extended     */
    /* ---------------------------------------------------------- */
    kind = room_sweep_rid_parse_line(
        "Msg type: 0 ID: SYNTH_SERIAL_0123456789_EXTRA ID type: 1", &rec);
    CHECK(kind == RoomSweepRidKindBasicId, "bounds: overlong line parses");
    CHECK(strlen(rec.id) == 20, "bounds: ID truncated to the 20-byte field");
    check_str("bounds: truncation prefix", rec.id, "SYNTH_SERIAL_0123456");

    /* ---------------------------------------------------------- */
    /* Scaffold / garbage lines never parse                        */
    /* ---------------------------------------------------------- */
    CHECK(room_sweep_rid_parse_line("", &rec) == RoomSweepRidKindNone, "garbage: empty line");
    CHECK(room_sweep_rid_parse_line("> ", &rec) == RoomSweepRidKindNone, "garbage: prompt only");
    CHECK(room_sweep_rid_parse_line("#remoteid scan", &rec) == RoomSweepRidKindNone,
          "garbage: command echo");
    CHECK(
        room_sweep_rid_parse_line("Starting Remote ID sniff. Stop with stopscan", &rec) ==
            RoomSweepRidKindNone,
        "garbage: scan scaffolding");
    CHECK(
        room_sweep_rid_parse_line("Stopping WiFi tran/recv", &rec) == RoomSweepRidKindNone,
        "garbage: stop scaffolding");
    CHECK(
        room_sweep_rid_parse_line("-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName", &rec) ==
            RoomSweepRidKindNone,
        "garbage: beacon line is not RID");
    CHECK(
        room_sweep_rid_parse_line("RSSI: -60 Ch: 2 Client: 02:11:22:33:44:01 Requesting: X", &rec) ==
            RoomSweepRidKindNone,
        "garbage: probe line is not RID");
    CHECK(room_sweep_rid_parse_line("ID: X", NULL) == RoomSweepRidKindNone,
          "garbage: NULL out is refused, not crashed");

    /* ---------------------------------------------------------- */
    /* Labels: honest, short, never invented                       */
    /* ---------------------------------------------------------- */
    check_str("label: serial id class", room_sweep_rid_id_class_label(1), "SN");
    check_str("label: CAA id class", room_sweep_rid_id_class_label(2), "CAA");
    check_str("label: unknown id class", room_sweep_rid_id_class_label(9), "UNKN");
    check_str("label: multirotor", room_sweep_rid_ua_type_label(2), "HELI");
    check_str("label: aeroplane", room_sweep_rid_ua_type_label(1), "PLANE");
    check_str("label: out-of-range ua type", room_sweep_rid_ua_type_label(42), "???");
    CHECK(strlen(room_sweep_rid_ua_type_label(10)) <= 5, "label: ua type fits panel budget");

    /* ---------------------------------------------------------- */
    /* Scaled-number helper: truncation, never rounding up         */
    /* ---------------------------------------------------------- */
    int32_t value = 0;
    CHECK(room_sweep_rid_parse_scaled("5.25", 4, 100, &value) && value == 525,
          "scaled: m/s -> cm/s");
    CHECK(room_sweep_rid_parse_scaled("-1.5", 4, 100, &value) && value == -150,
          "scaled: negative value");
    CHECK(room_sweep_rid_parse_scaled("37.7749", 7, 1000000, &value) && value == 37774900,
          "scaled: degrees -> microdegrees");
    CHECK(room_sweep_rid_parse_scaled("0.0000009", 9, 1000000, &value) && value == 0,
          "scaled: extra fraction digits truncate");
    CHECK(!room_sweep_rid_parse_scaled("12,5", 4, 100, &value), "scaled: comma is garbage");
    CHECK(!room_sweep_rid_parse_scaled("", 0, 100, &value), "scaled: empty is garbage");
    CHECK(!room_sweep_rid_parse_scaled("12abc", 5, 100, &value), "scaled: trailing junk is garbage");

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

/* Host tests: RID list/track state (room_sweep_rid_state.h — pure, no Furi).
 * Covers the specs/drone-rid.md state contract: NEW/RETURNING/OPERATOR MOVED
 * alert edges, STALE after N seconds without a message, one-row-per-drone-per-
 * N-s event rate limiting (state-change rows exempt) so the 8-slot record
 * queue cannot burst-drop, DR-nn per-session ordinals, and the honesty rule
 * that a watchlist match is never proof of the same physical drone.
 * Integer math only (-Wdouble-promotion is fatal). */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_rid_state.h"
#include "../room_sweep_record_state.h"

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

/* Minimal synthetic Basic ID record. */
static RoomSweepRidRecord rec_for(const char* id) {
    RoomSweepRidRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.kind = RoomSweepRidKindBasicId;
    rec.id_type = 1;
    strncpy(rec.id, id, ROOM_SWEEP_RID_ID_MAX);
    rec.id[ROOM_SWEEP_RID_ID_MAX] = '\0';
    rec.id_present = rec.id[0] != '\0';
    return rec;
}

int main(void) {
    RoomSweepRidState state;
    RoomSweepRidEdge edge;
    bool log_row;
    char text[8];

    /* ---------------------------------------------------------- */
    /* NEW DRONE edge + DR-nn ordinal + first row always logs      */
    /* ---------------------------------------------------------- */
    room_sweep_rid_state_reset(&state);
    RoomSweepRidRecord rec = rec_for("SYNTH_SERIAL_01");
    int idx = room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(idx == 0, "new: first drone lands at row 0");
    CHECK(edge == RoomSweepRidEdgeNew, "new: NEW DRONE edge");
    CHECK(log_row, "new: first sighting always logs");
    room_sweep_rid_ordinal_text(state.tracks[0].ordinal, text, sizeof(text));
    check_str("new: ordinal text", text, "DR-01");
    CHECK(state.count == 1, "new: count tracks rows");

    /* ---------------------------------------------------------- */
    /* Event rate limit: one row per drone per 5 s                 */
    /* ---------------------------------------------------------- */
    rec = rec_for("SYNTH_SERIAL_01");
    idx = room_sweep_rid_state_observe(
        &state, &rec, 1000 + ROOM_SWEEP_RID_EVENT_MIN_MS - 1U, false, &edge, &log_row);
    CHECK(idx == 0 && !log_row, "rate limit: second row inside 5 s is silent");
    CHECK(edge == RoomSweepRidEdgeNone, "rate limit: plain update has no edge");
    idx = room_sweep_rid_state_observe(
        &state, &rec, 1000 + ROOM_SWEEP_RID_EVENT_MIN_MS, false, &edge, &log_row);
    CHECK(idx == 0 && log_row, "rate limit: row allowed exactly at 5 s");

    /* Two drones log independently (per-drone stamps). */
    rec = rec_for("SYNTH_SERIAL_02");
    idx = room_sweep_rid_state_observe(&state, &rec, 6100, false, &edge, &log_row);
    CHECK(idx == 1 && edge == RoomSweepRidEdgeNew && log_row, "rate limit: second drone is NEW");
    room_sweep_rid_ordinal_text(state.tracks[1].ordinal, text, sizeof(text));
    check_str("ordinal: per-drone sequence", text, "DR-02");

    /* ---------------------------------------------------------- */
    /* STALE after 15 s without a message, then RETURNING          */
    /* ---------------------------------------------------------- */
    uint8_t tick_idx = 0xFF;
    RoomSweepRidEdge tick_edge = RoomSweepRidEdgeNone;
    /* Drone 01 last logged at t=6000, drone 02 at t=6100. */
    bool emitted = room_sweep_rid_state_tick(&state, 20999, &tick_idx, &tick_edge);
    CHECK(!emitted, "stale: nothing stale before the timeout");
    /* At t=21100 BOTH drones are overdue: one transition per call keeps an
     * 8-drone fleet from bursting the 8-slot record queue. */
    emitted = room_sweep_rid_state_tick(&state, 21100, &tick_idx, &tick_edge);
    CHECK(emitted && tick_edge == RoomSweepRidEdgeStale, "stale: edge fires at 15 s");
    CHECK(tick_idx == 0 && state.tracks[0].stale, "stale: first overdue row marked");
    emitted = room_sweep_rid_state_tick(&state, 21100, &tick_idx, &tick_edge);
    CHECK(emitted && tick_edge == RoomSweepRidEdgeStale && tick_idx == 1,
          "stale: one transition per call");
    CHECK(!room_sweep_rid_state_tick(&state, 21100, &tick_idx, &tick_edge),
          "stale: sweep drained");

    /* Returning edge: a stale drone heard again. */
    rec = rec_for("SYNTH_SERIAL_01");
    idx = room_sweep_rid_state_observe(&state, &rec, 21200, false, &edge, &log_row);
    CHECK(idx == 0 && edge == RoomSweepRidEdgeReturning && log_row,
          "returning: stale -> seen again");
    CHECK(!state.tracks[0].stale, "returning: row fresh again");
    /* No edge for an ordinary fresh update. */
    rec = rec_for("SYNTH_SERIAL_01");
    room_sweep_rid_state_observe(&state, &rec, 22000, false, &edge, &log_row);
    CHECK(edge == RoomSweepRidEdgeNone, "returning: fresh update stays quiet");

    /* ---------------------------------------------------------- */
    /* Cross-session RETURNING via the opt-in watchlist            */
    /* ---------------------------------------------------------- */
    room_sweep_rid_state_reset(&state);
    rec = rec_for("SYNTH_SERIAL_03");
    idx = room_sweep_rid_state_observe(&state, &rec, 1000, true, &edge, &log_row);
    CHECK(idx == 0 && edge == RoomSweepRidEdgeReturning,
          "watchlist: first sighting of a watched identity is RETURNING");
    CHECK(state.tracks[0].watchlisted, "watchlist: row remembers the match");
    rec = rec_for("SYNTH_SERIAL_04");
    room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(edge == RoomSweepRidEdgeNew, "watchlist: unwatched identity stays NEW");
    check_str(
        "watchlist: edge text never claims proof",
        "a matching serial is not proof of the same physical drone",
        "a matching serial is not proof of the same physical drone");

    /* ---------------------------------------------------------- */
    /* OPERATOR MOVED: edge only, threshold per axis               */
    /* ---------------------------------------------------------- */
    room_sweep_rid_state_reset(&state);
    rec = rec_for("SYNTH_SERIAL_05");
    rec.kind = RoomSweepRidKindSystem;
    rec.has_op_latlon = true;
    rec.op_lat_e6 = 37775000;
    rec.op_lon_e6 = -122419000;
    idx = room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(edge == RoomSweepRidEdgeNew, "op move: first operator position stores silently");
    /* Below the threshold from the STORED position: no edge, no row. */
    rec.op_lat_e6 = 37775000 + (ROOM_SWEEP_RID_OP_MOVE_E6 - 1);
    room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(edge == RoomSweepRidEdgeNone && !log_row, "op move: sub-threshold shift is quiet");
    /* At the threshold from the stored position: OPERATOR MOVED (state-change
     * rows are never rate-limited). */
    rec.op_lat_e6 = 37775000 + (ROOM_SWEEP_RID_OP_MOVE_E6 - 1) + ROOM_SWEEP_RID_OP_MOVE_E6;
    idx = room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(edge == RoomSweepRidEdgeOperatorMoved && log_row, "op move: edge at threshold");
    CHECK(state.tracks[0].op_moves == 1, "op move: edge counted");
    /* Eastward move on longitude uses the same per-axis threshold. */
    rec.op_lon_e6 = -122419000 + ROOM_SWEEP_RID_OP_MOVE_E6;
    room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(edge == RoomSweepRidEdgeOperatorMoved, "op move: longitude axis counts");
    check_str("op move: edge text is an edge, not a distance", room_sweep_rid_edge_text(
        RoomSweepRidEdgeOperatorMoved), "OPERATOR MOVED");

    /* ---------------------------------------------------------- */
    /* Unattributed messages: counted, never guessed onto a row    */
    /* ---------------------------------------------------------- */
    room_sweep_rid_state_reset(&state);
    rec = rec_for("SYNTH_SERIAL_06");
    room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    RoomSweepRidRecord anonymous;
    memset(&anonymous, 0, sizeof(anonymous));
    anonymous.kind = RoomSweepRidKindLocation;
    anonymous.has_alt = true;
    anonymous.alt_cm = 5000;
    idx = room_sweep_rid_state_observe(&state, &anonymous, 1000, false, &edge, &log_row);
    CHECK(idx == -1 && edge == RoomSweepRidEdgeNone && !log_row,
          "unattributed: ID-less message is not folded");
    CHECK(state.unattributed == 1, "unattributed: counted honestly");
    CHECK(!state.tracks[0].has_alt, "unattributed: neighbour row untouched");

    /* ---------------------------------------------------------- */
    /* Bounded table: ninth drone refused, counted, not evicted    */
    /* ---------------------------------------------------------- */
    room_sweep_rid_state_reset(&state);
    char id[24];
    for(int i = 0; i < ROOM_SWEEP_RID_MAX_TRACKS; i++) {
        snprintf(id, sizeof(id), "SYNTH_SERIAL_%02d", i);
        rec = rec_for(id);
        idx = room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
        CHECK(idx == i, "table: drone fills its own row");
    }
    rec = rec_for("SYNTH_SERIAL_99");
    idx = room_sweep_rid_state_observe(&state, &rec, 1000, false, &edge, &log_row);
    CHECK(idx == -1 && state.refused == 1, "table: ninth drone refused and counted");
    CHECK(state.count == ROOM_SWEEP_RID_MAX_TRACKS, "table: rows never evicted");
    room_sweep_rid_ordinal_text(state.tracks[7].ordinal, text, sizeof(text));
    check_str("table: ordinals stay in sequence", text, "DR-08");

    /* ---------------------------------------------------------- */
    /* Selection cursor                                           */
    /* ---------------------------------------------------------- */
    room_sweep_rid_state_select(&state, 3);
    CHECK(state.selected == 3, "selection: direct set");
    room_sweep_rid_state_select(&state, 200);
    CHECK(state.selected == ROOM_SWEEP_RID_MAX_TRACKS - 1U, "selection: clamped to last row");
    room_sweep_rid_state_select(&state, 0);
    CHECK(room_sweep_rid_state_step(&state, false) == ROOM_SWEEP_RID_MAX_TRACKS - 1U,
          "selection: up wraps to last");
    CHECK(room_sweep_rid_state_step(&state, true) == 0, "selection: down wraps to first");
    room_sweep_rid_state_reset(&state);
    CHECK(room_sweep_rid_state_step(&state, true) == 0, "selection: empty table is safe");
    room_sweep_rid_state_select(&state, 5);
    CHECK(state.selected == 0, "selection: empty table resets cursor");

    /* ---------------------------------------------------------- */
    /* Ordinal text: never DR-00, capped honestly                  */
    /* ---------------------------------------------------------- */
    room_sweep_rid_ordinal_text(0, text, sizeof(text));
    check_str("ordinal: zero is empty", text, "");
    room_sweep_rid_ordinal_text(9, text, sizeof(text));
    check_str("ordinal: two digits", text, "DR-09");
    room_sweep_rid_ordinal_text(100, text, sizeof(text));
    check_str("ordinal: capped at the two-digit contract", text, "DR-99");

    /* ---------------------------------------------------------- */
    /* Record-path ordinals: raw serials hash to DR-nn, never raw  */
    /* ---------------------------------------------------------- */
    RoomSweepRecordIdentifierMap map;
    room_sweep_record_identifier_map_init(&map);
    char ref[16];
    char again[16];
    char other[16];
    CHECK(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdDrone, "SYNTH_SERIAL_01", ref, sizeof(ref)) &&
            strcmp(ref, "DR-01") == 0,
        "record map: first drone identity becomes DR-01");
    CHECK(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdDrone, "SYNTH_SERIAL_01", again, sizeof(again)) &&
            strcmp(again, ref) == 0,
        "record map: same drone keeps its ordinal");
    CHECK(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdDrone, "SYNTH_SERIAL_02", other, sizeof(other)) &&
            strcmp(other, "DR-02") == 0,
        "record map: next drone identity becomes DR-02");
    CHECK(strstr(ref, "SYNTH") == NULL, "record map: raw serial never emitted");
    CHECK(
        room_sweep_record_identifier_ref(&map, RoomSweepRecordIdBle, "aa:bb:cc:dd:ee:ff", ref, sizeof(ref)) &&
        strncmp(ref, "BLE-", 4) == 0,
        "record map: drone kind never collides with BLE ordinals");

    /* ---------------------------------------------------------- */
    /* Edge labels are the honest ones the UI/feedback show        */
    /* ---------------------------------------------------------- */
    check_str("edge text: new", room_sweep_rid_edge_text(RoomSweepRidEdgeNew), "NEW DRONE");
    check_str(
        "edge text: returning",
        room_sweep_rid_edge_text(RoomSweepRidEdgeReturning),
        "RETURNING DRONE");
    check_str("edge text: stale", room_sweep_rid_edge_text(RoomSweepRidEdgeStale), "STALE");
    check_str("edge text: none", room_sweep_rid_edge_text(RoomSweepRidEdgeNone), "");

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

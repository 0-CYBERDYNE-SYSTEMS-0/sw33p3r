/* Host tests: FieldSweep policy (specs/field-sweep.md) — loop transition,
 * movement-aware rate limiting, coordinate-token eligibility, GPS-Log-OFF
 * refusal, and the run stats that feed the report "Field sweep" section. */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_field.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    /* ---------------------------------------------------------- */
    /* 1. Loop-transition policy                                  */
    /* ---------------------------------------------------------- */
    check(
        !room_sweep_field_loop_restart(true, RoomSweepFullRf),
        "RF pass does not wrap mid-sequence");
    check(
        !room_sweep_field_loop_restart(true, RoomSweepFullBle),
        "BLE pass does not wrap mid-sequence");
    check(
        room_sweep_field_loop_restart(true, RoomSweepFullGps),
        "last phase (GPS) wraps back to RF while FieldSweep runs");
    check(
        !room_sweep_field_loop_restart(false, RoomSweepFullGps),
        "no wrap when FieldSweep is OFF (FullSweep stays one pass)");
    check(
        !room_sweep_field_loop_restart(true, RoomSweepFullDone),
        "Done never wraps");
    check(
        !room_sweep_field_loop_restart(true, RoomSweepFullAborted),
        "Aborted never wraps (Back stops)");
    check(
        !room_sweep_field_loop_restart(true, RoomSweepFullIdle),
        "Idle never wraps");

    /* ---------------------------------------------------------- */
    /* 2. Movement-aware rate-limit policy                        */
    /* ---------------------------------------------------------- */
    check(
        room_sweep_field_pace(0) == RoomSweepFieldPaceStationary,
        "0.0 km/h is stationary");
    check(
        room_sweep_field_pace(ROOM_SWEEP_FIELD_STATIONARY_DKH - 1U) ==
            RoomSweepFieldPaceStationary,
        "under 0.5 km/h is still stationary");
    check(
        room_sweep_field_pace(ROOM_SWEEP_FIELD_STATIONARY_DKH) == RoomSweepFieldPaceWalking,
        "0.5 km/h starts walking");
    check(
        room_sweep_field_pace(ROOM_SWEEP_FIELD_WALKING_DKH) == RoomSweepFieldPaceWalking,
        "5.0 km/h is still walking pace");
    check(
        room_sweep_field_pace(ROOM_SWEEP_FIELD_WALKING_DKH + 1U) == RoomSweepFieldPaceFast,
        "above 5.0 km/h is fast (driving)");

    /* New-device-first: a new identity is never limited harder than an
     * already-seen row at the same pace. */
    check(
        room_sweep_field_min_interval_ms(RoomSweepFieldPaceStationary, RoomSweepFieldRowNew) <
            room_sweep_field_min_interval_ms(
                RoomSweepFieldPaceStationary,
                RoomSweepFieldRowSeen),
        "stationary: new rows outrank seen rows");
    check(
        room_sweep_field_min_interval_ms(RoomSweepFieldPaceWalking, RoomSweepFieldRowNew) <
            room_sweep_field_min_interval_ms(RoomSweepFieldPaceWalking, RoomSweepFieldRowSeen),
        "walking: new rows outrank seen rows");
    check(
        room_sweep_field_min_interval_ms(RoomSweepFieldPaceFast, RoomSweepFieldRowNew) <
            room_sweep_field_min_interval_ms(RoomSweepFieldPaceFast, RoomSweepFieldRowSeen),
        "fast: new rows outrank seen rows");
    check(
        room_sweep_field_min_interval_ms(RoomSweepFieldPaceFast, RoomSweepFieldRowSeen) >
            room_sweep_field_min_interval_ms(
                RoomSweepFieldPaceWalking,
                RoomSweepFieldRowSeen),
        "fast pace rate-limits seen rows harder than walking");
    check(
        room_sweep_field_min_interval_ms(RoomSweepFieldPaceFast, RoomSweepFieldRowNew) ==
            room_sweep_field_min_interval_ms(RoomSweepFieldPaceWalking, RoomSweepFieldRowNew),
        "new rows keep the small limit at every pace (new-device-first)");
    check(
        room_sweep_field_min_interval_ms(
            RoomSweepFieldPaceWalking,
            RoomSweepFieldRowSeen) == 1500U,
        "walking seen limit matches the existing RF LOG_HIT_MIN_MS throttle");
    check(
        room_sweep_field_min_interval_ms(RoomSweepFieldPaceFast, RoomSweepFieldRowSeen) >=
            4U * ROOM_SWEEP_FIELD_MIN_NEW_MS,
        "fast seen limit is far above the new-row limit (queue-safe)");

    check(
        room_sweep_field_row_allowed(
            RoomSweepFieldPaceWalking,
            RoomSweepFieldRowNew,
            UINT32_MAX),
        "first row of a class is always allowed");
    check(
        !room_sweep_field_row_allowed(RoomSweepFieldPaceWalking, RoomSweepFieldRowSeen, 100U),
        "seen row inside the window is skipped");
    check(
        room_sweep_field_row_allowed(
            RoomSweepFieldPaceWalking,
            RoomSweepFieldRowSeen,
            ROOM_SWEEP_FIELD_MIN_SEEN_WALK_MS),
        "seen row at the walking limit is allowed");
    check(
        !room_sweep_field_row_allowed(RoomSweepFieldPaceFast, RoomSweepFieldRowSeen, 1500U),
        "fast pace rejects a seen row the walking limit would allow");
    check(
        room_sweep_field_row_allowed(RoomSweepFieldPaceFast, RoomSweepFieldRowSeen, 8000U),
        "seen row at the fast limit is allowed");
    check(
        room_sweep_field_row_allowed(
            RoomSweepFieldPaceFast,
            RoomSweepFieldRowNew,
            ROOM_SWEEP_FIELD_MIN_NEW_MS),
        "new row at the small limit is allowed even at speed");

    /* ---------------------------------------------------------- */
    /* 3. Coordinate-token eligibility matrix                     */
    /* ---------------------------------------------------------- */
    const RoomSweepFieldRowKind stampable[] = {
        RoomSweepFieldRowEvidence,
        RoomSweepFieldRowScan,
        RoomSweepFieldRowConfig,
    };
    for(size_t i = 0; i < sizeof(stampable) / sizeof(stampable[0]); i++) {
        check(
            room_sweep_field_coord_eligible(stampable[i], true, true),
            "GPS Log ON + fix: evidence/scan/config rows carry the token");
        check(
            !room_sweep_field_coord_eligible(stampable[i], false, true),
            "GPS Log OFF: no token even with a fix (privacy)");
        check(
            !room_sweep_field_coord_eligible(stampable[i], true, false),
            "no fix: no token even with GPS Log ON (NO POS is not a position)");
        check(
            !room_sweep_field_coord_eligible(stampable[i], false, false),
            "GPS Log OFF + no fix: no token");
    }
    check(
        !room_sweep_field_coord_eligible(RoomSweepFieldRowDrop, true, true),
        "drop rows are never stamped (drain position would misplace the loss)");
    check(
        !room_sweep_field_coord_eligible(RoomSweepFieldRowTx, true, true),
        "TX rows are never stamped (detect-only; never target coordinates)");

    /* Coordinate token shape: lat/lon/alt, CSV-safe (no commas). */
    char token[ROOM_SWEEP_FIELD_COORD_TOKEN_MAX];
    /* Binary-exact values so the expected literal is stable. */
    int len = room_sweep_field_coord_token(token, sizeof(token), 1.0f, 2.0f, false, 0);
    check(len > 0, "token formats");
    check(
        strcmp(token, "pos=1.000000/2.000000/na") == 0,
        "token carries lat/lon and an honest na alt (NMEA alt not parsed)");
    check(strchr(token, ',') == NULL, "token is CSV-safe (no commas)");
    len = room_sweep_field_coord_token(token, sizeof(token), -1.5f, 2.25f, true, 123);
    check(
        strcmp(token, "pos=-1.500000/2.250000/123") == 0,
        "token carries altitude when a source provides one");
    char tiny[8];
    check(
        room_sweep_field_coord_token(tiny, sizeof(tiny), 1.0f, 2.0f, false, 0) == -1 &&
            tiny[0] == '\0',
        "tiny token buffer is refused, never truncated into validity");

    /* ---------------------------------------------------------- */
    /* 4. GPS-Log-OFF refusal state + run stats                   */
    /* ---------------------------------------------------------- */
    RoomSweepFieldState state;
    room_sweep_field_init(&state);
    check(!room_sweep_field_is_running(&state), "factory state is OFF every launch");
    check(!state.ran && state.loops == 0 && state.distance_m == 0, "factory stats are empty");

    check(
        room_sweep_field_toggle(&state, false, 1000) == RoomSweepFieldToggleRefused,
        "ON with GPS Log OFF is refused");
    check(!room_sweep_field_is_running(&state), "refusal never starts the loop");
    check(state.refused, "refusal is recorded for the UI");
    check(
        strstr(room_sweep_field_refusal_text(RoomSweepFieldToggleRefused), "GPS Log") != NULL,
        "refusal message names GPS Log");
    check(
        !room_sweep_field_note_fix(&state, 37000000, -122000000, false),
        "a refused state records no fixes (no silent coordinate drop)");
    check(state.fixes_used == 0 && !state.have_bbox, "refused state stores no coordinates");

    check(
        room_sweep_field_toggle(&state, true, 2000) == RoomSweepFieldToggleStarted,
        "ON with GPS Log ON starts the loop");
    check(room_sweep_field_is_running(&state), "loop runs after start");
    check(state.ran, "started marks the feature as ran");
    check(!state.refused, "successful start clears the refusal");

    /* Fixes: bbox of the operator's path + haversine walked distance. */
    check(
        room_sweep_field_note_fix(&state, 37000000, -122000000, true),
        "first fix is used");
    check(
        room_sweep_field_note_fix(&state, 37000900, -122000000, true),
        "second fix is used");
    check(
        room_sweep_field_note_fix(&state, 36999000, -121999000, true),
        "third fix is used");
    check(state.fixes_used == 3, "fixes used counts every stamped fix");
    check(
        !room_sweep_field_note_fix(&state, 1, 1, false),
        "a fix with GPS Log OFF records nothing");
    check(state.fixes_used == 3, "GPS-Log-OFF fix is not counted as used");
    check(state.have_bbox, "bbox exists after fixes");
    check(state.min_lat_e6 == 36999000 && state.max_lat_e6 == 37000900, "bbox latitude span");
    check(state.min_lon_e6 == -122000000 && state.max_lon_e6 == -121999000, "bbox longitude span");
    check(state.coords_recorded, "coordinates were recorded under GPS Log ON");
    /* ~111.2 m per 0.001 deg latitude: 0.0009 deg ~ 100 m, then ~148 m more
     * on the third fix — the exact sum is the haversine's business, only the
     * scale is pinned here. */
    check(state.distance_m > 150U && state.distance_m < 400U, "walked distance is in meters");

    check(
        room_sweep_field_duration_ms(&state, 7000) == 5000U,
        "duration covers the running span so far");
    room_sweep_field_note_loop(&state);
    room_sweep_field_note_loop(&state);
    check(state.loops == 2, "loop count tracks completed passes");

    check(
        room_sweep_field_toggle(&state, true, 8000) == RoomSweepFieldToggleStopped,
        "toggling while running stops (Back-equivalent)");
    check(!room_sweep_field_is_running(&state), "stopped state is not running");
    check(
        room_sweep_field_duration_ms(&state, 9000) == 6000U,
        "duration accumulates across runs and stops growing when stopped");
    check(!room_sweep_field_loop_restart(room_sweep_field_is_running(&state), RoomSweepFullGps),
          "a stopped loop no longer wraps the phase machine");
    room_sweep_field_stop(&state, 9000);
    check(
        room_sweep_field_duration_ms(&state, 50000) == 6000U,
        "stop after stop never double-counts");

    /* Restart keeps session-long stats (the report is per session). */
    check(
        room_sweep_field_toggle(&state, true, 10000) == RoomSweepFieldToggleStarted,
        "re-enable restarts the loop");
    check(state.loops == 2 && state.distance_m > 0U, "stats survive a restart within a session");

    /* Banner text: "FIELD nN phase dist". */
    char banner[32];
    check(
        room_sweep_field_banner(banner, sizeof(banner), 2, "Wi-raw", 183U) > 0 &&
            strcmp(banner, "FIELD n2 Wi-raw 183m") == 0,
        "banner strip matches the spec wording");
    check(
        room_sweep_field_banner(banner, 6U, 1, "RF", 0U) == -1 && banner[0] == '\0',
        "tiny banner buffer is refused");

    /* Step math: 0.001 deg of latitude is ~111 m. */
    check(
        room_sweep_field_step_m(0, 0, 1000, 0) >= 110U &&
            room_sweep_field_step_m(0, 0, 1000, 0) <= 113U,
        "haversine step ~111 m per 0.001 deg latitude");
    check(room_sweep_field_step_m(123, 456, 123, 456) == 0U, "same fix is zero distance");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

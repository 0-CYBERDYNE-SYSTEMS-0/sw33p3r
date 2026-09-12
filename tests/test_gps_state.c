#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../room_sweep_gps_state.h"

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
    RoomSweepGpsSnapshot snapshot = {
        .marauder_link = false,
        .external_gpio_link = false,
        .source = RoomSweepGpsSourceNone,
        .valid_sentences = 0,
        .has_fix = false,
        .has_pos = false,
        .last_valid_tick = 0,
        .now_tick = 100,
        .stale_timeout_ms = ROOM_SWEEP_GPS_DEFAULT_STALE_TIMEOUT_MS,
    };

    /* Up/Down page browsing cycles three pages and wraps in both directions. */
    check(
        room_sweep_gps_page_next(RoomSweepGpsPageSummary) == RoomSweepGpsPageDetail,
        "summary Down reaches detail page");
    check(
        room_sweep_gps_page_next(RoomSweepGpsPageDetail) == RoomSweepGpsPageRadar,
        "detail Down reaches radar page");
    check(
        room_sweep_gps_page_next(RoomSweepGpsPageRadar) == RoomSweepGpsPageSummary,
        "radar Down wraps to summary page");
    check(
        room_sweep_gps_page_prev(RoomSweepGpsPageSummary) == RoomSweepGpsPageRadar,
        "summary Up wraps to radar page");
    check(
        room_sweep_gps_page_prev(RoomSweepGpsPageRadar) == RoomSweepGpsPageDetail,
        "radar Up reaches detail page");
    check(
        room_sweep_gps_page_prev(RoomSweepGpsPageDetail) == RoomSweepGpsPageSummary,
        "detail Up reaches summary page");
    check(
        room_sweep_gps_page_text(RoomSweepGpsPageSummary)[0] == 'S' &&
            room_sweep_gps_page_text(RoomSweepGpsPageDetail)[0] == 'D' &&
            strcmp(room_sweep_gps_page_text(RoomSweepGpsPageRadar), "Radar") == 0,
        "all three GPS page labels are discoverable");

    /* No transport is different from a connected transport with no sentences. */
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusNoLink,
        "no Marauder or GPIO transport reports NO LINK");
    snapshot.marauder_link = true;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusWaiting,
        "BFFB Marauder link with no NMEA reports WAIT");

    /* Valid navigation data then separates no-fix, fix-without-position,
     * fix, and stale states. A receiver fix claim alone is NOT a FIX. */
    snapshot.valid_sentences = 1;
    snapshot.last_valid_tick = 100;
    snapshot.now_tick = 101;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusNoFix,
        "fresh navigation without a fix reports NO FIX");
    snapshot.has_fix = true;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusFixNoPos,
        "receiver fix claim without parsed coordinates reports NO POS");
    check(
        strcmp(room_sweep_gps_status_text(RoomSweepGpsStatusFixNoPos), "NO POS") == 0,
        "fix-without-position is labeled NO POS, never FIX");
    check(
        room_sweep_gps_status_is_fresh(RoomSweepGpsStatusFixNoPos),
        "fix-without-position still counts as fresh navigation");
    check(
        !room_sweep_gps_status_is_fresh(RoomSweepGpsStatusStale) &&
            !room_sweep_gps_status_is_fresh(RoomSweepGpsStatusWaiting) &&
            !room_sweep_gps_status_is_fresh(RoomSweepGpsStatusNoLink) &&
            room_sweep_gps_status_is_fresh(RoomSweepGpsStatusNoFix) &&
            room_sweep_gps_status_is_fresh(RoomSweepGpsStatusFix),
        "freshness is exactly the live-navigation states");
    snapshot.has_pos = true;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusFix,
        "fresh fix with parsed coordinates reports FIX");
    snapshot.now_tick = 5100;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusStale,
        "navigation at the five-second boundary reports STALE");

    /* Explicit timeout zero uses the documented default rather than staying fresh forever. */
    snapshot.stale_timeout_ms = 0;
    snapshot.now_tick = 5099;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusFix,
        "zero timeout selects the five-second default");

    /* Tick subtraction is wrap-safe for a valid sentence near UINT32_MAX. */
    snapshot.stale_timeout_ms = 5000;
    snapshot.last_valid_tick = UINT32_MAX - 100;
    snapshot.now_tick = 50;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusFix,
        "freshness remains correct across tick wrap");
    snapshot.now_tick = 5000;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusStale,
        "wrapped timestamp expires at the timeout boundary");

    /* Source labels name the actual transport and do not imply GPIO is required. */
    check(
        strcmp(room_sweep_gps_source_text(RoomSweepGpsSourceMarauder), "BFFB Marauder") == 0,
        "Marauder source is labeled BFFB Marauder");
    check(
        strcmp(
            room_sweep_gps_source_text(RoomSweepGpsSourceExternalGpio),
            "External GPIO (optional)") == 0,
        "GPIO source is labeled optional external GPIO");
    check(
        strcmp(room_sweep_gps_source_text(RoomSweepGpsSourceNone), "No active source") == 0,
        "missing source is labeled honestly");

    /* A GPIO-only device is valid and must not be mislabeled as Marauder. */
    snapshot.marauder_link = false;
    snapshot.external_gpio_link = true;
    snapshot.source = RoomSweepGpsSourceExternalGpio;
    snapshot.valid_sentences = 0;
    snapshot.last_valid_tick = 0;
    snapshot.has_fix = false;
    snapshot.has_pos = false;
    snapshot.now_tick = 100;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusWaiting,
        "optional external GPIO link reports WAIT before first sentence");

    /* Trail ring buffer: dedupe within ~2 m, FIFO wrap after MAX points. */
    {
        RoomSweepGpsTrail trail;
        const RoomSweepGpsTrailPoint* p;

        room_sweep_gps_trail_clear(&trail);
        check(trail.count == 0, "fresh trail starts empty");
        check(
            room_sweep_gps_trail_point_at(&trail, 0) == NULL,
            "empty trail has no points");

        room_sweep_gps_trail_push(&trail, 1000000, -70000000, 10);
        check(trail.count == 1, "first push stores one point");
        p = room_sweep_gps_trail_point_at(&trail, 0);
        check(
            p != NULL && p->lat_e6 == 1000000 && p->lon_e6 == -70000000 &&
                p->tick == 10,
            "newest point matches pushed coordinates and tick");

        /* Identical spot dedupes. */
        room_sweep_gps_trail_push(&trail, 1000000, -70000000, 20);
        check(trail.count == 1, "identical spot is deduped");

        /* 18 e6 in each axis is the ~2 m boundary: still deduped. */
        room_sweep_gps_trail_push(&trail, 1000018, -70000018, 30);
        check(trail.count == 1, "point within 2 m of newest is deduped");

        /* 37 e6 (~4 m) of latitude movement must be stored. */
        room_sweep_gps_trail_push(&trail, 1000037, -70000000, 40);
        check(trail.count == 2, "movement past 2 m stores a second point");
        p = room_sweep_gps_trail_point_at(&trail, 0);
        check(p != NULL && p->tick == 40, "point_at(0) is the newest point");
        p = room_sweep_gps_trail_point_at(&trail, 1);
        check(p != NULL && p->tick == 10, "point_at(1) is the previous point");

        /* MAX + 2 distinct pushes keep the MAX newest points in order. */
        room_sweep_gps_trail_clear(&trail);
        for(int i = 0; i < ROOM_SWEEP_GPS_TRAIL_MAX + 2; i++) {
            room_sweep_gps_trail_push(
                &trail, 1000 + 100 * i, 2000 + 100 * i, (uint32_t)(1000 + i));
        }
        check(
            trail.count == ROOM_SWEEP_GPS_TRAIL_MAX,
            "full trail caps at MAX points");
        p = room_sweep_gps_trail_point_at(&trail, 0);
        check(
            p != NULL && p->tick == 1000 + ROOM_SWEEP_GPS_TRAIL_MAX + 1,
            "wrap keeps the last pushed point as newest");
        p = room_sweep_gps_trail_point_at(&trail, ROOM_SWEEP_GPS_TRAIL_MAX - 1);
        check(
            p != NULL && p->tick == 1002,
            "point_at(MAX-1) is the oldest retained point");
        check(
            room_sweep_gps_trail_point_at(&trail, ROOM_SWEEP_GPS_TRAIL_MAX) == NULL,
            "point_at(MAX) is out of range");

        /* Clearing resets for the next session. */
        room_sweep_gps_trail_clear(&trail);
        check(
            trail.count == 0 && room_sweep_gps_trail_point_at(&trail, 0) == NULL,
            "clear empties a wrapped trail");
    }

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

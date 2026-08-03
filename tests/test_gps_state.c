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
        .last_valid_tick = 0,
        .now_tick = 100,
        .stale_timeout_ms = ROOM_SWEEP_GPS_DEFAULT_STALE_TIMEOUT_MS,
    };

    /* Up/Down page browsing is exactly two pages and wraps in both directions. */
    check(
        room_sweep_gps_page_next(RoomSweepGpsPageSummary) == RoomSweepGpsPageDetail,
        "summary Down reaches detail page");
    check(
        room_sweep_gps_page_next(RoomSweepGpsPageDetail) == RoomSweepGpsPageSummary,
        "detail Down wraps to summary page");
    check(
        room_sweep_gps_page_prev(RoomSweepGpsPageSummary) == RoomSweepGpsPageDetail,
        "summary Up wraps to detail page");
    check(
        room_sweep_gps_page_prev(RoomSweepGpsPageDetail) == RoomSweepGpsPageSummary,
        "detail Up reaches summary page");
    check(
        room_sweep_gps_page_text(RoomSweepGpsPageSummary)[0] == 'S' &&
            room_sweep_gps_page_text(RoomSweepGpsPageDetail)[0] == 'D',
        "both GPS page labels are discoverable");

    /* No transport is different from a connected transport with no sentences. */
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusNoLink,
        "no Marauder or GPIO transport reports NO LINK");
    snapshot.marauder_link = true;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusWaiting,
        "BFFB Marauder link with no NMEA reports WAIT");

    /* Valid navigation data then separates no-fix, fix, and stale states. */
    snapshot.valid_sentences = 1;
    snapshot.last_valid_tick = 100;
    snapshot.now_tick = 101;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusNoFix,
        "fresh navigation without a fix reports NO FIX");
    snapshot.has_fix = true;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusFix,
        "fresh navigation with a fix reports FIX");
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
    snapshot.now_tick = 100;
    check(
        room_sweep_gps_status(&snapshot) == RoomSweepGpsStatusWaiting,
        "optional external GPIO link reports WAIT before first sentence");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

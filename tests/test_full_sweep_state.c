#include <stdio.h>

#include "../room_sweep_full_sweep.h"
#include "../room_sweep_radio_path.h"

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
    RoomSweepFullSweepState s;
    room_sweep_full_sweep_init(&s);
    check(s.phase == RoomSweepFullIdle, "starts idle");
    check(!room_sweep_full_sweep_is_running(&s), "idle is not running");

    check(room_sweep_full_sweep_start(&s), "start ok");
    check(s.phase == RoomSweepFullRf, "first phase is RF");
    check(room_sweep_full_sweep_is_running(&s), "running after start");

    check(room_sweep_full_sweep_advance(&s, true), "RF advance");
    check(s.phase == RoomSweepFullWifi, "then Wi-Fi");
    check((s.phases_completed & ROOM_SWEEP_FULL_BIT_RF) != 0, "RF bit set");

    check(room_sweep_full_sweep_advance(&s, true), "Wi-Fi advance");
    check(s.phase == RoomSweepFullBle, "then BLE");
    check(room_sweep_full_sweep_advance(&s, false), "BLE skip still advances");
    check(s.phase == RoomSweepFullNrf24, "then nRF24");
    check((s.phases_completed & ROOM_SWEEP_FULL_BIT_BLE) == 0, "failed BLE not counted");

    check(room_sweep_full_sweep_advance(&s, true), "nRF24 advance");
    check(s.phase == RoomSweepFullGps, "then GPS");
    check(room_sweep_full_sweep_advance(&s, true), "GPS advance");
    check(s.phase == RoomSweepFullDone, "done");
    check(!room_sweep_full_sweep_is_running(&s), "not running when done");
    check(!room_sweep_full_sweep_completed_all(&s), "not all bits if BLE skipped");

    room_sweep_full_sweep_init(&s);
    room_sweep_full_sweep_start(&s);
    for(int i = 0; i < 5; i++) room_sweep_full_sweep_advance(&s, true);
    check(room_sweep_full_sweep_completed_all(&s), "all phases ok → completed_all");

    room_sweep_full_sweep_init(&s);
    room_sweep_full_sweep_start(&s);
    check(room_sweep_full_sweep_abort(&s), "abort ok");
    check(s.phase == RoomSweepFullAborted, "aborted phase");
    check(!room_sweep_full_sweep_advance(&s, true), "no advance after abort");

    check(room_sweep_force_internal_cc1101(RoomSweepSpiPathNrf24), "nRF24 forces internal");
    check(!room_sweep_force_internal_cc1101(RoomSweepSpiPathCc1101), "CC1101 path may use ext");
    check(room_sweep_external_cc1101_allowed(RoomSweepSpiPathCc1101), "ext allowed on CC1101 path");
    check(!room_sweep_external_cc1101_allowed(RoomSweepSpiPathNrf24), "ext blocked on nRF24 path");
    check(room_sweep_nrf24_spi_selected(RoomSweepSpiPathNrf24), "nRF24 selected");
    check(
        room_sweep_spi_path_step(RoomSweepSpiPathCc1101) == RoomSweepSpiPathNrf24,
        "path step toggles");

    check(
        room_sweep_full_sweep_phase_limit_ms(RoomSweepFullGps) == ROOM_SWEEP_FULL_GPS_MS,
        "GPS has hard limit");
    check(
        !room_sweep_full_sweep_gps_ready(1000, false, false),
        "GPS not ready at 1s with no data");
    check(
        room_sweep_full_sweep_gps_ready(ROOM_SWEEP_FULL_GPS_MIN_MS, true, false),
        "GPS early exit on fix after min dwell");
    check(
        room_sweep_full_sweep_gps_ready(ROOM_SWEEP_FULL_GPS_MS, false, false),
        "GPS hard timeout with zero data still finishes");
    check(
        room_sweep_full_sweep_hard_timeout(RoomSweepFullGps, ROOM_SWEEP_FULL_GPS_MS),
        "hard timeout helper matches GPS limit");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

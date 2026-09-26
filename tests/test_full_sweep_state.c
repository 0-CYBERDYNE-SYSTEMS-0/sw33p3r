#include <stdio.h>
#include <string.h>

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
    check(s.phase == RoomSweepFullRaw, "then Wi-raw (Phase 4 radar pass)");
    check((s.phases_completed & ROOM_SWEEP_FULL_BIT_BLE) == 0, "failed BLE not counted");

    check(room_sweep_full_sweep_advance(&s, true), "Wi-raw advance");
    check((s.phases_completed & ROOM_SWEEP_FULL_BIT_RAW) != 0, "raw bit set");
    check(s.phase == RoomSweepFullProbe, "then Wi-probe (Phase 11 hidden-SSID pass)");

    check(room_sweep_full_sweep_advance(&s, true), "Wi-probe advance");
    check((s.phases_completed & ROOM_SWEEP_FULL_BIT_PROBE) != 0, "probe bit set");
    check(s.phase == RoomSweepFullNrf24, "then nRF24");

    check(room_sweep_full_sweep_advance(&s, true), "nRF24 advance");
    check(s.phase == RoomSweepFullGps, "then GPS");
    check(room_sweep_full_sweep_advance(&s, true), "GPS advance");
    check(s.phase == RoomSweepFullDone, "done");
    check(!room_sweep_full_sweep_is_running(&s), "not running when done");
    check(!room_sweep_full_sweep_completed_all(&s), "not all bits if BLE skipped");

    room_sweep_full_sweep_init(&s);
    room_sweep_full_sweep_start(&s);
    for(int i = 0; i < 7; i++) room_sweep_full_sweep_advance(&s, true);
    check(room_sweep_full_sweep_completed_all(&s), "all phases ok → completed_all");

    /* Wi-raw honest skip: no UART scanner → advance(ok=false) leaves the
     * bit unset but the sequence continues (never blocks the sweep). */
    room_sweep_full_sweep_init(&s);
    room_sweep_full_sweep_start(&s);
    check(room_sweep_full_sweep_advance(&s, true), "RF advance 2");
    check(room_sweep_full_sweep_advance(&s, true), "Wi-Fi advance 2");
    check(room_sweep_full_sweep_advance(&s, true), "BLE advance 2");
    check(room_sweep_full_sweep_advance(&s, false), "raw skip advances");
    check(s.phase == RoomSweepFullProbe, "raw skip still reaches the probe pass");
    check(
        (s.phases_completed & ROOM_SWEEP_FULL_BIT_RAW) == 0,
        "skipped raw not counted");
    check(room_sweep_full_sweep_advance(&s, false), "probe skip advances");
    check(s.phase == RoomSweepFullNrf24, "probe skip still reaches nRF24");
    check(
        (s.phases_completed & ROOM_SWEEP_FULL_BIT_PROBE) == 0,
        "skipped probe not counted");
    check(
        room_sweep_full_sweep_hard_timeout(RoomSweepFullRaw, ROOM_SWEEP_FULL_RAW_MS),
        "raw phase has a hard ceiling");
    check(
        room_sweep_full_sweep_phase_limit_ms(RoomSweepFullRaw) == ROOM_SWEEP_FULL_RAW_MS,
        "raw limit constant matches helper");
    check(
        strcmp(room_sweep_full_sweep_phase_label(RoomSweepFullRaw), "Wi-raw") == 0,
        "raw phase label");
    check(
        room_sweep_full_sweep_hard_timeout(RoomSweepFullProbe, ROOM_SWEEP_FULL_PROBE_MS),
        "probe phase has a hard ceiling");
    check(
        room_sweep_full_sweep_phase_limit_ms(RoomSweepFullProbe) ==
            ROOM_SWEEP_FULL_PROBE_MS,
        "probe limit constant matches helper");
    check(
        ROOM_SWEEP_FULL_PROBE_MS > ROOM_SWEEP_FULL_RAW_MS,
        "probe window is longer than the raw pass (probes are bursty)");
    check(
        strcmp(room_sweep_full_sweep_phase_label(RoomSweepFullProbe), "Probe") == 0,
        "probe phase label");
    check(
        (ROOM_SWEEP_FULL_BIT_ALL & ROOM_SWEEP_FULL_BIT_PROBE) != 0,
        "completed_all requires the probe bit");

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
    /* Flag 1 is has_pos (parsed coordinates) — a receiver fix alone is the
     * NO POS state and must not satisfy the early-exit data gate. */
    check(
        room_sweep_full_sweep_gps_ready(ROOM_SWEEP_FULL_GPS_MIN_MS, true, false),
        "GPS early exit on parsed position after min dwell");
    check(
        room_sweep_full_sweep_gps_ready(ROOM_SWEEP_FULL_GPS_MIN_MS, false, true),
        "GPS early exit on received sentences after min dwell");
    check(
        room_sweep_full_sweep_gps_ready(ROOM_SWEEP_FULL_GPS_MS, false, false),
        "GPS hard timeout with zero data still finishes");
    check(
        room_sweep_full_sweep_hard_timeout(RoomSweepFullGps, ROOM_SWEEP_FULL_GPS_MS),
        "hard timeout helper matches GPS limit");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

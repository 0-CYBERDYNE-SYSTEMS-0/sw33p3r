/* Host tests: grouped settings + scan window presets. */
#include <stdio.h>

#include "../room_sweep_settings.h"

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
    check(RoomSweepSetCount == 13, "thirteen settings items");
    check(RoomSweepSetGroupCount == 5, "five settings groups");

    check(
        room_sweep_set_group_of(RoomSweepSetSound) == RoomSweepSetGroupFeedback,
        "Sound is Feedback");
    check(
        room_sweep_set_group_of(RoomSweepSetScanWin) == RoomSweepSetGroupWireless,
        "ScanWin is Wireless");
    check(
        room_sweep_set_group_of(RoomSweepSetTxDur) == RoomSweepSetGroupRadio,
        "TXDur is Radio");
    check(
        room_sweep_set_group_of(RoomSweepSetSpiPath) == RoomSweepSetGroupRadio,
        "SpiPath is Radio");
    check(
        room_sweep_set_group_of(RoomSweepSetFullSweep) == RoomSweepSetGroupSession,
        "FullSweep is Session");
    check(
        room_sweep_set_group_of(RoomSweepSetRecord) == RoomSweepSetGroupSession,
        "Record is Session");

    check(
        room_sweep_set_group_first(RoomSweepSetGroupWireless) == RoomSweepSetRescan,
        "Wireless group starts at Rescan");

    uint8_t sel = RoomSweepSetSound;
    sel = room_sweep_set_cursor_step(sel, true);
    check(sel == RoomSweepSetVibro, "Down stays in Feedback group");
    sel = room_sweep_set_cursor_step(sel, true);
    check(sel == RoomSweepSetSound, "Down wraps within Feedback");

    sel = room_sweep_set_group_step(RoomSweepSetSound, true);
    check(sel == RoomSweepSetRescan, "Long-right lands on Wireless first item");
    sel = room_sweep_set_group_step(sel, true);
    check(sel == RoomSweepSetExtBand, "next group is Radio");
    sel = room_sweep_set_group_step(sel, false);
    check(sel == RoomSweepSetRescan, "Long-left returns to Wireless");

    check(room_sweep_scan_timeout_ms(0) == 15000U, "scan preset 0 is 15s");
    check(room_sweep_scan_timeout_ms(1) == 30000U, "scan preset 1 is 30s");
    check(room_sweep_scan_timeout_ms(2) == 60000U, "scan preset 2 is 60s");
    check(room_sweep_scan_timeout_seconds(1) == 30, "seconds helper");
    check(room_sweep_scan_timeout_step(1, true) == 2, "longer steps 30->60");
    check(room_sweep_scan_timeout_step(2, true) == 0, "longer wraps 60->15");
    check(room_sweep_scan_timeout_step(0, false) == 2, "shorter wraps 15->60");

    check(
        room_sweep_set_group_label(RoomSweepSetGroupSession)[0] != '\0',
        "group labels are non-empty");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

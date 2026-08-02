#include <stdio.h>

#include "../room_sweep_input.h"

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
    check(
        room_sweep_back_action(true, false, true) == RoomSweepBackExit,
        "long Back exits even when Settings is open");
    check(
        room_sweep_back_action(true, false, false) == RoomSweepBackCloseSettings,
        "short Back closes Settings");
    check(
        room_sweep_back_action(false, true, false) == RoomSweepBackDisarm,
        "short Back disarms an armed TX tab");
    check(
        room_sweep_back_action(false, false, false) == RoomSweepBackOpenSettings,
        "short Back opens Settings outside TX");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

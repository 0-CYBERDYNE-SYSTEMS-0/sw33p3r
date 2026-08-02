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

static void check_action(
    RoomSweepInputKey key,
    RoomSweepInputPhase phase,
    RoomSweepInputAction expected,
    const char* message) {
    check(room_sweep_input_action(key, phase) == expected, message);
}

int main(void) {
    /* Long Back always wins; short Back applies one contextual transition. */
    check(
        room_sweep_back_action(true, false, true) == RoomSweepBackExit,
        "long Back exits even when Settings is open");
    check(
        room_sweep_back_action(true, true, true) == RoomSweepBackExit,
        "long Back exits when Settings and TX state overlap");
    check(
        room_sweep_back_action(false, false, true) == RoomSweepBackExit,
        "long Back exits outside Settings");
    check(
        room_sweep_back_action(false, true, true) == RoomSweepBackExit,
        "long Back exits while TX is active");
    check(
        room_sweep_back_action(true, false, false) == RoomSweepBackCloseSettings,
        "short Back closes Settings");
    check(
        room_sweep_back_action(true, true, false) == RoomSweepBackCloseSettings,
        "short Back closes Settings before changing hidden TX state");
    check(
        room_sweep_back_action(false, true, false) == RoomSweepBackDisarm,
        "short Back disarms an armed TX tab");
    check(
        room_sweep_back_action(false, false, false) == RoomSweepBackOpenSettings,
        "short Back opens Settings outside TX");

    /*
     * Exhaust every supported key/phase pair.  This table is the safety seam:
     * repeats are browse-only and long Left/Right cannot silently change tabs.
     */
    static const RoomSweepInputAction expected[RoomSweepInputKeyCount]
                                                      [RoomSweepInputPhaseCount] = {
        [RoomSweepInputUp] =
            {RoomSweepInputBrowseUp, RoomSweepInputBrowseUp, RoomSweepInputBrowseUp},
        [RoomSweepInputDown] =
            {RoomSweepInputBrowseDown, RoomSweepInputBrowseDown, RoomSweepInputBrowseDown},
        [RoomSweepInputLeft] =
            {RoomSweepInputNavigatePrev, RoomSweepInputAlternatePrev, RoomSweepInputNone},
        [RoomSweepInputRight] =
            {RoomSweepInputNavigateNext, RoomSweepInputAlternateNext, RoomSweepInputNone},
        [RoomSweepInputOk] =
            {RoomSweepInputPrimary, RoomSweepInputSecondary, RoomSweepInputNone},
        [RoomSweepInputBack] =
            {RoomSweepInputBackShort, RoomSweepInputBackLong, RoomSweepInputNone},
    };
    static const char* key_names[RoomSweepInputKeyCount] =
        {"Up", "Down", "Left", "Right", "OK", "Back"};
    static const char* phase_names[RoomSweepInputPhaseCount] =
        {"short", "long", "repeat"};
    char message[64];
    for(int key = 0; key < RoomSweepInputKeyCount; key++) {
        for(int phase = 0; phase < RoomSweepInputPhaseCount; phase++) {
            snprintf(
                message,
                sizeof(message),
                "%s %s is classified safely",
                phase_names[phase],
                key_names[key]);
            check_action(
                (RoomSweepInputKey)key,
                (RoomSweepInputPhase)phase,
                expected[key][phase],
                message);
        }
    }

    /* Flipper Press/Release and unknown values stay outside this pure seam. */
    check_action(
        (RoomSweepInputKey)RoomSweepInputKeyCount,
        RoomSweepInputShort,
        RoomSweepInputNone,
        "unknown key is rejected");
    check_action(
        RoomSweepInputOk,
        (RoomSweepInputPhase)RoomSweepInputPhaseCount,
        RoomSweepInputNone,
        "unsupported input phase is rejected");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

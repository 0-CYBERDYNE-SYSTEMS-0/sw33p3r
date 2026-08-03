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

    /* Browse movement wraps at both ends and never escapes an empty list. */
    check(
        room_sweep_cursor_step(0u, 3u, RoomSweepInputBrowseUp) == 2u,
        "Up wraps the browse cursor from the first item");
    check(
        room_sweep_cursor_step(2u, 3u, RoomSweepInputBrowseDown) == 0u,
        "Down wraps the browse cursor from the last item");
    check(
        room_sweep_cursor_step(1u, 3u, RoomSweepInputBrowseUp) == 0u,
        "Up steps toward the first item");
    check(
        room_sweep_cursor_step(1u, 3u, RoomSweepInputBrowseDown) == 2u,
        "Down steps toward the last item");
    check(
        room_sweep_cursor_step(9u, 3u, RoomSweepInputBrowseDown) == 1u,
        "an invalid cursor is normalized before stepping");
    check(
        room_sweep_cursor_step(1u, 0u, RoomSweepInputBrowseDown) == 0u,
        "an empty browse list always returns cursor zero");
    check(
        room_sweep_cursor_step(1u, 3u, RoomSweepInputPrimary) == 1u,
        "a non-browse action leaves the cursor unchanged");

    /* Long Left/Right and every repeat action cannot change tabs. */
    check(
        room_sweep_input_is_tab_navigation(RoomSweepInputNavigatePrev),
        "short Left is the only previous-tab action");
    check(
        room_sweep_input_is_tab_navigation(RoomSweepInputNavigateNext),
        "short Right is the only next-tab action");
    check(
        !room_sweep_input_is_tab_navigation(RoomSweepInputAlternatePrev) &&
            !room_sweep_input_is_tab_navigation(RoomSweepInputAlternateNext),
        "long Left/Right alternate actions cannot change tabs");
    check(
        !room_sweep_input_is_tab_navigation(
            room_sweep_input_action(RoomSweepInputLeft, RoomSweepInputRepeat)) &&
            !room_sweep_input_is_tab_navigation(
                room_sweep_input_action(RoomSweepInputRight, RoomSweepInputRepeat)),
        "repeated Left/Right cannot change tabs");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

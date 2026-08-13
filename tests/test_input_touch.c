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

static void check_browse(
    RoomSweepInputKey key,
    RoomSweepInputPhase phase,
    RoomSweepInputBrowsePhase expected,
    const char* message) {
    check(room_sweep_input_browse_phase(key, phase) == expected, message);
}

int main(void) {
    /* --- Phase-aware browse classifier --- */

    /* Up/Down carry their phase tag through. */
    check_browse(
        RoomSweepInputUp,
        RoomSweepInputShort,
        RoomSweepInputBrowsePhaseShort,
        "Up short tags short");
    check_browse(
        RoomSweepInputUp,
        RoomSweepInputLong,
        RoomSweepInputBrowsePhaseLong,
        "Up long tags long");
    check_browse(
        RoomSweepInputUp,
        RoomSweepInputRepeat,
        RoomSweepInputBrowsePhaseRepeat,
        "Up repeat tags repeat");
    check_browse(
        RoomSweepInputDown,
        RoomSweepInputShort,
        RoomSweepInputBrowsePhaseShort,
        "Down short tags short");
    check_browse(
        RoomSweepInputDown,
        RoomSweepInputLong,
        RoomSweepInputBrowsePhaseLong,
        "Down long tags long");
    check_browse(
        RoomSweepInputDown,
        RoomSweepInputRepeat,
        RoomSweepInputBrowsePhaseRepeat,
        "Down repeat tags repeat");

    /* Invalid phases on Up/Down classify as None. */
    check_browse(
        RoomSweepInputUp,
        (RoomSweepInputPhase)RoomSweepInputPhaseCount,
        RoomSweepInputBrowsePhaseNone,
        "Up with an invalid phase is None");
    check_browse(
        RoomSweepInputDown,
        (RoomSweepInputPhase)RoomSweepInputPhaseCount,
        RoomSweepInputBrowsePhaseNone,
        "Down with an invalid phase is None");
    check_browse(
        RoomSweepInputUp,
        (RoomSweepInputPhase)(RoomSweepInputPhaseCount + 1),
        RoomSweepInputBrowsePhaseNone,
        "Up with an out-of-range phase is None");
    check_browse(
        RoomSweepInputDown,
        (RoomSweepInputPhase)(-1),
        RoomSweepInputBrowsePhaseNone,
        "Down with a negative phase is None");

    /* Left/Right/OK/Back are not browse controls: None for every phase. */
    static const RoomSweepInputKey non_browse_keys[] = {
        RoomSweepInputLeft,
        RoomSweepInputRight,
        RoomSweepInputOk,
        RoomSweepInputBack,
    };
    static const char* non_browse_names[] = {"Left", "Right", "OK", "Back"};
    static const char* phase_names[] = {"short", "long", "repeat"};
    char message[64];
    for(unsigned int k = 0; k < sizeof(non_browse_keys) / sizeof(non_browse_keys[0]); k++) {
        for(int phase = 0; phase < RoomSweepInputPhaseCount; phase++) {
            snprintf(
                message,
                sizeof(message),
                "%s %s is not a browse phase",
                phase_names[phase],
                non_browse_names[k]);
            check_browse(
                non_browse_keys[k],
                (RoomSweepInputPhase)phase,
                RoomSweepInputBrowsePhaseNone,
                message);
        }
    }

    /* Invalid keys classify as None. */
    check_browse(
        (RoomSweepInputKey)RoomSweepInputKeyCount,
        RoomSweepInputShort,
        RoomSweepInputBrowsePhaseNone,
        "unknown key is None");
    check_browse(
        (RoomSweepInputKey)(-1),
        RoomSweepInputLong,
        RoomSweepInputBrowsePhaseNone,
        "negative key is None");

    /* --- Touch feedback tracker --- */

    /* Zero-initialized state behaves as inactive. */
    RoomSweepInputTouchState st = {0};
    check(
        !st.active && st.key == RoomSweepInputUp && !st.long_delivered,
        "zero-initialized state is inactive");

    /* Release with no press is None and leaves the state inactive. */
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputOk) ==
            RoomSweepInputFeedbackNone,
        "release without a press is None");
    check(!st.active, "release without a press leaves the state inactive");

    /* Press then Release gives PressTick. */
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputOk) ==
            RoomSweepInputFeedbackNone,
        "press returns None");
    check(
        st.active && st.key == RoomSweepInputOk && !st.long_delivered,
        "press tracks the key without a long note");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputOk) ==
            RoomSweepInputFeedbackPressTick,
        "matching release gives a press tick");
    check(!st.active, "matching release resets the state");

    /* Press, note_long on the same key, Release gives HoldConfirmed. */
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputOk);
    room_sweep_input_touch_note_long(&st, RoomSweepInputOk);
    check(st.long_delivered, "note_long on the tracked key sets long_delivered");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputOk) ==
            RoomSweepInputFeedbackHoldConfirmed,
        "release after a long note gives HoldConfirmed");

    /* note_long on a different key is ignored: release stays PressTick. */
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputOk);
    room_sweep_input_touch_note_long(&st, RoomSweepInputBack);
    check(!st.long_delivered, "note_long on a wrong key is ignored");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputOk) ==
            RoomSweepInputFeedbackPressTick,
        "release stays a press tick when the long note hit a wrong key");

    /* Rollover: Press(A), Press(B), Release(A) is None, Release(B) ticks. */
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputLeft);
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputRight);
    check(st.key == RoomSweepInputRight, "a second press re-tracks to the new key");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputLeft) ==
            RoomSweepInputFeedbackNone,
        "stale release after rollover is None");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputRight) ==
            RoomSweepInputFeedbackPressTick,
        "release of the rolled-to key gives a press tick");
    check(!st.active, "rollover completes with an inactive state");

    /* Re-pressing the same key clears long_delivered. */
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputOk);
    room_sweep_input_touch_note_long(&st, RoomSweepInputOk);
    check(st.long_delivered, "long note is recorded");
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputOk);
    check(!st.long_delivered, "re-press of the same key clears the long note");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputOk) ==
            RoomSweepInputFeedbackPressTick,
        "release after a cleared long note gives a press tick");

    /* Invalid key values are no-ops. */
    room_sweep_input_touch_feedback(&st, RoomSweepInputTouchPress, RoomSweepInputOk);
    check(
        room_sweep_input_touch_feedback(
            &st, RoomSweepInputTouchPress, (RoomSweepInputKey)RoomSweepInputKeyCount) ==
            RoomSweepInputFeedbackNone,
        "press with an invalid key is None");
    check(
        st.active && st.key == RoomSweepInputOk,
        "press with an invalid key leaves the tracked press alone");
    check(
        room_sweep_input_touch_feedback(
            &st, RoomSweepInputTouchRelease, (RoomSweepInputKey)RoomSweepInputKeyCount) ==
            RoomSweepInputFeedbackNone,
        "release with an invalid key is None");
    check(
        st.active && st.key == RoomSweepInputOk,
        "release with an invalid key leaves the tracked press alone");
    check(
        room_sweep_input_touch_feedback(&st, RoomSweepInputTouchRelease, RoomSweepInputOk) ==
            RoomSweepInputFeedbackPressTick,
        "the tracked press still ticks after invalid events");

    /* note_long while inactive is a no-op. */
    room_sweep_input_touch_note_long(&st, RoomSweepInputOk);
    check(!st.active, "note_long while inactive is a no-op");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

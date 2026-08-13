#pragma once

#include <stdbool.h>

/*
 * Portable operator-input vocabulary.
 *
 * Keep this seam independent from Flipper's input headers so it can be
 * compiled by the host tests.  The runtime adapter may translate InputKey
 * and InputType values into these enums before dispatching an action.
 */
typedef enum {
    RoomSweepInputUp,
    RoomSweepInputDown,
    RoomSweepInputLeft,
    RoomSweepInputRight,
    RoomSweepInputOk,
    RoomSweepInputBack,
    RoomSweepInputKeyCount,
} RoomSweepInputKey;

typedef enum {
    RoomSweepInputShort,
    RoomSweepInputLong,
    RoomSweepInputRepeat,
    RoomSweepInputPhaseCount,
} RoomSweepInputPhase;

typedef enum {
    RoomSweepInputNone,
    RoomSweepInputBrowseUp,
    RoomSweepInputBrowseDown,
    RoomSweepInputNavigatePrev,
    RoomSweepInputNavigateNext,
    RoomSweepInputAlternatePrev,
    RoomSweepInputAlternateNext,
    RoomSweepInputPrimary,
    RoomSweepInputSecondary,
    RoomSweepInputBackShort,
    RoomSweepInputBackLong,
} RoomSweepInputAction;

/*
 * Classify one physical input event without applying mode-specific state.
 *
 * Up/Down are the only repeatable controls: a held direction advances a
 * browse cursor one item per repeat.  Repeat is deliberately rejected for
 * Left/Right, OK, and Back so a held button cannot skip tabs, repeat an
 * action, or retrigger an exit/safety transition.  Long Left/Right are
 * deliberately distinct from ordinary tab navigation so
 * only an explicitly supporting context (RF Sweep while idle) can use them.
 */
static inline RoomSweepInputAction room_sweep_input_action(
    RoomSweepInputKey key,
    RoomSweepInputPhase phase) {
    switch(key) {
    case RoomSweepInputUp:
        return phase == RoomSweepInputRepeat ||
                       phase == RoomSweepInputShort ||
                       phase == RoomSweepInputLong
                   ? RoomSweepInputBrowseUp
                   : RoomSweepInputNone;
    case RoomSweepInputDown:
        return phase == RoomSweepInputRepeat ||
                       phase == RoomSweepInputShort ||
                       phase == RoomSweepInputLong
                   ? RoomSweepInputBrowseDown
                   : RoomSweepInputNone;
    case RoomSweepInputLeft:
        if(phase == RoomSweepInputShort) return RoomSweepInputNavigatePrev;
        if(phase == RoomSweepInputLong) return RoomSweepInputAlternatePrev;
        return RoomSweepInputNone;
    case RoomSweepInputRight:
        if(phase == RoomSweepInputShort) return RoomSweepInputNavigateNext;
        if(phase == RoomSweepInputLong) return RoomSweepInputAlternateNext;
        return RoomSweepInputNone;
    case RoomSweepInputOk:
        if(phase == RoomSweepInputShort) return RoomSweepInputPrimary;
        if(phase == RoomSweepInputLong) return RoomSweepInputSecondary;
        return RoomSweepInputNone;
    case RoomSweepInputBack:
        if(phase == RoomSweepInputShort) return RoomSweepInputBackShort;
        if(phase == RoomSweepInputLong) return RoomSweepInputBackLong;
        return RoomSweepInputNone;
    default:
        return RoomSweepInputNone;
    }
}

/*
 * Advance a browse cursor while keeping it inside [0, count).  Invalid
 * cursors are normalized to the first item; non-browse actions are no-ops.
 */
static inline unsigned int room_sweep_cursor_step(
    unsigned int cursor,
    unsigned int count,
    RoomSweepInputAction action) {
    if(count == 0u) return 0u;
    if(cursor >= count) cursor = 0u;

    if(action == RoomSweepInputBrowseUp) {
        return cursor == 0u ? count - 1u : cursor - 1u;
    }
    if(action == RoomSweepInputBrowseDown) {
        return cursor + 1u == count ? 0u : cursor + 1u;
    }
    return cursor;
}

/* Only short Left/Right may change tabs; long and repeat phases are separate
 * actions (or None) and must be handled by their explicit owning context. */
static inline bool room_sweep_input_is_tab_navigation(RoomSweepInputAction action) {
    return action == RoomSweepInputNavigatePrev || action == RoomSweepInputNavigateNext;
}

typedef enum {
    RoomSweepBackCloseSettings,
    RoomSweepBackOpenSettings,
    RoomSweepBackDisarm,
    RoomSweepBackExit,
} RoomSweepBackAction;

static inline RoomSweepBackAction room_sweep_back_action(
    bool settings_active,
    bool tx_armed,
    bool long_press) {
    if(long_press) return RoomSweepBackExit;
    if(settings_active) return RoomSweepBackCloseSettings;
    return tx_armed ? RoomSweepBackDisarm : RoomSweepBackOpenSettings;
}

/*
 * Phase-aware browse classifier.
 *
 * Where room_sweep_input_action() collapses every Up/Down phase into a single
 * BrowseUp/BrowseDown action, this classifier keeps the phase tag so the RF
 * tab can tell a short tap from a hold or a repeat without going through the
 * legacy classifier.  Up/Down return the tagged phase; every other key
 * (Left/Right/OK/Back) and every invalid key or phase enum value classify as
 * None.
 */
typedef enum {
    RoomSweepInputBrowsePhaseNone,
    RoomSweepInputBrowsePhaseShort,
    RoomSweepInputBrowsePhaseLong,
    RoomSweepInputBrowsePhaseRepeat,
} RoomSweepInputBrowsePhase;

static inline RoomSweepInputBrowsePhase room_sweep_input_browse_phase(
    RoomSweepInputKey key,
    RoomSweepInputPhase phase) {
    switch(key) {
    case RoomSweepInputUp:
    case RoomSweepInputDown:
        switch(phase) {
        case RoomSweepInputShort:
            return RoomSweepInputBrowsePhaseShort;
        case RoomSweepInputLong:
            return RoomSweepInputBrowsePhaseLong;
        case RoomSweepInputRepeat:
            return RoomSweepInputBrowsePhaseRepeat;
        default:
            return RoomSweepInputBrowsePhaseNone;
        }
    default:
        return RoomSweepInputBrowsePhaseNone;
    }
}

/*
 * Press/hold touch feedback tracker.
 *
 * RoomSweepInputTouchState is zero-initializable: `= {0}` yields an inactive
 * state with no tracked key.  Feed press/release pairs through
 * room_sweep_input_touch_feedback() and mark long-hold delivery with
 * room_sweep_input_touch_note_long(); the returned feedback tells the caller
 * whether to emit a press tick or a hold-confirmed cue.
 */
typedef enum {
    RoomSweepInputTouchPress,
    RoomSweepInputTouchRelease,
} RoomSweepInputTouchEvent;

typedef enum {
    RoomSweepInputFeedbackNone,
    RoomSweepInputFeedbackPressTick,
    RoomSweepInputFeedbackHoldConfirmed,
} RoomSweepInputFeedback;

typedef struct {
    bool active;
    RoomSweepInputKey key;
    bool long_delivered;
} RoomSweepInputTouchState;

/* Record that the long-hold feedback for the tracked press has already been
 * delivered.  No-op unless the state is active and key is the tracked key. */
static inline void room_sweep_input_touch_note_long(
    RoomSweepInputTouchState* st,
    RoomSweepInputKey key) {
    if(st->active && st->key == key) {
        st->long_delivered = true;
    }
}

/*
 * Feed one touch event and return the feedback to emit:
 *
 * - Press: re-track this key (active=true, key=key, long_delivered=false) and
 *   return None.  A new press while active re-tracks to the new key
 *   (rollover-safe); re-pressing the same key clears long_delivered.
 * - Release with !active: return None.
 * - Release with active but key != tracked key: ignore the stale release
 *   (state untouched), return None.  The newer press stays tracked and still
 *   yields its own feedback when released.
 * - Release with active and key == tracked key: return HoldConfirmed when
 *   long_delivered, otherwise PressTick; the state resets to inactive.
 * - Invalid key enum values are no-ops (state untouched, returns None), as
 *   are invalid event values.
 */
static inline RoomSweepInputFeedback room_sweep_input_touch_feedback(
    RoomSweepInputTouchState* st,
    RoomSweepInputTouchEvent ev,
    RoomSweepInputKey key) {
    if(ev != RoomSweepInputTouchPress && ev != RoomSweepInputTouchRelease) {
        return RoomSweepInputFeedbackNone;
    }
    /* Upper bound only: gcc's enum-range tracking flags a `key < Up` lower
     * bound as always-false under -Werror=type-limits (ARM build), and the
     * runtime adapter never produces negative keys. */
    if(key >= RoomSweepInputKeyCount) {
        return RoomSweepInputFeedbackNone;
    }
    if(ev == RoomSweepInputTouchPress) {
        st->active = true;
        st->key = key;
        st->long_delivered = false;
        return RoomSweepInputFeedbackNone;
    }
    if(!st->active) return RoomSweepInputFeedbackNone;
    if(st->key != key) return RoomSweepInputFeedbackNone;
    RoomSweepInputFeedback feedback = st->long_delivered
                                           ? RoomSweepInputFeedbackHoldConfirmed
                                           : RoomSweepInputFeedbackPressTick;
    st->active = false;
    return feedback;
}

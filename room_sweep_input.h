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

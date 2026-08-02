#pragma once

#include <stdbool.h>

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

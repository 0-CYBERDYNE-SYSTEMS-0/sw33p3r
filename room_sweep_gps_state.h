#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Furi-free GPS presentation state.  The runtime owns UART/GPIO lifecycle;
 * this seam only classifies the snapshot that the renderer should present.
 */
#define ROOM_SWEEP_GPS_DEFAULT_STALE_TIMEOUT_MS 5000U

typedef enum {
    RoomSweepGpsPageSummary = 0,
    RoomSweepGpsPageDetail,
    RoomSweepGpsPageCount,
} RoomSweepGpsPage;

typedef enum {
    RoomSweepGpsSourceNone = 0,
    RoomSweepGpsSourceMarauder,
    RoomSweepGpsSourceExternalGpio,
} RoomSweepGpsSource;

typedef enum {
    RoomSweepGpsStatusNoLink = 0,
    RoomSweepGpsStatusWaiting,
    RoomSweepGpsStatusNoFix,
    RoomSweepGpsStatusFix,
    RoomSweepGpsStatusStale,
} RoomSweepGpsStatus;

typedef struct {
    /* A link means the corresponding transport is available, not that a fix exists. */
    bool marauder_link;
    bool external_gpio_link;
    RoomSweepGpsSource source;
    uint32_t valid_sentences;
    bool has_fix;
    uint32_t last_valid_tick;
    uint32_t now_tick;
    uint32_t stale_timeout_ms;
} RoomSweepGpsSnapshot;

static inline RoomSweepGpsPage room_sweep_gps_page_next(RoomSweepGpsPage page) {
    return (RoomSweepGpsPage)(((int)page + 1) % RoomSweepGpsPageCount);
}

static inline RoomSweepGpsPage room_sweep_gps_page_prev(RoomSweepGpsPage page) {
    return page == RoomSweepGpsPageSummary
               ? RoomSweepGpsPageDetail
               : RoomSweepGpsPageSummary;
}

static inline const char* room_sweep_gps_page_text(RoomSweepGpsPage page) {
    return page == RoomSweepGpsPageDetail ? "Detail" : "Summary";
}

static inline const char* room_sweep_gps_source_text(RoomSweepGpsSource source) {
    switch(source) {
    case RoomSweepGpsSourceMarauder:
        return "BFFB Marauder";
    case RoomSweepGpsSourceExternalGpio:
        return "External GPIO (optional)";
    case RoomSweepGpsSourceNone:
    default:
        return "No active source";
    }
}

static inline bool room_sweep_gps_is_fresh(
    uint32_t last_valid_tick,
    uint32_t now_tick,
    uint32_t stale_timeout_ms) {
    if(last_valid_tick == 0 || stale_timeout_ms == 0) return false;
    /* Unsigned subtraction intentionally remains correct across tick wrap. */
    return (uint32_t)(now_tick - last_valid_tick) < stale_timeout_ms;
}

static inline RoomSweepGpsStatus room_sweep_gps_status(
    const RoomSweepGpsSnapshot* snapshot) {
    if(!snapshot || (!snapshot->marauder_link && !snapshot->external_gpio_link)) {
        return RoomSweepGpsStatusNoLink;
    }
    if(snapshot->valid_sentences == 0) return RoomSweepGpsStatusWaiting;

    uint32_t timeout = snapshot->stale_timeout_ms;
    if(timeout == 0) timeout = ROOM_SWEEP_GPS_DEFAULT_STALE_TIMEOUT_MS;
    if(!room_sweep_gps_is_fresh(
           snapshot->last_valid_tick, snapshot->now_tick, timeout)) {
        return RoomSweepGpsStatusStale;
    }
    return snapshot->has_fix ? RoomSweepGpsStatusFix : RoomSweepGpsStatusNoFix;
}

static inline const char* room_sweep_gps_status_text(RoomSweepGpsStatus status) {
    switch(status) {
    case RoomSweepGpsStatusNoLink:
        return "NO LINK";
    case RoomSweepGpsStatusWaiting:
        return "WAIT";
    case RoomSweepGpsStatusNoFix:
        return "NO FIX";
    case RoomSweepGpsStatusFix:
        return "FIX";
    case RoomSweepGpsStatusStale:
        return "STALE";
    default:
        return "NO LINK";
    }
}

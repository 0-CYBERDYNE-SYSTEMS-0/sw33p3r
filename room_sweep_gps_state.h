#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Furi-free GPS presentation state.  The runtime owns UART/GPIO lifecycle;
 * this seam only classifies the snapshot that the renderer should present.
 */
#define ROOM_SWEEP_GPS_DEFAULT_STALE_TIMEOUT_MS 5000U

typedef enum {
    RoomSweepGpsPageSummary = 0,
    RoomSweepGpsPageDetail,
    RoomSweepGpsPageRadar,
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
    /* Fresh navigation AND a valid parsed latitude/longitude: a usable
     * position. Never shown when only the receiver claims a fix. */
    RoomSweepGpsStatusFix,
    /* Receiver reports a fix (GGA quality > 0 / RMC 'A') but no valid
     * coordinates were parsed in the fresh window — NOT a usable position. */
    RoomSweepGpsStatusFixNoPos,
    RoomSweepGpsStatusStale,
} RoomSweepGpsStatus;

typedef struct {
    /* A link means the corresponding transport is available, not that a fix exists. */
    bool marauder_link;
    bool external_gpio_link;
    RoomSweepGpsSource source;
    uint32_t valid_sentences;
    bool has_fix; /* receiver claims a fix; not a position guarantee */
    bool has_pos; /* valid parsed latitude/longitude in the latest nav data */
    uint32_t last_valid_tick;
    uint32_t now_tick;
    uint32_t stale_timeout_ms;
} RoomSweepGpsSnapshot;

static inline RoomSweepGpsPage room_sweep_gps_page_next(RoomSweepGpsPage page) {
    return (RoomSweepGpsPage)(((int)page + 1) % RoomSweepGpsPageCount);
}

static inline RoomSweepGpsPage room_sweep_gps_page_prev(RoomSweepGpsPage page) {
    return (RoomSweepGpsPage)(((int)page + RoomSweepGpsPageCount - 1) %
                              RoomSweepGpsPageCount);
}

static inline const char* room_sweep_gps_page_text(RoomSweepGpsPage page) {
    switch(page) {
    case RoomSweepGpsPageDetail:
        return "Detail";
    case RoomSweepGpsPageRadar:
        return "Radar";
    case RoomSweepGpsPageSummary:
    default:
        return "Summary";
    }
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
    if(!snapshot->has_fix) return RoomSweepGpsStatusNoFix;
    /* "FIX" requires parsed coordinates: a receiver fix claim alone must not
     * be presented as a usable position. */
    return snapshot->has_pos ? RoomSweepGpsStatusFix : RoomSweepGpsStatusFixNoPos;
}

/* True when a fresh navigation sentence stream exists — includes NO FIX and
 * FIX-without-position, since both come from live nav data. Only NO LINK,
 * WAITING, and STALE are not fresh. */
static inline bool room_sweep_gps_status_is_fresh(RoomSweepGpsStatus status) {
    return status == RoomSweepGpsStatusFix ||
           status == RoomSweepGpsStatusFixNoPos ||
           status == RoomSweepGpsStatusNoFix;
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
    case RoomSweepGpsStatusFixNoPos:
        return "NO POS";
    case RoomSweepGpsStatusStale:
        return "STALE";
    default:
        return "NO LINK";
    }
}

/*
 * GPS trail ring buffer (radar page presentation seam, Furi-free).
 * Coordinates are integer e6 (degrees * 1e6); tick is a millisecond counter
 * owned by the runtime.
 *
 * Dedupe threshold: 1e-6 degree of latitude is ~0.111 m, so 18e-6 deg is
 * ~2.0 m (18 * 0.111 = 1.998 m, slightly under the 2 m band).  Longitude
 * degrees shrink toward the poles, so the same 18e-6 threshold errs on the
 * permissive side there; this is a display dedupe, not a survey measurement.
 */
#define ROOM_SWEEP_GPS_TRAIL_MAX 8

typedef struct {
    int32_t lat_e6;
    int32_t lon_e6;
    uint32_t tick;
} RoomSweepGpsTrailPoint;

typedef struct {
    RoomSweepGpsTrailPoint pts[ROOM_SWEEP_GPS_TRAIL_MAX];
    uint8_t head; /* next write slot; when full, head is the oldest point */
    uint8_t count;
} RoomSweepGpsTrail;

static inline void room_sweep_gps_trail_clear(RoomSweepGpsTrail* t) {
    t->head = 0;
    t->count = 0;
}

static inline void room_sweep_gps_trail_push(
    RoomSweepGpsTrail* t,
    int32_t lat_e6,
    int32_t lon_e6,
    uint32_t tick) {
    if(t->count > 0) {
        uint8_t newest =
            (uint8_t)((t->head + ROOM_SWEEP_GPS_TRAIL_MAX - 1) %
                      ROOM_SWEEP_GPS_TRAIL_MAX);
        int32_t dlat = lat_e6 - t->pts[newest].lat_e6;
        int32_t dlon = lon_e6 - t->pts[newest].lon_e6;
        /* Within ~2 m of the newest point in both axes: skip entirely. */
        if(dlat <= 18 && dlat >= -18 && dlon <= 18 && dlon >= -18) {
            return;
        }
    }
    t->pts[t->head].lat_e6 = lat_e6;
    t->pts[t->head].lon_e6 = lon_e6;
    t->pts[t->head].tick = tick;
    t->head = (uint8_t)((t->head + 1) % ROOM_SWEEP_GPS_TRAIL_MAX);
    if(t->count < ROOM_SWEEP_GPS_TRAIL_MAX) t->count++;
}

/* age 0 = newest, age ROOM_SWEEP_GPS_TRAIL_MAX-1 = oldest; NULL past count. */
static inline const RoomSweepGpsTrailPoint* room_sweep_gps_trail_point_at(
    const RoomSweepGpsTrail* t,
    uint8_t age) {
    if(age >= t->count) return NULL;
    uint8_t index =
        (uint8_t)((t->head + ROOM_SWEEP_GPS_TRAIL_MAX - 1 - age) %
                  ROOM_SWEEP_GPS_TRAIL_MAX);
    return &t->pts[index];
}

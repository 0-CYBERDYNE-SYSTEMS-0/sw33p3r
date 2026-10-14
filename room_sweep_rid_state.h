#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "room_sweep_rid.h"

/*
 * Remote ID (air vehicle) list / track state — staleness, alert edges,
 * selection, per-session ordinals, and event rate limiting.
 *
 * Contract (specs/drone-rid.md):
 *  - Track table is bounded (8 rows fit the 128x64 list page); a full table
 *    refuses new identities honestly (counted, never silently evicted).
 *  - A tracked drone goes STALE after ROOM_SWEEP_RID_STALE_MS without any
 *    message, and the alert edge set is exactly NEW DRONE / RETURNING DRONE /
 *    OPERATOR MOVED (plus the STALE transition row). Edges feed the existing
 *    feedback ladder; rows feed the session recorder.
 *  - RETURNING covers two honest cases: within-session (a STALE drone heard
 *    again) and cross-session (first sighting this session of an identity on
 *    the opt-in watchlist). A watchlist match is NOT proof of the same
 *    physical drone session-over-session without a sighting gap model — the
 *    edge text says so and nothing stronger is ever claimed.
 *  - OPERATOR MOVED is an EDGE, never a distance claim: it fires when the
 *    broadcast operator position changes by at least one per-axis threshold.
 *    Longitude metres vary with latitude (no trig is available — see
 *    FLIPPER_PITFALLS #19), so the threshold is documented in microdegree
 *    steps: ROOM_SWEEP_RID_OP_MOVE_E6 (~50 m of latitude, less of longitude
 *    away from the equator — conservative direction for an alert).
 *  - Event rate limiting: at most ONE session row per drone per
 *    ROOM_SWEEP_RID_EVENT_MIN_MS, plus always-on state-change rows. The
 *    staleness sweep emits at most ONE transition per call, so a whole fleet
 *    going quiet can never burst-drop the 8-slot record queue.
 *  - Identity: per-session ordinals DR-nn for every screen and session row.
 *    The raw serial is NEVER formatted for display or CSV here — callers map
 *    it through the record fingerprint path (room_sweep_record_state.h).
 *
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * Integer math only (-Wdouble-promotion is fatal).
 */

#define ROOM_SWEEP_RID_MAX_TRACKS 8
/* STALE after 15 s without any message from the drone. */
#define ROOM_SWEEP_RID_STALE_MS 15000U
/* Observation rows: one per drone per 5 s (state-change rows are exempt). */
#define ROOM_SWEEP_RID_EVENT_MIN_MS 5000U
/* Operator-move edge threshold, microdegrees per axis (~50 m latitude). */
#define ROOM_SWEEP_RID_OP_MOVE_E6 450
/* Per-session ordinal ceiling: "DR-nn" is a two-digit contract. */
#define ROOM_SWEEP_RID_ORDINAL_MAX 99

typedef enum {
    RoomSweepRidEdgeNone = 0,
    RoomSweepRidEdgeNew,           /* NEW DRONE */
    RoomSweepRidEdgeReturning,     /* RETURNING DRONE */
    RoomSweepRidEdgeOperatorMoved, /* OPERATOR MOVED */
    RoomSweepRidEdgeStale,         /* STALE (drifted quiet) */
} RoomSweepRidEdge;

typedef struct {
    char id[ROOM_SWEEP_RID_ID_MAX + 1U]; /* identity key (raw; never displayed) */
    uint8_t ordinal; /* 1..99 -> "DR-nn" */
    uint8_t id_type;
    uint8_t ua_type;
    int rssi; /* last broadcast RSSI if the line carried one */
    bool has_rssi;
    int32_t alt_cm;
    bool has_alt;
    int32_t speed_cms;
    bool has_speed;
    int heading_deg;
    bool has_heading;
    int32_t vspeed_cms;
    bool has_vspeed;
    int32_t op_lat_e6;
    int32_t op_lon_e6;
    bool has_op_pos;
    int32_t op_alt_cm;
    bool has_op_alt;
    uint8_t op_moves; /* operator-move edges seen (saturating) */
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    uint32_t last_row_ms; /* rate-limit stamp for observation rows */
    uint16_t messages;    /* messages folded into this row (saturating) */
    bool stale;
    bool watchlisted; /* identity matched the opt-in watchlist */
    bool valid;
} RoomSweepRidTrack;

typedef struct {
    RoomSweepRidTrack tracks[ROOM_SWEEP_RID_MAX_TRACKS];
    uint8_t count;
    uint8_t next_ordinal;
    uint8_t selected; /* list cursor into tracks[] */
    uint32_t unattributed; /* parsed messages with no usable ID */
    uint32_t refused;      /* messages refused because the table is full */
} RoomSweepRidState;

static inline void room_sweep_rid_state_reset(RoomSweepRidState* s) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->next_ordinal = 1;
}

/* "DR-01" style ordinal text (8-byte buffer: 5 chars + NUL). */
static inline void room_sweep_rid_ordinal_text(uint8_t ordinal, char* out, size_t out_size) {
    if(!out || out_size == 0) return;
    out[0] = '\0';
    if(ordinal == 0) return; /* no ordinal assigned — never "DR-00" */
    unsigned shown = ordinal > ROOM_SWEEP_RID_ORDINAL_MAX ? ROOM_SWEEP_RID_ORDINAL_MAX :
                                                            (unsigned)ordinal;
    snprintf(out, out_size, "DR-%02u", shown);
}

static inline int room_sweep_rid_state_find(const RoomSweepRidState* s, const char* id) {
    if(!s || !id || !id[0]) return -1;
    for(uint8_t i = 0; i < s->count && i < ROOM_SWEEP_RID_MAX_TRACKS; i++) {
        if(s->tracks[i].valid && strcmp(s->tracks[i].id, id) == 0) return (int)i;
    }
    return -1;
}

/* List cursor: clamp to the current row count (0 rows -> cursor 0). */
static inline void room_sweep_rid_state_select(RoomSweepRidState* s, uint8_t index) {
    if(!s) return;
    if(s->count == 0) {
        s->selected = 0;
        return;
    }
    s->selected = index < s->count ? index : (uint8_t)(s->count - 1U);
}

/* Step the cursor one row (wraps); returns the new cursor. */
static inline uint8_t room_sweep_rid_state_step(RoomSweepRidState* s, bool down) {
    if(!s || s->count == 0) return 0;
    if(down) {
        s->selected = (uint8_t)((s->selected + 1U) % s->count);
    } else {
        s->selected = s->selected == 0 ? (uint8_t)(s->count - 1U) :
                                         (uint8_t)(s->selected - 1U);
    }
    return s->selected;
}

/*
 * Operator-move edge test: |dlat| or |dlon| at or over the per-axis
 * microdegree threshold. It is an EDGE only — never print a distance from it.
 */
static inline bool room_sweep_rid_op_moved(int32_t lat_a, int32_t lon_a, int32_t lat_b, int32_t lon_b) {
    int32_t dlat = lat_a > lat_b ? lat_a - lat_b : lat_b - lat_a;
    int32_t dlon = lon_a > lon_b ? lon_a - lon_b : lon_b - lon_a;
    return dlat >= ROOM_SWEEP_RID_OP_MOVE_E6 || dlon >= ROOM_SWEEP_RID_OP_MOVE_E6;
}

/*
 * Fold one parsed RID message into the track table.
 *
 * `watchlisted` is the caller's opt-in watchlist match for this identity
 * (cross-session RETURNING semantics; see header contract). Returns the row
 * index, or -1 when the message cannot be attributed (no/empty ID — counted
 * in unattributed, never guessed onto a neighbour row) or the table is full
 * (counted in refused).
 *
 * `*edge` receives the alert edge (None for a plain update). `*log_row`
 * receives true when this fold must produce a session row: always for a new
 * row or a state change, otherwise at most once per
 * ROOM_SWEEP_RID_EVENT_MIN_MS per drone.
 */
static inline int room_sweep_rid_state_observe(
    RoomSweepRidState* s,
    const RoomSweepRidRecord* rec,
    uint32_t now_ms,
    bool watchlisted,
    RoomSweepRidEdge* edge,
    bool* log_row) {
    RoomSweepRidEdge out_edge = RoomSweepRidEdgeNone;
    bool out_log = false;
    if(edge) *edge = RoomSweepRidEdgeNone;
    if(log_row) *log_row = false;
    if(!s || !rec) return -1;
    if(!rec->id_present || !rec->id[0]) {
        /* Never guess which drone an unidentified message belongs to. */
        if(s->unattributed < UINT32_MAX) s->unattributed++;
        return -1;
    }

    int found = room_sweep_rid_state_find(s, rec->id);
    if(found < 0) {
        if(s->count >= ROOM_SWEEP_RID_MAX_TRACKS) {
            if(s->refused < UINT32_MAX) s->refused++;
            return -1;
        }
        RoomSweepRidTrack* row = &s->tracks[s->count];
        memset(row, 0, sizeof(*row));
        strncpy(row->id, rec->id, ROOM_SWEEP_RID_ID_MAX);
        row->id[ROOM_SWEEP_RID_ID_MAX] = '\0';
        row->ordinal = s->next_ordinal;
        if(s->next_ordinal < 255U) s->next_ordinal++;
        row->first_seen_ms = now_ms;
        row->valid = true; /* publish the row before the shared count grows */
        found = s->count;
        s->count++;
        room_sweep_rid_state_select(s, s->selected); /* clamp cursor at 0 rows */
        /* Cross-session RETURNING: first sighting this session of a
         * watchlisted identity. Honesty: a matching serial is not proof
         * of the same physical drone without a sighting gap model. */
        out_edge = watchlisted ? RoomSweepRidEdgeReturning : RoomSweepRidEdgeNew;
        out_log = true;
    } else {
        RoomSweepRidTrack* row = &s->tracks[found];
        if(row->stale) {
            /* Within-session RETURNING: quiet long enough to go stale. */
            row->stale = false;
            out_edge = RoomSweepRidEdgeReturning;
            out_log = true;
        }
    }

    RoomSweepRidTrack* row = &s->tracks[found];
    row->last_seen_ms = now_ms;
    if(row->messages < UINT16_MAX) row->messages++;
    if(rec->id_type != 0xFF) row->id_type = rec->id_type;
    if(rec->ua_type != 0xFF) row->ua_type = rec->ua_type;
    if(rec->has_rssi) {
        row->rssi = rec->rssi;
        row->has_rssi = true;
    }
    if(rec->has_alt) {
        row->alt_cm = rec->alt_cm;
        row->has_alt = true;
    }
    if(rec->has_speed) {
        row->speed_cms = rec->speed_cms;
        row->has_speed = true;
    }
    if(rec->has_heading) {
        row->heading_deg = rec->heading_deg;
        row->has_heading = true;
    }
    if(rec->has_vspeed) {
        row->vspeed_cms = rec->vspeed_cms;
        row->has_vspeed = true;
    }
    if(rec->has_op_latlon) {
        if(row->has_op_pos &&
           room_sweep_rid_op_moved(row->op_lat_e6, row->op_lon_e6, rec->op_lat_e6, rec->op_lon_e6)) {
            /* A state-change row is never rate-limited. */
            if(row->op_moves < 255U) row->op_moves++;
            if(out_edge == RoomSweepRidEdgeNone) out_edge = RoomSweepRidEdgeOperatorMoved;
            out_log = true;
        }
        row->op_lat_e6 = rec->op_lat_e6;
        row->op_lon_e6 = rec->op_lon_e6;
        row->has_op_pos = true;
    }
    if(rec->has_op_alt) {
        row->op_alt_cm = rec->op_alt_cm;
        row->has_op_alt = true;
    }
    if(watchlisted) row->watchlisted = true;

    /* Observation-row rate limit (state-change rows bypass it above). */
    if(!out_log && (uint32_t)(now_ms - row->last_row_ms) >= ROOM_SWEEP_RID_EVENT_MIN_MS) {
        out_log = true;
    }
    if(out_log) row->last_row_ms = now_ms;
    if(edge) *edge = out_edge;
    if(log_row) *log_row = out_log;
    return found;
}

/*
 * Staleness sweep: mark at most ONE drone STALE per call (so a fleet going
 * quiet can never burst the 8-slot record queue) and report its transition.
 * Returns true while a transition was emitted; the caller loops until false.
 */
static inline bool room_sweep_rid_state_tick(
    RoomSweepRidState* s,
    uint32_t now_ms,
    uint8_t* out_index,
    RoomSweepRidEdge* out_edge) {
    if(!s) return false;
    for(uint8_t i = 0; i < s->count && i < ROOM_SWEEP_RID_MAX_TRACKS; i++) {
        RoomSweepRidTrack* row = &s->tracks[i];
        if(!row->valid || row->stale) continue;
        if((uint32_t)(now_ms - row->last_seen_ms) < ROOM_SWEEP_RID_STALE_MS) continue;
        row->stale = true;
        if(out_index) *out_index = i;
        if(out_edge) *out_edge = RoomSweepRidEdgeStale;
        return true;
    }
    return false;
}

/* Alert-edge text (UI + feedback). STALE rows say what is known: silence,
 * not loss — the drone may simply be out of range or quiet. */
static inline const char* room_sweep_rid_edge_text(RoomSweepRidEdge edge) {
    switch(edge) {
    case RoomSweepRidEdgeNew:
        return "NEW DRONE";
    case RoomSweepRidEdgeReturning:
        return "RETURNING DRONE";
    case RoomSweepRidEdgeOperatorMoved:
        return "OPERATOR MOVED";
    case RoomSweepRidEdgeStale:
        return "STALE";
    case RoomSweepRidEdgeNone:
    default:
        return "";
    }
}

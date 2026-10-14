#pragma once

/*
 * Host-testable FieldSweep policy (specs/field-sweep.md) — "room sweep, but
 * outside": a continuous walking/driving survey where every detection is
 * GPS-stamped and coverage is honest about where the OPERATOR actually was.
 *
 * Flipper-header-free. The full-sweep phase machine (room_sweep_full_sweep.h)
 * still owns phase sequencing and timeouts; this header decides:
 *   1. loop transition   — when a finished pass wraps to RF instead of done
 *   2. rate-limit policy — movement-aware, new-device-first sampling
 *   3. coordinate-token eligibility — which rows may carry a position token
 *   4. refusal state     — FieldSweep ON with GPS Log OFF is refused loudly
 *
 * Privacy contract (MI1): coordinates reach CSVs ONLY with GPS Log ON.
 * FieldSweep REQUIRES GPS Log ON — enabling it otherwise is REFUSED with a
 * clear message, never a silent coordinate drop. The 64-entry identity map's
 * honest REDACTED overflow behavior is untouched and stays as-is.
 * No TX interaction of any kind (same as FullSweep).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "room_sweep_full_sweep.h"
#include "room_sweep_radar.h" /* integer great-circle distance */

/* ------------------------------------------------------------------ */
/* 1. Loop-transition policy                                           */
/* ------------------------------------------------------------------ */

/*
 * True when finishing `phase` under FieldSweep must wrap back to RF instead
 * of ending the sweep. Only the last sensor phase wraps; Idle/Done/Aborted
 * never do. Back stops the loop (the caller stops the state below), so this
 * policy is a pure function of (field running, current phase).
 */
static inline bool room_sweep_field_loop_restart(bool field_running, RoomSweepFullPhase phase) {
    if(!field_running) return false;
    if(phase == RoomSweepFullIdle || phase == RoomSweepFullDone || phase == RoomSweepFullAborted)
        return false;
    return room_sweep_full_sweep_next_after(phase) == RoomSweepFullDone;
}

/* ------------------------------------------------------------------ */
/* 2. Movement-aware rate-limit policy                                 */
/* ------------------------------------------------------------------ */

/*
 * Pace is passed in deci-km/h (integer math only; the runtime converts the
 * NMEA speed once). Walking pace ceiling is 5.0 km/h.
 */
#define ROOM_SWEEP_FIELD_WALKING_DKH 50U
#define ROOM_SWEEP_FIELD_STATIONARY_DKH 5U /* below 0.5 km/h = standing */

typedef enum {
    RoomSweepFieldPaceStationary = 0,
    RoomSweepFieldPaceWalking,
    RoomSweepFieldPaceFast, /* above walking pace: running / cycling / driving */
} RoomSweepFieldPace;

typedef enum {
    RoomSweepFieldRowNew = 0, /* first sighting of an identity */
    RoomSweepFieldRowSeen,    /* identity already logged this session */
    RoomSweepFieldRowClassCount,
} RoomSweepFieldRowClass;

static inline RoomSweepFieldPace room_sweep_field_pace(uint32_t speed_dkh) {
    if(speed_dkh > ROOM_SWEEP_FIELD_WALKING_DKH) return RoomSweepFieldPaceFast;
    if(speed_dkh >= ROOM_SWEEP_FIELD_STATIONARY_DKH) return RoomSweepFieldPaceWalking;
    return RoomSweepFieldPaceStationary;
}

/*
 * Minimum ms between accepted CSV rows of one class (tracked per source by
 * the caller). New-device-first: a new identity is always worth a row, so its
 * limit is small; already-seen rows are limited much harder as speed rises so
 * a long walk cannot flood the 8-slot record queue (burst-drop fix: small
 * group drains + rate limiting) or the 256 KiB session budget. The walking
 * seen limit matches the existing RF LOG_HIT_MIN_MS throttle.
 */
#define ROOM_SWEEP_FIELD_MIN_NEW_MS 250U
#define ROOM_SWEEP_FIELD_MIN_SEEN_WALK_MS 1500U
#define ROOM_SWEEP_FIELD_MIN_SEEN_FAST_MS 8000U

static inline uint32_t room_sweep_field_min_interval_ms(
    RoomSweepFieldPace pace,
    RoomSweepFieldRowClass cls) {
    if(cls == RoomSweepFieldRowNew) return ROOM_SWEEP_FIELD_MIN_NEW_MS;
    if(pace == RoomSweepFieldPaceFast) return ROOM_SWEEP_FIELD_MIN_SEEN_FAST_MS;
    return ROOM_SWEEP_FIELD_MIN_SEEN_WALK_MS;
}

/* ms_since_last == UINT32_MAX means "no row of this class accepted yet". */
static inline bool room_sweep_field_row_allowed(
    RoomSweepFieldPace pace,
    RoomSweepFieldRowClass cls,
    uint32_t ms_since_last) {
    return ms_since_last >= room_sweep_field_min_interval_ms(pace, cls);
}

/* ------------------------------------------------------------------ */
/* 3. Coordinate-token eligibility matrix                              */
/* ------------------------------------------------------------------ */

/*
 * Which row kinds may carry the latest-fix coordinate token in their
 * observation detail. The token exists so a CSV row says WHERE the operator
 * was — that is meaningful for evidence and window/lifecycle rows and
 * misleading for the two excluded kinds:
 *   - drop rows are written at drain time, so a stamp would claim a position
 *     for evidence that was LOST elsewhere;
 *   - TX rows are never stamped: FieldSweep is detect-only and a coordinate
 *     on a TX row would read like target coordinates.
 */
typedef enum {
    RoomSweepFieldRowEvidence = 0, /* observation / snapshot / hidden_resolved */
    RoomSweepFieldRowScan,         /* scan_start / scan_end window markers */
    RoomSweepFieldRowConfig,       /* config + lifecycle rows */
    RoomSweepFieldRowDrop,         /* drop accounting rows — never stamped */
    RoomSweepFieldRowTx,           /* TX rows — never stamped */
    RoomSweepFieldRowKindCount,
} RoomSweepFieldRowKind;

/*
 * Eligibility = row kind allows it AND GPS Log is ON AND a usable fix exists.
 * BOTH gates are required: GPS Log OFF can never emit coordinates (privacy),
 * and a fix without parsed coordinates is the NO POS state — stamping it
 * would fabricate a position.
 */
static inline bool room_sweep_field_coord_eligible(
    RoomSweepFieldRowKind kind,
    bool gps_log_on,
    bool has_fix) {
    if(!gps_log_on || !has_fix) return false;
    switch(kind) {
    case RoomSweepFieldRowEvidence:
    case RoomSweepFieldRowScan:
    case RoomSweepFieldRowConfig:
        return true;
    case RoomSweepFieldRowDrop:
    case RoomSweepFieldRowTx:
    default:
        return false;
    }
}

/*
 * Coordinate token for an observation detail: "pos=<lat>/<lon>/<alt>" in
 * decimal degrees (same values the row's lat/lon columns carry) with '/' as
 * the separator — the detail column is an unquoted CSV field, so commas are
 * impossible. Altitude is NOT parsed by this build's NMEA parser (nmea.c has
 * no alt field), so has_alt=false prints the honest "na"; the slot keeps the
 * lat/lon/alt shape stable and no altitude is ever invented. Returns the
 * length written, or -1 on a too-small buffer (out is then empty).
 */
#define ROOM_SWEEP_FIELD_COORD_TOKEN_MAX 40

static inline int room_sweep_field_coord_token(
    char* out,
    size_t out_size,
    float latitude,
    float longitude,
    bool has_alt,
    int32_t alt_cm) {
    if(!out || out_size == 0) return -1;
    out[0] = '\0';
    int written;
    if(has_alt) {
        written = snprintf(
            out,
            out_size,
            "pos=%.6f/%.6f/%ld",
            (double)latitude,
            (double)longitude,
            (long)alt_cm);
    } else {
        written =
            snprintf(out, out_size, "pos=%.6f/%.6f/na", (double)latitude, (double)longitude);
    }
    if(written <= 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return -1;
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* 4. GPS-Log-OFF refusal state + run stats                            */
/* ------------------------------------------------------------------ */

typedef enum {
    RoomSweepFieldToggleStopped = 0, /* toggled OFF (loop stops) */
    RoomSweepFieldToggleStarted,     /* toggled ON */
    RoomSweepFieldToggleRefused,     /* ON refused: GPS Log is OFF */
} RoomSweepFieldToggleResult;

/* The clear refusal message — shown in Settings and written to the CSV. */
static inline const char* room_sweep_field_refusal_text(RoomSweepFieldToggleResult result) {
    switch(result) {
    case RoomSweepFieldToggleRefused:
        return "FieldSweep needs GPS Log ON";
    case RoomSweepFieldToggleStarted:
        return "FieldSweep ON: looping GPS-stamped survey";
    case RoomSweepFieldToggleStopped:
    default:
        return "FieldSweep OFF";
    }
}

/*
 * Run stats for the report "Field sweep" section. Distance is whole meters
 * (the GPS haversine summed over the fix path); positions are integer e6 like
 * the GPS trail. The bbox is the bounding box of the OPERATOR'S PATH — never
 * of the emitters.
 */
typedef struct {
    bool enabled;   /* the loop runs while true */
    bool ran;       /* a field run happened at least once */
    bool refused;   /* last ON attempt refused (GPS Log OFF) */
    bool coords_recorded; /* bbox/distance were recorded with GPS Log ON */
    uint16_t loops; /* completed full phase passes (wraps) */
    uint32_t start_tick;
    uint32_t active_ms; /* accumulated running time across runs */
    uint32_t fixes_used;
    uint32_t distance_m; /* walked path, integer meters */
    bool have_fix;
    int32_t lat_e6;
    int32_t lon_e6;
    bool have_bbox;
    int32_t min_lat_e6;
    int32_t max_lat_e6;
    int32_t min_lon_e6;
    int32_t max_lon_e6;
} RoomSweepFieldState;

static inline void room_sweep_field_init(RoomSweepFieldState* s) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
}

static inline bool room_sweep_field_is_running(const RoomSweepFieldState* s) {
    return s && s->enabled;
}

static inline void room_sweep_field_clear_refusal(RoomSweepFieldState* s) {
    if(s) s->refused = false;
}

/* Back / settings / GPS-Log-off all stop the loop through here. */
static inline void room_sweep_field_stop(RoomSweepFieldState* s, uint32_t now_tick) {
    if(!s) return;
    if(s->enabled) {
        /* Unsigned subtraction stays correct across tick wrap. */
        s->active_ms += (uint32_t)(now_tick - s->start_tick);
        s->enabled = false;
    }
}

/*
 * Toggle with the GPS-Log gate. Turning ON with GPS Log OFF is REFUSED —
 * the state records the refusal so Settings can show it; coordinates are
 * never written and never silently dropped.
 */
static inline RoomSweepFieldToggleResult room_sweep_field_toggle(
    RoomSweepFieldState* s,
    bool gps_log_on,
    uint32_t now_tick) {
    if(!s) return RoomSweepFieldToggleStopped;
    if(s->enabled) {
        room_sweep_field_stop(s, now_tick);
        return RoomSweepFieldToggleStopped;
    }
    if(!gps_log_on) {
        s->refused = true;
        return RoomSweepFieldToggleRefused;
    }
    s->refused = false;
    s->enabled = true;
    s->ran = true;
    s->start_tick = now_tick;
    return RoomSweepFieldToggleStarted;
}

/* One full phase pass finished and wrapped. */
static inline void room_sweep_field_note_loop(RoomSweepFieldState* s) {
    if(s && s->loops < UINT16_MAX) s->loops++;
}

/* Total running time including the current run (ms). */
static inline uint32_t room_sweep_field_duration_ms(
    const RoomSweepFieldState* s,
    uint32_t now_tick) {
    if(!s) return 0;
    uint32_t total = s->active_ms;
    if(s->enabled) total += (uint32_t)(now_tick - s->start_tick);
    return total;
}

/*
 * Walked-distance step in whole meters between two e6 fixes. The policy
 * state keeps only integer meters.
 */
static inline uint32_t room_sweep_field_step_m(
    int32_t lat1_e6,
    int32_t lon1_e6,
    int32_t lat2_e6,
    int32_t lon2_e6) {
    return room_sweep_geo_distance_m(lat1_e6, lon1_e6, lat2_e6, lon2_e6);
}

/*
 * Record a fresh usable fix: path distance, operator-path bbox, fix count.
 * `gps_log_on` is the privacy gate — a fix with GPS Log OFF records NOTHING
 * (that would be coordinates without permission). Returns true when the fix
 * was used. Only counts while the loop runs.
 */
static inline bool room_sweep_field_note_fix(
    RoomSweepFieldState* s,
    int32_t lat_e6,
    int32_t lon_e6,
    bool gps_log_on) {
    if(!s || !s->enabled || !gps_log_on) return false;
    if(s->have_fix) {
        s->distance_m += room_sweep_field_step_m(s->lat_e6, s->lon_e6, lat_e6, lon_e6);
    } else {
        s->have_bbox = true;
        s->min_lat_e6 = lat_e6;
        s->max_lat_e6 = lat_e6;
        s->min_lon_e6 = lon_e6;
        s->max_lon_e6 = lon_e6;
    }
    if(s->have_fix) {
        if(lat_e6 < s->min_lat_e6) s->min_lat_e6 = lat_e6;
        if(lat_e6 > s->max_lat_e6) s->max_lat_e6 = lat_e6;
        if(lon_e6 < s->min_lon_e6) s->min_lon_e6 = lon_e6;
        if(lon_e6 > s->max_lon_e6) s->max_lon_e6 = lon_e6;
    }
    s->have_fix = true;
    s->coords_recorded = true;
    s->lat_e6 = lat_e6;
    s->lon_e6 = lon_e6;
    if(s->fixes_used < UINT32_MAX) s->fixes_used++;
    return true;
}

/*
 * Banner strip text like FullSweep's: "FIELD nN <phase> <dist>m".
 * Returns the length written, or -1 on a too-small buffer.
 */
static inline int room_sweep_field_banner(
    char* out,
    size_t out_size,
    uint16_t loop_number,
    const char* phase_label,
    uint32_t distance_m) {
    if(!out || out_size == 0) return -1;
    out[0] = '\0';
    int written = snprintf(
        out,
        out_size,
        "FIELD n%u %s %lum",
        (unsigned)loop_number,
        phase_label ? phase_label : "?",
        (unsigned long)distance_m);
    if(written <= 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return -1;
    }
    return written;
}

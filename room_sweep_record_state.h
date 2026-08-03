#pragma once

/*
 * Pure recorder state helpers.
 *
 * This header deliberately has no Furi, storage, or application dependencies.
 * The runtime recorder can adapt these bounded decisions to Flipper storage,
 * while host tests exercise naming, redaction, and lifecycle semantics.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROOM_SWEEP_RECORD_DEFAULT_MAX_RECORDS 2048U
#define ROOM_SWEEP_RECORD_DEFAULT_MAX_BYTES (256U * 1024U)
#define ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP 32U
#define ROOM_SWEEP_RECORD_NAME_MAX 48U

typedef enum {
    RoomSweepRecordFileSession = 0,
    RoomSweepRecordFileUart,
    RoomSweepRecordFileReport,
    RoomSweepRecordFileCount,
} RoomSweepRecordFileKind;

/* Return a collision-resistant, non-sensitive filename for one artifact. */
static inline bool room_sweep_record_filename(
    RoomSweepRecordFileKind kind,
    uint32_t ordinal,
    char* out,
    size_t out_size) {
    if(!out || out_size == 0 || kind >= RoomSweepRecordFileCount) return false;

    const char* stem;
    const char* extension;
    switch(kind) {
    case RoomSweepRecordFileSession:
        stem = "session";
        extension = ".csv";
        break;
    case RoomSweepRecordFileUart:
        stem = "uart";
        extension = ".txt";
        break;
    case RoomSweepRecordFileReport:
        stem = "report";
        extension = ".txt";
        break;
    case RoomSweepRecordFileCount:
    default:
        return false;
    }

    int written = snprintf(out, out_size, "%s-%lu%s", stem, (unsigned long)ordinal, extension);
    return written >= 0 && (size_t)written < out_size;
}

typedef enum {
    RoomSweepRecordIdAccessPoint = 0,
    RoomSweepRecordIdBle,
    RoomSweepRecordIdRf,
    RoomSweepRecordIdGps,
    RoomSweepRecordIdOther,
    RoomSweepRecordIdKindCount,
} RoomSweepRecordIdKind;

typedef struct {
    uint64_t fingerprint;
    uint16_t ordinal;
    uint8_t kind;
    bool used;
} RoomSweepRecordIdentifierEntry;

typedef struct {
    RoomSweepRecordIdentifierEntry entries[ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP];
    uint16_t next_ordinal[RoomSweepRecordIdKindCount];
} RoomSweepRecordIdentifierMap;

static inline void room_sweep_record_identifier_map_init(RoomSweepRecordIdentifierMap* map) {
    if(!map) return;
    memset(map, 0, sizeof(*map));
    for(size_t i = 0; i < RoomSweepRecordIdKindCount; i++) map->next_ordinal[i] = 1;
}

/* FNV-1a over the identifier; the raw identifier is never retained. */
static inline uint64_t room_sweep_record_identifier_fingerprint(
    RoomSweepRecordIdKind kind,
    const char* raw) {
    uint64_t hash = UINT64_C(1469598103934665603) ^ (uint64_t)kind;
    for(const unsigned char* p = (const unsigned char*)raw; p && *p; p++) {
        hash ^= (uint64_t)*p;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static inline const char* room_sweep_record_identifier_prefix(RoomSweepRecordIdKind kind) {
    switch(kind) {
    case RoomSweepRecordIdAccessPoint:
        return "AP";
    case RoomSweepRecordIdBle:
        return "BLE";
    case RoomSweepRecordIdRf:
        return "RF";
    case RoomSweepRecordIdGps:
        return "GPS";
    case RoomSweepRecordIdOther:
        return "ID";
    case RoomSweepRecordIdKindCount:
    default:
        return NULL;
    }
}

/* Map an identifier to a stable per-session ordinal such as AP-01. */
static inline bool room_sweep_record_identifier_ref(
    RoomSweepRecordIdentifierMap* map,
    RoomSweepRecordIdKind kind,
    const char* raw,
    char* out,
    size_t out_size) {
    if(!out || out_size == 0 || kind >= RoomSweepRecordIdKindCount) return false;
    out[0] = '\0';
    if(!raw || !raw[0]) {
        if(out_size < 2) return false;
        out[0] = '-';
        out[1] = '\0';
        return true;
    }
    if(!map) return false;

    uint64_t fingerprint = room_sweep_record_identifier_fingerprint(kind, raw);
    for(size_t i = 0; i < ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP; i++) {
        RoomSweepRecordIdentifierEntry* entry = &map->entries[i];
        if(entry->used && entry->kind == (uint8_t)kind && entry->fingerprint == fingerprint) {
            int written = snprintf(
                out,
                out_size,
                "%s-%02u",
                room_sweep_record_identifier_prefix(kind),
                (unsigned)entry->ordinal);
            return written >= 0 && (size_t)written < out_size;
        }
    }

    size_t free_index = ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP;
    for(size_t i = 0; i < ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP; i++) {
        if(!map->entries[i].used) {
            free_index = i;
            break;
        }
    }
    if(free_index == ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP || map->next_ordinal[kind] == 0) {
        return false;
    }

    uint16_t ordinal = map->next_ordinal[kind];
    int written = snprintf(
        out,
        out_size,
        "%s-%02u",
        room_sweep_record_identifier_prefix(kind),
        (unsigned)ordinal);
    if(written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return false;
    }

    map->entries[free_index].fingerprint = fingerprint;
    map->entries[free_index].ordinal = ordinal;
    map->entries[free_index].kind = (uint8_t)kind;
    map->entries[free_index].used = true;
    map->next_ordinal[kind]++;
    return true;
}

typedef enum {
    RoomSweepRecordEndOpen = 0,
    RoomSweepRecordEndClean,
    RoomSweepRecordEndIncomplete,
} RoomSweepRecordEndStatus;

typedef struct {
    uint32_t max_records;
    uint32_t max_bytes;
    uint32_t records_written;
    uint32_t bytes_written;
    uint32_t dropped_records;
    uint32_t storage_failures;
    bool storage_failed;
    bool active;
    RoomSweepRecordEndStatus end_status;
} RoomSweepRecordState;

static inline void room_sweep_record_state_init(
    RoomSweepRecordState* state,
    uint32_t max_records,
    uint32_t max_bytes) {
    if(!state) return;
    memset(state, 0, sizeof(*state));
    state->max_records = max_records ? max_records : ROOM_SWEEP_RECORD_DEFAULT_MAX_RECORDS;
    state->max_bytes = max_bytes ? max_bytes : ROOM_SWEEP_RECORD_DEFAULT_MAX_BYTES;
    state->end_status = RoomSweepRecordEndOpen;
}

static inline bool room_sweep_record_state_begin(RoomSweepRecordState* state) {
    if(!state || state->active) return false;
    state->records_written = 0;
    state->bytes_written = 0;
    state->dropped_records = 0;
    state->storage_failures = 0;
    state->storage_failed = false;
    state->active = true;
    state->end_status = RoomSweepRecordEndOpen;
    return true;
}

static inline bool room_sweep_record_state_can_append(
    const RoomSweepRecordState* state,
    size_t bytes) {
    return state && state->active && !state->storage_failed && bytes <= UINT32_MAX &&
           state->records_written < state->max_records &&
           (uint32_t)bytes <=
               state->max_bytes -
                   (state->bytes_written <= state->max_bytes ? state->bytes_written :
                                                              state->max_bytes);
}

static inline bool room_sweep_record_state_commit_append(
    RoomSweepRecordState* state,
    size_t bytes) {
    if(!room_sweep_record_state_can_append(state, bytes)) return false;
    state->records_written++;
    state->bytes_written += (uint32_t)bytes;
    return true;
}

static inline bool room_sweep_record_state_append(RoomSweepRecordState* state, size_t bytes) {
    if(!room_sweep_record_state_commit_append(state, bytes)) {
        if(state) state->dropped_records++;
        return false;
    }
    return true;
}

static inline void room_sweep_record_state_note_storage_failure(RoomSweepRecordState* state) {
    if(!state) return;
    state->storage_failures++;
    state->storage_failed = true;
}

static inline bool room_sweep_record_state_finish(
    RoomSweepRecordState* state,
    bool clean_end_requested) {
    if(!state || !state->active) return false;
    state->active = false;
    state->end_status = clean_end_requested && !state->storage_failed
                            ? RoomSweepRecordEndClean
                            : RoomSweepRecordEndIncomplete;
    return state->end_status == RoomSweepRecordEndClean;
}

static inline bool room_sweep_record_state_is_complete(const RoomSweepRecordState* state) {
    return state && state->end_status == RoomSweepRecordEndClean;
}

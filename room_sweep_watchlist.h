#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Phase 9 — opt-in cross-session watchlist (pure logic only).
 *
 * PRIVACY CONTRACT (specs/full-capability-expansion-2026-09-20.md, Phase 9):
 * this is the app's ONLY persistent-identity feature. It is OFF by default;
 * no file is read or written until the user enables it, and the raw MACs
 * live ONLY in the user-curated watchlist file. Session CSVs keep ordinals
 * exactly as before. Entries are added solely by an explicit user flag
 * action — there is no automatic collection anywhere.
 *
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * Integer math only: the project builds with -Wdouble-promotion as an error.
 */

#define ROOM_SWEEP_WATCH_MAX 16

/* One user-flagged identity: "aa:bb:cc:dd:ee:ff" + a short operator label. */
typedef struct {
    char mac[18];
    char label[24]; /* 23 chars + NUL; longer labels are refused on load */
} RoomSweepWatchEntry;

#define ROOM_SWEEP_WATCH_LABEL_DEFAULT "flagged"
#define ROOM_SWEEP_WATCHLIST_LABEL_MAX 23

/* Longest line the loader will even attempt: "mac,label" = 17 + 1 + 23.
 * Longer lines are skipped whole, never truncated into validity. */
#define ROOM_SWEEP_WATCHLIST_LINE_MAX 48

static inline char room_sweep_watchlist_fold(char value) {
    if(value >= 'a' && value <= 'f') return (char)(value - 'a' + 'A');
    return value;
}

/* Case-insensitive MAC equality; empty strings never match (safe default). */
static inline bool room_sweep_watchlist_mac_equal(const char* left, const char* right) {
    if(!left || !right || !left[0] || !right[0]) return false;
    for(size_t i = 0;; i++) {
        char l = room_sweep_watchlist_fold(left[i]);
        char r = room_sweep_watchlist_fold(right[i]);
        if(l != r) return false;
        if(l == '\0') return true;
    }
}

/*
 * Case-insensitive exact MAC compare against one entry. Never matches an
 * empty entry MAC or an empty observation MAC.
 */
static inline bool room_sweep_watchlist_match(const RoomSweepWatchEntry* e, const char* mac) {
    if(!e) return false;
    return room_sweep_watchlist_mac_equal(e->mac, mac);
}

static inline bool room_sweep_watchlist_is_hex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

/* Strict colon-MAC shape: exactly aa:bb:cc:dd:ee:ff (any hex case). */
static inline bool room_sweep_watchlist_mac_shape(const char* mac) {
    if(!mac) return false;
    size_t i = 0;
    for(uint8_t group = 0; group < 6; group++) {
        for(uint8_t j = 0; j < 2; j++) {
            if(!room_sweep_watchlist_is_hex(mac[i++])) return false;
        }
        if(group < 5 && mac[i++] != ':') return false;
    }
    return mac[i] == '\0';
}

/*
 * Parse one "aa:bb:cc:dd:ee:ff,label" line.
 *
 * Tolerant of: a trailing '\n' or '\r', surrounding spaces/tabs on both
 * fields, blank lines, and leading-'#' comment lines — all of which yield
 * false (the loader simply skips them). A missing or empty label becomes
 * "flagged". Returns false (and writes nothing) on garbage: a malformed or
 * overlong MAC, or a label longer than 23 chars. Commas inside the label
 * are kept — the MAC is fixed-length, so the FIRST comma is the separator
 * and such lines round-trip through the file.
 */
static inline bool room_sweep_watchlist_parse_line(const char* line, RoomSweepWatchEntry* out) {
    if(!line || !out) return false;
    size_t len = 0;
    while(line[len] != '\0' && line[len] != '\n') len++;

    size_t comma = len;
    bool has_comma = false;
    for(size_t i = 0; i < len; i++) {
        if(line[i] == ',') {
            comma = i;
            has_comma = true;
            break;
        }
    }

    /* Trim the MAC field (leading '#' after trim marks a comment line). */
    size_t begin = 0;
    size_t end = has_comma ? comma : len;
    while(begin < end &&
          (line[begin] == ' ' || line[begin] == '\t' || line[begin] == '\r'))
        begin++;
    while(end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r'))
        end--;
    if(begin >= end) return false; /* blank line */
    if(line[begin] == '#') return false; /* comment line */
    if(end - begin != 17) return false; /* malformed or overlong MAC */
    char mac[18];
    memcpy(mac, line + begin, 17);
    mac[17] = '\0';
    if(!room_sweep_watchlist_mac_shape(mac)) return false;

    /* Trim the label field; refuse overlong labels instead of truncating
     * (a truncated identity file must never silently differ from disk). */
    size_t lab_begin = 0;
    size_t lab_end = 0;
    if(has_comma) {
        lab_begin = comma + 1;
        lab_end = len;
        while(lab_begin < lab_end &&
              (line[lab_begin] == ' ' || line[lab_begin] == '\t' || line[lab_begin] == '\r'))
            lab_begin++;
        while(lab_end > lab_begin &&
              (line[lab_end - 1] == ' ' || line[lab_end - 1] == '\t' ||
               line[lab_end - 1] == '\r'))
            lab_end--;
        if(lab_end - lab_begin > ROOM_SWEEP_WATCHLIST_LABEL_MAX) return false;
    }

    memcpy(out->mac, mac, sizeof(out->mac));
    if(has_comma && lab_end > lab_begin) {
        memcpy(out->label, line + lab_begin, lab_end - lab_begin);
        out->label[lab_end - lab_begin] = '\0';
    } else {
        strcpy(out->label, ROOM_SWEEP_WATCH_LABEL_DEFAULT);
    }
    return true;
}

/*
 * Copy an observed SSID/name into the 23-char label field for a new flag.
 * Control characters (\n \r \t) become '_' so the file line stays intact;
 * commas are kept (parse splits on the FIRST comma, after the fixed-length
 * MAC, so they round-trip). Empty results become "flagged".
 */
static inline void room_sweep_watchlist_copy_label(char* label, size_t cap, const char* src) {
    if(!label || cap == 0) return;
    size_t used = 0;
    if(src) {
        for(size_t i = 0; src[i] != '\0' && used + 1U < cap; i++) {
            char value = src[i];
            if(value == '\n' || value == '\r' || value == '\t') value = '_';
            label[used++] = value;
        }
    }
    while(used > 0 && label[used - 1] == ' ') used--;
    if(used == 0) {
        strncpy(label, ROOM_SWEEP_WATCH_LABEL_DEFAULT, cap - 1U);
        label[cap - 1U] = '\0';
    } else {
        label[used] = '\0';
    }
}

/* The in-RAM watchlist (bounded; the file on disk may hold more). */
typedef struct {
    RoomSweepWatchEntry entries[ROOM_SWEEP_WATCH_MAX];
    uint8_t count;
    bool full_seen; /* a valid flag was refused because the list was full */
} RoomSweepWatchlist;

static inline void room_sweep_watchlist_reset(RoomSweepWatchlist* list) {
    if(!list) return;
    memset(list, 0, sizeof(*list));
}

/* Index of the entry matching mac, or -1. */
static inline int room_sweep_watchlist_find(const RoomSweepWatchlist* list, const char* mac) {
    if(!list || !mac || !mac[0]) return -1;
    for(uint8_t i = 0; i < list->count && i < ROOM_SWEEP_WATCH_MAX; i++) {
        if(room_sweep_watchlist_match(&list->entries[i], mac)) return (int)i;
    }
    return -1;
}

/*
 * Append one entry. False when the entry is unusable, a case-insensitive
 * duplicate, or the list is full (full_seen then records the overflow).
 */
static inline bool room_sweep_watchlist_add(RoomSweepWatchlist* list, const RoomSweepWatchEntry* e) {
    if(!list || !e || !e->mac[0]) return false;
    if(room_sweep_watchlist_find(list, e->mac) >= 0) return false;
    if(list->count >= ROOM_SWEEP_WATCH_MAX) {
        list->full_seen = true;
        return false;
    }
    list->entries[list->count++] = *e;
    return true;
}

/*
 * Feed a raw file buffer (whole bounded read; the trailing bytes need not
 * be NUL-terminated). Splits on '\n', tolerates a missing final newline,
 * and skips overlong lines whole. Stops accepting at ROOM_SWEEP_WATCH_MAX
 * (full_seen records that the FILE held more than fits in RAM).
 */
static inline void room_sweep_watchlist_load_buffer(
    RoomSweepWatchlist* list,
    const char* data,
    size_t len) {
    if(!list || !data) return;
    size_t start = 0;
    for(size_t i = 0; i <= len; i++) {
        if(i != len && data[i] != '\n') continue;
        size_t line_len = i - start;
        if(line_len > 0 && line_len < ROOM_SWEEP_WATCHLIST_LINE_MAX) {
            char line[ROOM_SWEEP_WATCHLIST_LINE_MAX];
            memcpy(line, data + start, line_len);
            line[line_len] = '\0';
            RoomSweepWatchEntry entry;
            if(room_sweep_watchlist_parse_line(line, &entry)) {
                (void)room_sweep_watchlist_add(list, &entry);
            }
        }
        start = i + 1;
    }
}

/* Popcount over the 16-bit per-entry hit mask (distinct matches this session). */
static inline uint32_t room_sweep_watchlist_hit_count(uint16_t hit_mask) {
    uint32_t count = 0;
    for(uint8_t i = 0; i < ROOM_SWEEP_WATCH_MAX; i++) {
        if((hit_mask & (uint16_t)(1U << i)) != 0) count++;
    }
    return count;
}

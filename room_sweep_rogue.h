#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Phase 6 — duplicate-SSID / possible-rogue correlation. Pure analysis over
 * the Wi-Fi AP table the app already captures: same advertised name heard
 * from different BSSIDs (evil twin, cloned hotspot, mis-configured mesh).
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * Integer/string math only: the project builds with -Wdouble-promotion as
 * an error.
 *
 * TRUTH CONTRACT: a duplicate SSID is a LEAD, never a verdict. Enterprise
 * mesh / roaming systems legitimately share one SSID across many BSSIDs,
 * and two cheap repeaters can share a default name. Everything this header
 * produces is phrased "possible cloned SSID", never "attacker".
 */

/* WifiAp lives in room_sweep.h (device build, pulls Furi headers). Forward
 * declaring the same struct tag keeps this header Flipper-free; host tests
 * re-declare an identical layout. Only ssid/bssid/valid are touched here. */
typedef struct RoomSweepWifiAp WifiAp;

/* The literal placeholder the Marauder parser stores for an unresolved
 * SSID. Hidden networks carry no name to correlate — always excluded. */
#define ROOM_SWEEP_ROGUE_HIDDEN_LABEL "Hidden/unknown"

/* Maximum distinct BSSIDs tracked per group: two representative row
 * pointers plus the full share count. */
#define ROOM_SWEEP_ROGUE_ROWS_MAX 2

typedef struct {
    const WifiAp* rows[ROOM_SWEEP_ROGUE_ROWS_MAX]; /* representative pair (first two found) */
    uint8_t count; /* BSSIDs sharing the exact SSID (>= 2 in every emitted group) */
} RoomSweepRogueGroup;

/* A row can only take part in a duplicate group when it is a real stored
 * AP with a BSSID and an actual (non-placeholder, non-empty) SSID. */
static inline bool room_sweep_rogue_row_eligible(const WifiAp* ap) {
    return ap != NULL && ap->valid && ap->bssid[0] != '\0' && ap->ssid[0] != '\0' &&
           strcmp(ap->ssid, ROOM_SWEEP_ROGUE_HIDDEN_LABEL) != 0;
}

/* Exact-SSID match: 802.11 SSIDs are octet strings, so comparison is
 * case-sensitive byte equality — "Home" and "HOME" are different networks.
 * MAC case is normalized elsewhere; SSIDs are never folded. */
static inline bool room_sweep_rogue_ssid_equal(const char* left, const char* right) {
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

/*
 * Number of eligible rows in table[0..n) (including row `index` itself)
 * sharing row `index`'s exact SSID; 0 when the row is not eligible or the
 * index is out of range. Used by the UI for the list "!" marker and the
 * detail "SAME NAME ON n BSSIDS" badge.
 */
static inline int room_sweep_rogue_dup_count(const WifiAp* table, int n, int index) {
    if(table == NULL || index < 0 || index >= n) return 0;
    if(!room_sweep_rogue_row_eligible(&table[index])) return 0;
    int count = 0;
    for(int i = 0; i < n; i++) {
        if(room_sweep_rogue_row_eligible(&table[i]) &&
           room_sweep_rogue_ssid_equal(table[i].ssid, table[index].ssid)) {
            count++;
        }
    }
    return count;
}

/*
 * Group the AP table by exact SSID. Every group emitted has count >= 2
 * (a lone AP is never "rogue"). rows[] holds the first two matching rows in
 * table order; count holds the full number of sharing BSSIDs (a triple
 * emits one group with count = 3).
 *
 * out may be NULL to count only; otherwise at most `max` groups are
 * written. Returns the number of groups FOUND (not capped by max), so the
 * report line can stay honest even when the display list is truncated.
 */
static inline int room_sweep_rogue_scan(const WifiAp* table, int n, RoomSweepRogueGroup* out, int max) {
    if(table == NULL || n <= 0) return 0;
    int found = 0;
    for(int i = 0; i < n; i++) {
        if(!room_sweep_rogue_row_eligible(&table[i])) continue;
        /* Only the first row of each SSID emits the group. */
        bool already_grouped = false;
        for(int j = 0; j < i && !already_grouped; j++) {
            if(room_sweep_rogue_row_eligible(&table[j]) &&
               room_sweep_rogue_ssid_equal(table[j].ssid, table[i].ssid)) {
                already_grouped = true;
            }
        }
        if(already_grouped) continue;
        int count = room_sweep_rogue_dup_count(table, n, i);
        if(count < 2) continue;
        if(out != NULL && found < max) {
            RoomSweepRogueGroup* group = &out[found];
            group->rows[0] = &table[i];
            group->rows[1] = NULL;
            int second = 0;
            for(int j = i + 1; j < n && second == 0; j++) {
                if(room_sweep_rogue_row_eligible(&table[j]) &&
                   room_sweep_rogue_ssid_equal(table[j].ssid, table[i].ssid)) {
                    group->rows[1] = &table[j];
                    second = 1;
                }
            }
            group->count = (uint8_t)(count > 255 ? 255 : count);
        }
        found++;
    }
    return found;
}

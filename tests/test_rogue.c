/* Host tests: duplicate-SSID / possible-rogue correlation (pure, no Furi).
 * See specs/full-capability-expansion-2026-09-20.md Phase 6 — truth
 * contract: exact case-SENSITIVE SSID match, hidden/no-BSSID rows excluded,
 * groups only when >= 2 BSSIDs share a name, and the result is a lead, not
 * a verdict.
 *
 * The fixture mirrors room_sweep.h's WifiAp layout field-for-field (the
 * rogue header only touches ssid/bssid/valid and forward-declares the tag
 * to stay Flipper-header-free; the struct definition must precede the
 * include so the inline bodies see a complete type). */
#include <stdio.h>
#include <string.h>

struct RoomSweepWifiAp {
    char ssid[33];
    signed char rssi;
    unsigned char channel;
    char bssid[18];
    unsigned int first_seen;
    unsigned int last_seen;
    unsigned short observations;
    int valid;
};

#include "../room_sweep_rogue.h"

static int fails;

static void check(const char* name, int cond) {
    if(cond) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        fails++;
    }
}

static void make_ap(
    struct RoomSweepWifiAp* ap,
    const char* ssid,
    const char* bssid,
    int valid) {
    memset(ap, 0, sizeof(*ap));
    if(ssid) strncpy(ap->ssid, ssid, 32);
    if(bssid) strncpy(ap->bssid, bssid, 17);
    ap->rssi = -50;
    ap->channel = 6;
    ap->observations = 1;
    ap->valid = valid;
}

int main(void) {
    /* ---------------------------------------------------------- */
    /* Basic pair                                                 */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp pair_room[3];
    make_ap(&pair_room[0], "HomeNet", "aa:bb:cc:00:00:01", 1);
    make_ap(&pair_room[1], "HomeNet", "aa:bb:cc:00:00:02", 1);
    make_ap(&pair_room[2], "OtherNet", "aa:bb:cc:00:00:03", 1);
    RoomSweepRogueGroup groups[4];
    int n = room_sweep_rogue_scan(pair_room, 3, groups, 4);
    check("pair: one group found", n == 1);
    check("pair: count is 2", groups[0].count == 2);
    check("pair: representative is first row", groups[0].rows[0] == &pair_room[0]);
    check("pair: second representative is second row", groups[0].rows[1] == &pair_room[1]);

    /* ---------------------------------------------------------- */
    /* Triple: one group, count 3                                 */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp triple_room[4];
    make_ap(&triple_room[0], "Mesh", "aa:bb:cc:00:00:01", 1);
    make_ap(&triple_room[1], "Solo", "aa:bb:cc:00:00:02", 1);
    make_ap(&triple_room[2], "Mesh", "aa:bb:cc:00:00:03", 1);
    make_ap(&triple_room[3], "Mesh", "aa:bb:cc:00:00:04", 1);
    n = room_sweep_rogue_scan(triple_room, 4, groups, 4);
    check("triple: exactly one group", n == 1);
    check("triple: count is 3, not 2", groups[0].count == 3);
    check("triple: representatives are the first two Mesh rows",
          groups[0].rows[0] == &triple_room[0] && groups[0].rows[1] == &triple_room[2]);

    /* ---------------------------------------------------------- */
    /* No-duplicate room: no groups                               */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp quiet_room[3];
    make_ap(&quiet_room[0], "Alpha", "aa:bb:cc:00:00:01", 1);
    make_ap(&quiet_room[1], "Beta", "aa:bb:cc:00:00:02", 1);
    make_ap(&quiet_room[2], "Gamma", "aa:bb:cc:00:00:03", 1);
    n = room_sweep_rogue_scan(quiet_room, 3, groups, 4);
    check("quiet room: zero groups", n == 0);

    /* ---------------------------------------------------------- */
    /* Hidden placeholder excluded                                */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp hidden_room[3];
    make_ap(&hidden_room[0], "Hidden/unknown", "aa:bb:cc:00:00:01", 1);
    make_ap(&hidden_room[1], "Hidden/unknown", "aa:bb:cc:00:00:02", 1);
    make_ap(&hidden_room[2], "Hidden/unknown", "aa:bb:cc:00:00:03", 1);
    n = room_sweep_rogue_scan(hidden_room, 3, groups, 4);
    check("hidden rows never group", n == 0);
    /* A hidden row is not eligible even as the probe row. */
    check("hidden row itself reports dup count 0",
          room_sweep_rogue_dup_count(hidden_room, 3, 0) == 0);
    /* Hidden rows do not inflate a real SSID's count. */
    make_ap(&hidden_room[0], "Hidden/unknown", "aa:bb:cc:00:00:01", 1);
    make_ap(&hidden_room[1], "HomeNet", "aa:bb:cc:00:00:02", 1);
    make_ap(&hidden_room[2], "OtherNet", "aa:bb:cc:00:00:03", 1);
    check("hidden row not counted into a real group",
          room_sweep_rogue_dup_count(hidden_room, 3, 1) == 1);

    /* ---------------------------------------------------------- */
    /* No-BSSID and invalid rows excluded                         */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp macless_room[4];
    make_ap(&macless_room[0], "PhoneHotspot", "", 1); /* no BSSID */
    make_ap(&macless_room[1], "PhoneHotspot", "aa:bb:cc:00:00:02", 1);
    make_ap(&macless_room[2], "PhoneHotspot", "aa:bb:cc:00:00:03", 0); /* invalid row */
    make_ap(&macless_room[3], "PhoneHotspot", "aa:bb:cc:00:00:04", 1);
    n = room_sweep_rogue_scan(macless_room, 4, groups, 4);
    check("macless/invalid rows excluded: one group of 2", n == 1 && groups[0].count == 2);
    check("macless row reports dup count 0", room_sweep_rogue_dup_count(macless_room, 4, 0) == 0);
    check("invalid row reports dup count 0", room_sweep_rogue_dup_count(macless_room, 4, 2) == 0);
    check("eligible row sees exactly 2 peers", room_sweep_rogue_dup_count(macless_room, 4, 1) == 2);

    /* Empty SSID rows excluded. */
    struct RoomSweepWifiAp empty_room[2];
    make_ap(&empty_room[0], "", "aa:bb:cc:00:00:01", 1);
    make_ap(&empty_room[1], "", "aa:bb:cc:00:00:02", 1);
    check("empty SSID never groups", room_sweep_rogue_scan(empty_room, 2, groups, 4) == 0);

    /* ---------------------------------------------------------- */
    /* Case-sensitive 802.11 semantics                            */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp case_room[2];
    make_ap(&case_room[0], "Home", "aa:bb:cc:00:00:01", 1);
    make_ap(&case_room[1], "HOME", "aa:bb:cc:00:00:02", 1);
    check("SSID match is case-sensitive", room_sweep_rogue_scan(case_room, 2, groups, 4) == 0);

    /* ---------------------------------------------------------- */
    /* Cap behavior + count-only mode + degenerate inputs         */
    /* ---------------------------------------------------------- */
    struct RoomSweepWifiAp busy_room[6];
    make_ap(&busy_room[0], "NetA", "aa:bb:cc:00:00:01", 1);
    make_ap(&busy_room[1], "NetA", "aa:bb:cc:00:00:02", 1);
    make_ap(&busy_room[2], "NetB", "aa:bb:cc:00:00:03", 1);
    make_ap(&busy_room[3], "NetB", "aa:bb:cc:00:00:04", 1);
    make_ap(&busy_room[4], "NetC", "aa:bb:cc:00:00:05", 1);
    make_ap(&busy_room[5], "NetC", "aa:bb:cc:00:00:06", 1);
    RoomSweepRogueGroup capped[1];
    n = room_sweep_rogue_scan(busy_room, 6, capped, 1);
    check("three groups found", n == 3);
    check("only max=1 group written", capped[0].count == 2 && capped[0].rows[0] == &busy_room[0]);
    check("count-only mode (out=NULL) still returns full count",
          room_sweep_rogue_scan(busy_room, 6, NULL, 0) == 3);
    check("NULL table returns 0", room_sweep_rogue_scan(NULL, 6, groups, 4) == 0);
    check("empty table returns 0", room_sweep_rogue_scan(busy_room, 0, groups, 4) == 0);
    check("negative n returns 0", room_sweep_rogue_scan(busy_room, -1, groups, 4) == 0);
    check("dup_count out-of-range index is 0", room_sweep_rogue_dup_count(busy_room, 6, 99) == 0);
    check("dup_count NULL table is 0", room_sweep_rogue_dup_count(NULL, 6, 0) == 0);

    /* ---------------------------------------------------------- */
    /* Row eligibility edge cases                                 */
    /* ---------------------------------------------------------- */
    check("NULL row not eligible", !room_sweep_rogue_row_eligible(NULL));
    struct RoomSweepWifiAp one;
    make_ap(&one, "X", "aa:bb:cc:00:00:01", 1);
    check("valid row eligible", room_sweep_rogue_row_eligible(&one));
    make_ap(&one, "X", "", 1);
    check("no-BSSID row not eligible", !room_sweep_rogue_row_eligible(&one));

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

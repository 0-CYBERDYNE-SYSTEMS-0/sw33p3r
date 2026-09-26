/* Host tests: sniffraw transmitter-radar parsing + bounded upsert
 * (Phase 4, pure, no Furi). Synthetic redacted fixtures preserve the
 * 2026-09-20 format check without retaining device identifiers:
 *   > RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:01
 *   RSSI: -92 Ch: 6 BSSID: 02:22:33:44:55:02
 * Truth contract: a raw row is "a transmitter heard", never identity. */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_sta.h"
#include "../room_sweep_oui.h"

static int fails;

static void check(const char* name, bool cond) {
    if(cond) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        fails++;
    }
}

static void check_str(const char* name, const char* got, const char* want) {
    bool ok = got && want && strcmp(got, want) == 0;
    if(ok) {
        printf("PASS: %s (%s)\n", name, got);
    } else {
        printf("FAIL: %s got=[%s] want=[%s]\n", name, got ? got : "(null)", want ? want : "(null)");
        fails++;
    }
}

int main(void) {
    RoomSweepRawRecord r;

    /* Redacted fixture: prompt-prefixed first line. */
    check(
        "fixture line parses",
        room_sweep_raw_parse_line("> RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:01", &r));
    check_str("fixture mac", r.mac, "02:22:33:44:55:01");
    check("fixture rssi", r.rssi == -44);
    check("fixture channel", r.channel == 6);

    /* Bare repeat line. */
    check(
        "bare line parses",
        room_sweep_raw_parse_line("RSSI: -92 Ch: 6 BSSID: 02:22:33:44:55:02", &r));
    check_str("bare line mac", r.mac, "02:22:33:44:55:02");
    check("bare line rssi", r.rssi == -92);

    /* Other sources' lines must be rejected. */
    check(
        "beacon line rejected",
        !room_sweep_raw_parse_line(
            "RSSI: -54 Ch: 2 BSSID: 02:33:44:55:66:01 ESSID: LAB_NET_ALPHA", &r));
    check(
        "ble line rejected",
        !room_sweep_raw_parse_line("RSSI: -37 Device: 02:44:55:66:77:01", &r));
    check(
        "probe line rejected",
        !room_sweep_raw_parse_line(
            "RSSI: -52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet", &r));
    check("echo rejected", !room_sweep_raw_parse_line("#sniffraw", &r));
    check("empty rejected", !room_sweep_raw_parse_line("", &r));
    check("null rejected", !room_sweep_raw_parse_line(NULL, &r));
    check("banner rejected", !room_sweep_raw_parse_line("Starting Raw sniff. Stop with stopscan", &r));
    check("no-mac rejected", !room_sweep_raw_parse_line("RSSI: -44 Ch: 6 BSSID: ", &r));
    check("positive rssi rejected", !room_sweep_raw_parse_line("RSSI: 44 Ch: 6 BSSID: 02:22:33:44:55:01", &r));

    /* ---------------------------------------------------------- */
    /* Upsert: MAC-first identity, stats fold, bounded table      */
    /* ---------------------------------------------------------- */
    {
        RawDev table[MAX_RAW_DEVS];
        memset(table, 0, sizeof(table));

        RoomSweepRawRecord a;
        room_sweep_raw_parse_line("RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:01", &a);
        int idx = room_sweep_raw_upsert(table, MAX_RAW_DEVS, &a, 100);
        check("insert returns row 0", idx == 0);
        check("first_seen set", table[0].first_seen == 100);
        check("last_seen set", table[0].last_seen == 100);
        check("observations = 1", table[0].observations == 1);
        check("min initialized", table[0].rssi_min == -44);
        check("max initialized", table[0].rssi_max == -44);

        /* Same MAC, weaker then stronger: min/max/sum fold, first_seen kept. */
        RoomSweepRawRecord b;
        room_sweep_raw_parse_line("RSSI: -60 Ch: 6 BSSID: 02:22:33:44:55:01", &b);
        idx = room_sweep_raw_upsert(table, MAX_RAW_DEVS, &b, 200);
        check("update returns same row", idx == 0);
        check("first_seen intact", table[0].first_seen == 100);
        check("last_seen advanced", table[0].last_seen == 200);
        check("observations = 2", table[0].observations == 2);
        check("min folded", table[0].rssi_min == -60);
        check("max folded", table[0].rssi_max == -44);
        RoomSweepRawRecord c;
        room_sweep_raw_parse_line("RSSI: -38 Ch: 6 BSSID: 02:22:33:44:55:01", &c);
        room_sweep_raw_upsert(table, MAX_RAW_DEVS, &c, 300);
        check("max folded up", table[0].rssi_max == -38);
        check("observations = 3", table[0].observations == 3);
        /* sum = offsets (76 + 60 + 82) = 218 -> avg rounds to -47 */
        check("avg math", room_sweep_stats_avg((int32_t)table[0].rssi_sum, 3) == -47);

        /* Different MAC never merges into the row. */
        RoomSweepRawRecord d;
        room_sweep_raw_parse_line("RSSI: -50 Ch: 11 BSSID: 02:22:33:44:55:02", &d);
        idx = room_sweep_raw_upsert(table, MAX_RAW_DEVS, &d, 400);
        check("second mac gets new row", idx == 1);
        check("first row untouched", table[0].observations == 3);

        /* Case-insensitive MAC identity. */
        RoomSweepRawRecord e;
        room_sweep_raw_parse_line("RSSI: -51 Ch: 11 BSSID: 02:22:33:44:55:02", &e);
        idx = room_sweep_raw_upsert(table, MAX_RAW_DEVS, &e, 500);
        check("case-insensitive merge", idx == 1);
        check("merged observations", table[1].observations == 2);

        /* Fill the table; overflow returns -1 and keeps rows intact. */
        for(int i = 2; i < MAX_RAW_DEVS; i++) {
            RoomSweepRawRecord f;
            char line[64];
            snprintf(line, sizeof(line), "RSSI: -40 Ch: 1 BSSID: 02:00:00:00:00:%02x", i);
            if(!room_sweep_raw_parse_line(line, &f)) {
                check("fill line parses", false);
                break;
            }
            if(room_sweep_raw_upsert(table, MAX_RAW_DEVS, &f, 600) < 0) {
                check("fill upsert failed early", false);
                break;
            }
        }
        RoomSweepRawRecord g;
        room_sweep_raw_parse_line("RSSI: -40 Ch: 1 BSSID: 02:00:00:00:ff:ff", &g);
        check("full table returns -1", room_sweep_raw_upsert(table, MAX_RAW_DEVS, &g, 700) == -1);
        uint8_t total = 0;
        for(int i = 0; i < MAX_RAW_DEVS; i++) {
            if(table[i].valid) total++;
        }
        check("table held exactly max", total == MAX_RAW_DEVS);
    }

    /* Randomized-station evidence: the _ble OUI evidence must flag a
     * locally administered address (wiring uses it for raw rows). */
    {
        const char* ev = room_sweep_oui_evidence_ble("02:22:33:44:55:01");
        check("randomized station flagged", ev != NULL && strstr(ev, "random") != NULL);
    }

    if(fails == 0) {
        printf("test_sta: ALL PASS\n");
        return 0;
    }
    printf("test_sta: %d FAILURES\n", fails);
    return 1;
}

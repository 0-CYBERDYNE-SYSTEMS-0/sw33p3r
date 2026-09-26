/* Host tests: hostile-tooling source parsing + alternating-window planner
 * (Phase 10, pure, no Furi). Formats pinned from upstream ESP32Marauder
 * source, NOT observed live on this build (docs/BFFB_MOMENTUM.md):
 *   pwn v1.9.1: "Pwnagotchi Name: <name>" / "Pwnd Totals: <n>"
 *   pwn master: "Name: <name>" / "Pwnd #: <n>"
 *   esp: historical espressifSnifferCallback shape only ("RSSI: -n Ch: n
 *   BSSID: mac"); the mode is unrouted in every current upstream tree. */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_tool.h"

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
    RoomSweepToolRecord t;

    /* --------------------------- esp --------------------------- */
    check(
        "esp historical shape parses",
        room_sweep_tool_parse_esp_line("> RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:03", &t));
    check_str("esp mac", t.mac, "02:22:33:44:55:03");
    check("esp rssi", t.rssi == -44);
    check("esp kind", t.kind == ROOM_SWEEP_TOOL_KIND_ESP);
    check(
        "esp bare rssi form",
        room_sweep_tool_parse_esp_line("-44 Ch: 6 BSSID: 02:22:33:44:55:03", &t));
    check("esp rejects beacon", !room_sweep_tool_parse_esp_line("RSSI: -54 Ch: 2 BSSID: 02:33:44:55:66:01 ESSID: x", &t));
    check("esp rejects pwn name line", !room_sweep_tool_parse_esp_line("Pwnagotchi Name: pwn", &t));
    check("esp rejects echo", !room_sweep_tool_parse_esp_line("#sniffesp", &t));
    check("esp rejects banner", !room_sweep_tool_parse_esp_line("Starting Espressif device sniff. Stop with stopscan", &t));

    /* --------------------------- pwn --------------------------- */
    check("pwn v1.9.1 name parses", room_sweep_tool_parse_pwn_line("Pwnagotchi Name: alpha", &t));
    check_str("pwn name", t.name, "alpha");
    check("pwn kind", t.kind == ROOM_SWEEP_TOOL_KIND_PWN);
    check("pwn no rssi", t.rssi == 0);
    check("pwn no mac", t.mac[0] == '\0');
    check("pwn master name parses", room_sweep_tool_parse_pwn_line("Name: beta", &t));
    check_str("pwn master name", t.name, "beta");
    /* Upstream noise lines are rejected. */
    check("pwn rejects noise frame", !room_sweep_tool_parse_pwn_line("Not a Pwnagotchi frame.", &t));
    check("pwn rejects json noise", !room_sweep_tool_parse_pwn_line("JSON payload not found.", &t));
    check("pwn rejects pwnd count", !room_sweep_tool_parse_pwn_line("Pwnd Totals: 42", &t));
    check("pwn rejects master count", !room_sweep_tool_parse_pwn_line("Pwnd #: 42", &t));
    check("pwn rejects raw line", !room_sweep_tool_parse_pwn_line("RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:03", &t));
    check("pwn rejects echo", !room_sweep_tool_parse_pwn_line("#sniffpwn", &t));
    check("pwn rejects banner", !room_sweep_tool_parse_pwn_line("Starting Pwnagotchi sniff. Stop with stopscan", &t));
    check("pwn rejects empty name", !room_sweep_tool_parse_pwn_line("Name: ", &t));

    /* --------------------------- upsert --------------------------- */
    {
        ToolDev table[MAX_TOOL_DEVS];
        memset(table, 0, sizeof(table));

        RoomSweepToolRecord e;
        room_sweep_tool_parse_esp_line("RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:03", &e);
        int idx = room_sweep_tool_upsert(table, MAX_TOOL_DEVS, &e, 100);
        check("esp insert row 0", idx == 0);
        check("esp first_seen", table[0].first_seen == 100);
        check("esp rssi stored", table[0].rssi == -44);

        room_sweep_tool_parse_esp_line("RSSI: -50 Ch: 6 BSSID: 02:22:33:44:55:03", &e);
        idx = room_sweep_tool_upsert(table, MAX_TOOL_DEVS, &e, 200);
        check("esp merges by mac", idx == 0);
        check("esp observations 2", table[0].observations == 2);
        check("esp latest rssi", table[0].rssi == -50);

        RoomSweepToolRecord p1;
        room_sweep_tool_parse_pwn_line("Pwnagotchi Name: alpha", &p1);
        idx = room_sweep_tool_upsert(table, MAX_TOOL_DEVS, &p1, 300);
        check("pwn insert row 1", idx == 1);
        check("pwn rssi stays unknown", table[1].rssi == 0);

        room_sweep_tool_parse_pwn_line("Name: alpha", &p1);
        room_sweep_tool_upsert(table, MAX_TOOL_DEVS, &p1, 400);
        check("pwn merges by name", table[1].observations == 2);

        RoomSweepToolRecord p2;
        room_sweep_tool_parse_pwn_line("Name: beta", &p2);
        idx = room_sweep_tool_upsert(table, MAX_TOOL_DEVS, &p2, 500);
        check("second pwn new row", idx == 2);

        /* esp row never merges with a pwn row even if labels matched. */
        check("esp row still 2 obs", table[0].observations == 2);
    }

    /* ----------------------- window planner ----------------------- */
    {
        /* Window 10000 ms: first window esp, next pwn, alternating. */
        check("planner starts esp", room_sweep_tool_next_window(0, 10000, 0) == ROOM_SWEEP_TOOL_KIND_ESP);
        check("planner keeps esp", room_sweep_tool_next_window(5000, 10000, ROOM_SWEEP_TOOL_KIND_ESP) == 0);
        check("planner flips to pwn", room_sweep_tool_next_window(10000, 10000, ROOM_SWEEP_TOOL_KIND_ESP) == ROOM_SWEEP_TOOL_KIND_PWN);
        check("planner keeps pwn", room_sweep_tool_next_window(15000, 10000, ROOM_SWEEP_TOOL_KIND_PWN) == 0);
        check("planner flips back", room_sweep_tool_next_window(20000, 10000, ROOM_SWEEP_TOOL_KIND_PWN) == ROOM_SWEEP_TOOL_KIND_ESP);
        check("planner wrap far", room_sweep_tool_next_window(100000, 10000, ROOM_SWEEP_TOOL_KIND_ESP) == 0);
        check("planner zero window keeps", room_sweep_tool_next_window(0, 0, ROOM_SWEEP_TOOL_KIND_ESP) == 0);
    }

    if(fails == 0) {
        printf("test_tool: ALL PASS\n");
        return 0;
    }
    printf("test_tool: %d FAILURES\n", fails);
    return 1;
}

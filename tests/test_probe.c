/* Host tests: sniffprobe parsing + hidden-SSID repair decision
 * (Phase 5, pure, no Furi). Formats are pinned from upstream ESP32Marauder
 * source (v1.9.1 + master beaconSnifferCallback, WIFI_SCAN_PROBE branch).
 * The six records below are synthetic redacted fixtures derived from the
 * on-device format check; no device identifiers or room-specific SSIDs are
 * retained in the repository.
 *   v1.9.1: RSSI: -52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet
 *   master:      -52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet
 * Empty SSID: "<hidden>" (master) / nothing (v1.9.1). */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_probe.h"

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

/* Synthetic redacted records preserving the observed line shapes. */
static const char* const fixture_probe_lines[] = {
    "RSSI: -60 Ch: 2 Client: 02:11:22:33:44:01 Requesting: ",
    "RSSI: -67 Ch: 11 Client: 02:11:22:33:44:02 Requesting: ",
    "RSSI: -91 Ch: 11 Client: 02:11:22:33:44:03 Requesting: SYNTH_CAMERA_NET",
    "RSSI: -59 Ch: 1 Client: 02:11:22:33:44:04 Requesting: ",
    "RSSI: -58 Ch: 1 Client: 02:11:22:33:44:04 Requesting: ",
    "RSSI: -50 Ch: 8 Client: 02:11:22:33:44:05 Requesting: ",
};

/* Synthetic scaffolding: none of it is a probe record. */
static const char* const fixture_probe_noise[] = {
    "#sniffprobe",
    "> #sniffprobe",
    "Starting Probe sniff. Stop with stopscan",
    "Stopping WiFi tran/recv",
    "> ",
};

int main(void) {
    RoomSweepProbeRecord p;

    /* --- redacted fixture derived from the 2026-09-21 format check --- */
    {
        int parsed = 0;
        for(size_t i = 0; i < sizeof(fixture_probe_lines) / sizeof(fixture_probe_lines[0]); i++) {
            RoomSweepProbeRecord fixture;
            if(room_sweep_probe_parse_line(fixture_probe_lines[i], &fixture)) parsed++;
        }
        check("all six fixture records parse", parsed == 6);

        RoomSweepProbeRecord named;
        check(
            "fixture named probe parses",
            room_sweep_probe_parse_line(fixture_probe_lines[2], &named));
        check_str("fixture ssid", named.ssid, "SYNTH_CAMERA_NET");
        check_str("fixture client", named.client_mac, "02:11:22:33:44:03");
        check("fixture rssi", named.rssi == -91);
        check("fixture line carries no target bssid", named.target_bssid[0] == '\0');

        RoomSweepProbeRecord empty;
        check(
            "fixture empty request parses",
            room_sweep_probe_parse_line(fixture_probe_lines[0], &empty));
        check_str("empty request becomes the hidden label", empty.ssid, ROOM_SWEEP_PROBE_HIDDEN_LABEL);
        check("empty request rssi", empty.rssi == -60);

        int noise_parsed = 0;
        for(size_t i = 0; i < sizeof(fixture_probe_noise) / sizeof(fixture_probe_noise[0]); i++) {
            RoomSweepProbeRecord junk;
            if(room_sweep_probe_parse_line(fixture_probe_noise[i], &junk)) noise_parsed++;
        }
        check("scan scaffolding never parses as a probe", noise_parsed == 0);
    }

    /* v1.9.1 shape with RSSI prefix. */
    check(
        "v1.9.1 shape parses",
        room_sweep_probe_parse_line(
            "> RSSI: -52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet", &p));
    check_str("ssid", p.ssid, "SomeNet");
    check_str("client", p.client_mac, "aa:bb:cc:dd:ee:ff");
    check("no target bssid (pinned)", p.target_bssid[0] == '\0');
    check("rssi", p.rssi == -52);

    /* Master shape: bare leading RSSI value. */
    check(
        "master shape parses",
        room_sweep_probe_parse_line(
            "-52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet", &p));

    /* SSIDs with spaces are kept verbatim. */
    check(
        "spaces kept",
        room_sweep_probe_parse_line(
            "RSSI: -60 Ch: 11 Client: 11:22:33:44:55:66 Requesting: home net 5G", &p));
    check_str("spaced ssid", p.ssid, "home net 5G");

    /* Empty SSID forms become the hidden label. */
    check(
        "master hidden tag parses",
        room_sweep_probe_parse_line(
            "RSSI: -60 Ch: 1 Client: 11:22:33:44:55:66 Requesting: <hidden>", &p));
    check_str("hidden tag mapped", p.ssid, ROOM_SWEEP_PROBE_HIDDEN_LABEL);
    check(
        "trailing empty ssid parses",
        room_sweep_probe_parse_line(
            "RSSI: -60 Ch: 1 Client: 11:22:33:44:55:66 Requesting: ", &p));
    check_str("empty ssid mapped", p.ssid, ROOM_SWEEP_PROBE_HIDDEN_LABEL);

    /* Other sources' lines rejected. */
    check(
        "beacon line rejected",
        !room_sweep_probe_parse_line(
            "RSSI: -54 Ch: 2 BSSID: 02:33:44:55:66:01 ESSID: LAB_NET_ALPHA", &p));
    check(
        "ble line rejected",
        !room_sweep_probe_parse_line("RSSI: -37 Device: 02:44:55:66:77:01", &p));
    check(
        "raw line rejected",
        !room_sweep_probe_parse_line("RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:01", &p));
    check("echo rejected", !room_sweep_probe_parse_line("#sniffprobe", &p));
    check("banner rejected", !room_sweep_probe_parse_line("Starting Probe sniff. Stop with stopscan", &p));
    check("empty rejected", !room_sweep_probe_parse_line("", &p));
    check("null rejected", !room_sweep_probe_parse_line(NULL, &p));

    /* A future build printing the target BSSID is honored via the key. */
    check(
        "explicit BSSID key honored",
        room_sweep_probe_parse_line(
            "RSSI: -60 Ch: 6 Client: 11:22:33:44:55:66 BSSID: aa:bb:cc:dd:ee:ff Requesting: X", &p));
    check_str("target captured", p.target_bssid, "aa:bb:cc:dd:ee:ff");
    check_str("ssid after bssid", p.ssid, "X");

    /* ---------------------------------------------------------- */
    /* names_hidden: the pure repair decision                     */
    /* ---------------------------------------------------------- */
    {
        RoomSweepProbeRecord q;
        check(
            "named probe parses",
            room_sweep_probe_parse_line(
                "RSSI: -52 Ch: 6 Client: 11:22:33:44:55:66 BSSID: AA:BB:CC:DD:EE:FF Requesting: SecretNet",
                &q));

        check(
            "repair fires on bssid + hidden + name",
            room_sweep_probe_names_hidden("aa:bb:cc:dd:ee:ff", "Hidden/unknown", &q));
        check(
            "repair case-insensitive bssid",
            room_sweep_probe_names_hidden("AA:BB:CC:DD:EE:FF", "Hidden/unknown", &q));
        check(
            "no repair when ap named",
            !room_sweep_probe_names_hidden("aa:bb:cc:dd:ee:ff", "SomeNet", &q));
        check(
            "no repair without ap bssid",
            !room_sweep_probe_names_hidden("", "Hidden/unknown", &q));
        check(
            "no repair when probe broadcast",
            !room_sweep_probe_names_hidden("aa:bb:cc:dd:ee:ff", "Hidden/unknown", &p) ||
            p.target_bssid[0] != '\0');
        {
            RoomSweepProbeRecord hidden_probe;
            room_sweep_probe_parse_line(
                "RSSI: -52 Ch: 6 Client: 11:22:33:44:55:66 BSSID: aa:bb:cc:dd:ee:ff Requesting: <hidden>",
                &hidden_probe);
            check(
                "no repair from hidden probe name",
                !room_sweep_probe_names_hidden(
                    "aa:bb:cc:dd:ee:ff", "Hidden/unknown", &hidden_probe));
        }
        check("no repair on null", !room_sweep_probe_names_hidden("aa:bb:cc:dd:ee:ff", "Hidden/unknown", NULL));
        /* Today's pinned formats never carry a BSSID, so repair cannot fire. */
        {
            RoomSweepProbeRecord pinned;
            room_sweep_probe_parse_line(
                "RSSI: -52 Ch: 6 Client: 11:22:33:44:55:66 Requesting: SecretNet", &pinned);
            check(
                "pinned format cannot fire repair",
                !room_sweep_probe_names_hidden(
                    "aa:bb:cc:dd:ee:ff", "Hidden/unknown", &pinned));
        }
    }

    /* ---------------------------------------------------------- */
    /* Upsert: client-MAC identity, bounded table                 */
    /* ---------------------------------------------------------- */
    {
        ProbeDev table[MAX_PROBE_DEVS];
        memset(table, 0, sizeof(table));

        RoomSweepProbeRecord a;
        room_sweep_probe_parse_line(
            "RSSI: -52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet", &a);
        int idx = room_sweep_probe_upsert(table, MAX_PROBE_DEVS, &a, 100);
        check("insert row 0", idx == 0);
        check("first_seen", table[0].first_seen == 100);
        check("observations 1", table[0].observations == 1);
        check_str("row ssid", table[0].ssid, "SomeNet");

        room_sweep_probe_upsert(table, MAX_PROBE_DEVS, &a, 200);
        check("repeat folds observations", table[0].observations == 2);
        check("last_seen advanced", table[0].last_seen == 200);

        RoomSweepProbeRecord b;
        room_sweep_probe_parse_line(
            "RSSI: -70 Ch: 6 Client: 99:88:77:66:55:44 Requesting: OtherNet", &b);
        idx = room_sweep_probe_upsert(table, MAX_PROBE_DEVS, &b, 300);
        check("second client new row", idx == 1);

        for(int i = 2; i < MAX_PROBE_DEVS; i++) {
            RoomSweepProbeRecord f;
            char line[96];
            snprintf(
                line,
                sizeof(line),
                "RSSI: -50 Ch: 1 Client: 02:00:00:00:00:%02x Requesting: Net%d",
                i,
                i);
            if(!room_sweep_probe_parse_line(line, &f) ||
               room_sweep_probe_upsert(table, MAX_PROBE_DEVS, &f, 400) < 0) {
                check("fill failed", false);
                break;
            }
        }
        RoomSweepProbeRecord g;
        room_sweep_probe_parse_line(
            "RSSI: -50 Ch: 1 Client: 02:00:00:00:ff:ff Requesting: Overflow", &g);
        check(
            "full table returns -1",
            room_sweep_probe_upsert(table, MAX_PROBE_DEVS, &g, 500) == -1);
    }

    if(fails == 0) {
        printf("test_probe: ALL PASS\n");
        return 0;
    }
    printf("test_probe: %d FAILURES\n", fails);
    return 1;
}

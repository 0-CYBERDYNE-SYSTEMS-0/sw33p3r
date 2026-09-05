/* Host tests: Marauder line parsing (pure, no Furi). See
 * specs/marauder-parser-2026-08-15.md — preserved invariants asserted here. */
#include <stdio.h>
#include <string.h>

#include "room_sweep_marauder.h"

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
    /* ---------------------------------------------------------- */
    /* WiFi                                                       */
    /* ---------------------------------------------------------- */
    {
        RoomSweepWifiRecord r;
        check("wifi: legacy format recognized",
              room_sweep_marauder_parse_wifi(
                  "-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00", &r));
        check("wifi: rssi from leading number", r.rssi == -45);
        check("wifi: channel parsed", r.channel == 6);
        check_str("wifi: capability bytes stripped from ssid", r.ssid, "NetworkName");
        check_str("wifi: bssid captured", r.bssid, "AA:BB:CC:DD:EE:FF");
        check("wifi: row flagged valid", r.valid);
    }
    {
        RoomSweepWifiRecord r;
        /* Captured BFFB active AP line: prompt prefix + RSSI key + BSSID key. */
        check("wifi: prompt RSSI-key form recognized",
              room_sweep_marauder_parse_wifi(
                  "> RSSI: -38 Ch: 5 BSSID: aa:bb:cc:dd:ee:ff ESSID: My Home Net", &r));
        check("wifi: RSSI-key rssi", r.rssi == -38);
        check("wifi: RSSI-key channel", r.channel == 5);
        check_str("wifi: multi-word ssid kept", r.ssid, "My Home Net");
        check_str("wifi: RSSI-key bssid lowercase kept", r.bssid, "aa:bb:cc:dd:ee:ff");
    }
    {
        RoomSweepWifiRecord r;
        /* Hidden network: no ESSID field, but Ch: marks it as WiFi. */
        check("wifi: no-essid line recognized",
              room_sweep_marauder_parse_wifi("-50 Ch: 6 AA:BB:CC:DD:EE:FF", &r));
        check_str("wifi: missing essid falls back to Hidden/unknown", r.ssid, "Hidden/unknown");
    }
    check("wifi: # echo line rejected",
          !room_sweep_marauder_parse_wifi("#sniffbeacon", NULL));
    check("wifi: empty line rejected",
          !room_sweep_marauder_parse_wifi("", NULL));
    check("wifi: prompt-only line rejected",
          !room_sweep_marauder_parse_wifi(">", NULL));
    check("wifi: BLE line rejected as wifi",
          !room_sweep_marauder_parse_wifi("-60 Device: AirPods", NULL));
    check("wifi: out-of-range high rssi rejected",
          !room_sweep_marauder_parse_wifi("+1 Ch: 6 ESSID: x", NULL));
    check("wifi: out-of-range low rssi rejected",
          !room_sweep_marauder_parse_wifi("-150 Ch: 6 ESSID: x", NULL));
    check("wifi: contradictory Device: + ESSID rejected",
          !room_sweep_marauder_parse_wifi("-60 Device: x ESSID: y", NULL));

    /* ---------------------------------------------------------- */
    /* BLE record                                                 */
    /* ---------------------------------------------------------- */
    {
        RoomSweepBleRecord r;
        /* Wiki form "-60 Device: name" with no MAC. */
        check("ble: wiki name-only form recognized",
              room_sweep_marauder_parse_ble_record("-60 Device: AirPods", &r));
        check("ble: wiki form rssi", r.rssi == -60);
        check_str("ble: wiki form name", r.name, "AirPods");
        check_str("ble: wiki form mac empty", r.mac, "");
    }
    {
        RoomSweepBleRecord r;
        /* Captured BFFB: RSSI key + device MAC as the identifier. */
        check("ble: MAC device recognized",
              room_sweep_marauder_parse_ble_record(
                  "RSSI: -37 Device: 02:5a:9c:11:22:33", &r));
        check("ble: MAC device rssi", r.rssi == -37);
        check_str("ble: MAC device name is the mac", r.name, "02:5a:9c:11:22:33");
        check_str("ble: MAC captured", r.mac, "02:5a:9c:11:22:33");
    }
    {
        RoomSweepBleRecord r;
        check("ble: prompt RSSI form recognized",
              room_sweep_marauder_parse_ble_record("> RSSI: -60 Device: Tabs", &r));
        check("ble: prompt RSSI form rssi", r.rssi == -60);
        check_str("ble: prompt RSSI form name", r.name, "Tabs");
    }
    {
        RoomSweepBleRecord r;
        /* Device with an empty name: rssi + Device: marker, name falls back. */
        check("ble: empty-name device recognized",
              room_sweep_marauder_parse_ble_record("-50 Device: ", &r));
        check_str("ble: empty-name falls back to Hidden/unknown", r.name, "Hidden/unknown");
    }
    {
        RoomSweepBleRecord r;
        /* A second RSSI token leaking into the name field is truncated. */
        check("ble: leaked RSSI token truncated",
              room_sweep_marauder_parse_ble_record("-60 Device: AirPods RSSI: -50", &r));
        check("ble: leaked-token rssi", r.rssi == -60);
        check_str("ble: leaked-token name truncated", r.name, "AirPods");
    }
    check("ble: WiFi line rejected as ble",
          !room_sweep_marauder_parse_ble_record("-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: x", NULL));
    check("ble: # echo line rejected",
          !room_sweep_marauder_parse_ble_record("#stopscan", NULL));
    check("ble: empty line rejected",
          !room_sweep_marauder_parse_ble_record("", NULL));
    check("ble: no-rssi no-marker line rejected",
          !room_sweep_marauder_parse_ble_record("Hello world", NULL));

    /* ---------------------------------------------------------- */
    /* BLE line framing (abutting records)                        */
    /* ---------------------------------------------------------- */
    {
        /* Captured BFFB headless: many records abutting on one line. */
        RoomSweepBleRecord out[8];
        uint8_t n = room_sweep_marauder_parse_ble(
            "RSSI: -37 Device: 02:5a:9c:11:22:33 RSSI: -50 Device: bb:cc:dd:ee:ff:01",
            out,
            8);
        check("ble-line: two abutting records framed", n == 2);
        check("ble-line: first rssi", out[0].rssi == -37);
        check("ble-line: second rssi", out[1].rssi == -50);
        check_str("ble-line: second name", out[1].name, "bb:cc:dd:ee:ff:01");
    }
    {
        /* A stopscan echo abuts the last MAC — must not merge into it. */
        RoomSweepBleRecord out[8];
        uint8_t n = room_sweep_marauder_parse_ble(
            "RSSI: -37 Device: 02:5a:9c:11:22:33#stopscan", out, 8);
        check("ble-line: record abutting # framed once", n == 1);
        check_str("ble-line: #-abutting record name", out[0].name, "02:5a:9c:11:22:33");
    }
    {
        RoomSweepBleRecord out[8];
        uint8_t n = room_sweep_marauder_parse_ble("RSSI: -60 Device: Tabs  ", out, 8);
        check("ble-line: trailing spaces trimmed", n == 1);
        check_str("ble-line: trimmed name", out[0].name, "Tabs");
    }
    check("ble-line: Nonexistent out array guard",
          room_sweep_marauder_parse_ble("RSSI: -60 Device: Tabs", NULL, 8) == 0);
    check("ble-line: # only yields zero records",
          room_sweep_marauder_parse_ble("#stopscan", NULL, 0) == 0);

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

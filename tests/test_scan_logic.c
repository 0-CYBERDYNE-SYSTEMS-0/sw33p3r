/* Host tests: Marauder timeout decision + headless BLE UART frame. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "room_sweep_scan.h"

static int fails;

static void expect_true(const char* name, bool cond) {
    if(cond) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        fails++;
    }
}

int main(void) {
    fails = 0;

    /* Timeout: not scanning */
    expect_true(
        "idle never errors",
        !marauder_scan_should_error(false, 100000, 0, 30000, 0));

    /* CRITICAL: last_data_tick==0 style — scan just started, zero results, now large */
    expect_true(
        "fresh scan does not instant-error",
        !marauder_scan_should_error(true, 100000, 99950, 30000, 0));

    /* Zero results for full 30s → error */
    expect_true(
        "zero results after timeout errors",
        marauder_scan_should_error(true, 100000, 70000, 30000, 0));

    /* Results exist → silence is OK even past 30s */
    expect_true(
        "results present: silence is not error",
        !marauder_scan_should_error(true, 100000, 1000, 30000, 3));

    /* Boundary: exactly timeout */
    expect_true(
        "exactly at timeout with zero results",
        marauder_scan_should_error(true, 30000, 0, 30000, 0));

    expect_true(
        "one ms under timeout with zero results",
        !marauder_scan_should_error(true, 29999, 0, 30000, 0));

    /* Redacted BFFB format fixture */
    expect_true(
        "ble accepts RSSI: Device: form",
        room_sweep_uart_is_ble_result_line("RSSI: -37 Device: 02:44:55:66:77:01"));
    expect_true(
        "ble accepts prompt-prefixed RSSI form",
        room_sweep_uart_is_ble_result_line(">  RSSI: -70 Device: 02:44:55:66:77:02"));
    expect_true(
        "ble accepts legacy -NN Device form",
        room_sweep_uart_is_ble_result_line("-60 Device: AirPods"));
    expect_true(
        "wifi beacon is not a ble result line",
        !room_sweep_uart_is_ble_result_line("RSSI: -38 Ch: 5 BSSID: aa:bb:cc:dd:ee:ff ESSID: Net"));

    expect_true(
        "strip prompt removes gt",
        strcmp(room_sweep_uart_strip_prompt(">  RSSI: -1 Device: x"), "RSSI: -1 Device: x") == 0);

    {
        /* Redacted abutting stream (no newlines between devices) */
        RoomSweepUartLineAccum acc;
        char out[ROOM_SWEEP_UART_LINE_MAX];
        const char* stream =
            ">  RSSI: -37 Device: 02:44:55:66:77:01 RSSI: -50 Device: 02:44:55:66:77:02 "
            "RSSI: -78 Device: 02:44:55:66:77:03#stopscan";
        room_sweep_uart_line_reset(&acc);
        int emitted = 0;
        for(const char* p = stream; *p; p++) {
            if(room_sweep_uart_feed_byte(&acc, *p, out, sizeof(out))) {
                emitted++;
                if(emitted == 1) {
                    expect_true(
                        "first live ble record",
                        strstr(out, "02:44:55:66:77:01") != NULL &&
                            strstr(out, "-37") != NULL);
                }
            }
        }
        expect_true("emitted at least two records mid-stream", emitted >= 2);
        /* remainder may still hold last record until idle or # */
        if(room_sweep_uart_flush_ble_idle(&acc, out, sizeof(out))) {
            expect_true(
                "idle/final has third mac or stopscan remnant",
                strstr(out, "02:44:55:66:77:03") != NULL ||
                    strstr(out, "02:44:55:66:77:02") != NULL || out[0] != '\0');
            emitted++;
        }
        expect_true("at least 3 ble frames total", emitted >= 3);
    }

    {
        RoomSweepUartLineAccum acc;
        char out[ROOM_SWEEP_UART_LINE_MAX];
        const char* wifi = "> RSSI: -38 Ch: 5 BSSID: 02:33:44:55:66:03 ESSID: FakeNet\n";
        room_sweep_uart_line_reset(&acc);
        int emitted = 0;
        for(const char* p = wifi; *p; p++) {
            if(room_sweep_uart_feed_byte(&acc, *p, out, sizeof(out))) {
                emitted++;
                expect_true("wifi line kept with newline", strstr(out, "FakeNet") != NULL);
            }
        }
        expect_true("one wifi line", emitted == 1);
    }

    {
        /* Official Marauder RID CSV rows can exceed the old 128-byte frame. */
        RoomSweepUartLineAccum acc;
        char out[ROOM_SWEEP_UART_LINE_MAX];
        char expected[ROOM_SWEEP_UART_LINE_MAX];
        memcpy(expected, "RID,", 4);
        memset(expected + 4, 'x', sizeof(expected) - 5U);
        expected[sizeof(expected) - 1U] = '\0';
        room_sweep_uart_line_reset(&acc);
        bool emitted = false;
        for(const char* p = expected; *p; p++) {
            if(room_sweep_uart_feed_byte(&acc, *p, out, sizeof(out))) emitted = true;
        }
        expect_true(
            "long RID frame waits for delimiter",
            !emitted && room_sweep_uart_feed_byte(&acc, '\n', out, sizeof(out)));
        expect_true(
            "long RID frame survives intact",
            strcmp(out, expected) == 0 && strlen(out) > 128U);
    }

    {
        const char* rec = "RSSI: -37 Device: 02:44:55:66:77:01 RSSI: -50 Device: 02:44:55:66:77:02";
        const char* a = room_sweep_uart_find_ble_record(rec);
        const char* b = room_sweep_uart_ble_record_end(a);
        expect_true("find first record", a == rec);
        expect_true("end at second RSSI", b && b[0] == 'R' && strstr(b, "-50") != NULL);
    }

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

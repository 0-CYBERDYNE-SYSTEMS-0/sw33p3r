/* test_nmea.c — host unit test for the NMEA parser. Compile with cc. */
#include "nmea.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if(cond) {                                              \
            printf("  PASS: %s\n", msg);                        \
        } else {                                                \
            printf("  FAIL: %s\n", msg);                        \
            failures++;                                         \
        }                                                       \
    } while(0)

static void feed_str(GpsFix* f, const char* s) {
    for(const char* p = s; *p; p++) nmea_feed(f, *p);
}

static void feed_body(GpsFix* f, const char* body) {
    char sentence[160];
    snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body, nmea_checksum(body));
    feed_str(f, sentence);
}

static int approx(double a, double b, double eps) {
    return fabs(a - b) < eps;
}

int main(void) {
    /* --- Test 1: canonical GGA with fix --- */
    printf("Test 1: GGA with fix\n");
    GpsFix f;
    nmea_init(&f);
    feed_str(&f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    CHECK(f.sentences == 1, "one valid sentence accepted");
    CHECK(f.has_time && f.hour == 12 && f.minute == 35 && f.second == 19,
          "time parsed (12:35:19)");
    CHECK(f.fix_quality == 1, "fix quality = 1 (GPS)");
    CHECK(f.sats == 8, "satellites = 8");
    CHECK(f.has_pos, "position present");
    CHECK(approx(f.latitude, 48.1173, 0.001), "latitude ~48.1173 N");
    CHECK(approx(f.longitude, 11.5166667, 0.001), "longitude ~11.5167 E");

    /* --- Test 2: RMC active, speed/course + southern/western hemispheres --- */
    printf("Test 2: RMC active, S/W hemispheres\n");
    nmea_init(&f);
    feed_str(&f, "$GPRMC,220516,A,5133.82,S,00042.24,W,173.8,231.8,130694,004.2,W*6D\r\n");
    CHECK(f.sentences == 1, "RMC accepted");
    CHECK(f.has_fix, "fix active (status A)");
    CHECK(f.has_pos, "RMC position present");
    CHECK(approx(f.latitude, -(51.0 + 33.82 / 60.0), 0.001), "latitude negative (S)");
    CHECK(approx(f.longitude, -(0.0 + 42.24 / 60.0), 0.001), "longitude negative (W)");
    CHECK(approx(f.speed_kts, 173.8, 0.1), "speed ~173.8 kts");
    CHECK(approx(f.course, 231.8, 0.1), "course ~231.8");

    /* --- Test 3: bad checksum rejected --- */
    printf("Test 3: bad checksum rejected\n");
    nmea_init(&f);
    feed_str(&f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00\r\n");
    CHECK(f.sentences == 0, "bad checksum sentence dropped");
    CHECK(!f.has_pos, "no position from bad sentence");

    /* --- Test 4: void/no-fix GGA does not set position --- */
    printf("Test 4: no-fix GGA\n");
    nmea_init(&f);
    feed_str(&f, "$GPGGA,,,,,,0,00,,,,,,,*66\r\n");
    CHECK(f.sentences == 1, "no-fix GGA accepted syntactically");
    CHECK(!f.has_pos, "no position when quality=0");
    CHECK(!f.has_fix, "no fix flag");

    /* --- Test 5: garbage / partial does not crash, no false accept --- */
    printf("Test 5: garbage resilience\n");
    nmea_init(&f);
    feed_str(&f, "garbage no dollar\r\n$$$$$****\r\n$GP");
    feed_str(&f, "RMC,partial"); /* truncated, no CR */
    CHECK(f.sentences == 0, "garbage produced no valid sentences");

    /* --- Test 6: two sentences back to back --- */
    printf("Test 6: consecutive sentences\n");
    nmea_init(&f);
    feed_str(&f,
             "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
             "$GPGGA,123520,4807.040,N,01131.002,E,1,09,0.8,545.5,M,46.9,M,,*41\r\n");
    CHECK(f.sentences == 2, "two valid sentences accepted");
    CHECK(f.second == 20, "time advanced to :20");

    /* --- Test 7: checksum helper --- */
    printf("Test 7: checksum helper\n");
    CHECK(nmea_checksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,") == 0x47,
          "checksum helper matches 0x47");

    /* --- Test 8: GLL with no fix (real BFFB sentence) --- */
    printf("Test 8: GLL no-fix (BFFB GN talker)\n");
    nmea_init(&f);
    feed_str(&f, "$GNGLL,,,,,104832.000,V,N*68\r\n");
    CHECK(f.sentences == 1, "GLL accepted");
    CHECK(f.has_time && f.hour == 10 && f.minute == 48 && f.second == 32,
          "GLL time parsed (10:48:32)");
    CHECK(!f.has_fix, "no fix from GLL status V");
    CHECK(!f.has_pos, "no position from GLL with empty fields");

    /* --- Test 9: ZDA date+time (real BFFB sentence) --- */
    printf("Test 9: ZDA date/time (BFFB GN talker)\n");
    nmea_init(&f);
    feed_str(&f, "$GNZDA,104840.000,01,08,2026,00,00*4E\r\n");
    CHECK(f.sentences == 1, "ZDA accepted");
    CHECK(f.has_time && f.hour == 10 && f.minute == 48 && f.second == 40,
          "ZDA time parsed (10:48:40)");
    CHECK(f.has_date && f.day == 1 && f.month == 8 && f.year == 2026,
          "ZDA date parsed (2026-08-01)");

    /* --- Test 10: GSV sats-in-view (real BFFB BeiDou sentence) --- */
    printf("Test 10: GSV sats-in-view (BFFB BD talker)\n");
    nmea_init(&f);
    feed_str(&f, "$BDGSV,1,1,01,32,,,21*6B\r\n");
    CHECK(f.sentences == 1, "GSV accepted");
    CHECK(f.sats_in_view == 1, "1 satellite in view (BD PRN 32)");

    /* --- Test 11: GGA with GN talker (multi-GNSS) --- */
    printf("Test 11: GGA with GN talker\n");
    nmea_init(&f);
    feed_str(&f, "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59\r\n");
    CHECK(f.sentences == 1, "GN GGA accepted");
    CHECK(f.fix_quality == 1, "GN GGA fix quality = 1");
    CHECK(f.sats == 8, "GN GGA sats = 8");

    /* --- Test 12: RMC with GN talker + date (real BFFB sentence) --- */
    printf("Test 12: RMC no-fix with date (BFFB GN talker)\n");
    nmea_init(&f);
    feed_str(&f, "$GNRMC,104832,V,0000.00000,N,00000.00000,E,0.0,0.0,010826,,,N*77\r\n");
    CHECK(f.sentences == 1, "GN RMC accepted");
    CHECK(f.has_time && f.hour == 10 && f.minute == 48 && f.second == 32,
          "RMC time parsed");
    CHECK(!f.has_fix, "RMC status V = no fix");
    CHECK(f.has_date && f.day == 1 && f.month == 8 && f.year == 2026,
          "RMC date parsed (2026-08-01)");

    /* --- Test 13: realistic BFFB no-fix stream (GLL + VTG + GSV + ZDA) --- */
    printf("Test 13: realistic BFFB no-fix stream\n");
    nmea_init(&f);
    feed_str(&f, "$GNGLL,,,,,104833.000,V,N*69\r\n");
    feed_str(&f, "$GNVTG,,,,,,,,,N*2E\r\n");
    feed_str(&f, "$BDGSV,1,1,01,32,,,21*6B\r\n");
    feed_str(&f, "$GNZDA,104840.000,01,08,2026,00,00*4E\r\n");
    CHECK(f.sentences == 4, "all 4 stream sentences accepted");
    CHECK(f.has_time, "time available from GLL/ZDA");
    CHECK(f.sats_in_view == 1, "sats-in-view tracked from GSV");
    CHECK(!f.has_fix, "no fix (indoor)");
    CHECK(f.has_date && f.year == 2026, "date available from ZDA");

    printf("Test 14: truncated RMC boundary\n");
    nmea_init(&f);
    feed_body(&f, "GPRMC,123519,V,,,,,,," );
    CHECK(f.sentences == 1 && f.nav_sentences == 1, "RMC with empty date accepted");
    CHECK(f.has_time && !f.has_date, "RMC keeps valid time without date");

    printf("Test 15: non-hex checksum rejected\n");
    nmea_init(&f);
    feed_str(&f, "$GPGGA,123519,,,,,,*0Z\r\n");
    CHECK(f.sentences == 0, "non-hex checksum sentence dropped");

    printf("Test 16: semantic NMEA bounds rejected\n");
    nmea_init(&f);
    feed_body(&f, "GPGGA,256161,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    CHECK(f.sentences == 1 && f.nav_sentences == 0, "invalid GGA time does not refresh navigation");
    nmea_init(&f);
    feed_body(&f, "GPRMC,123519,V,,,,,,321399");
    CHECK(f.nav_sentences == 0 && !f.has_date, "invalid RMC date is rejected");
    nmea_init(&f);
    feed_body(&f, "GPRMC,256161,V,,,,,,,010826");
    CHECK(f.sentences == 1 && f.nav_sentences == 0 && !f.has_time,
          "invalid RMC time does not refresh navigation");
    nmea_init(&f);
    feed_body(&f, "GNGLL,,,,,256161,V,N");
    CHECK(f.sentences == 1 && f.nav_sentences == 0 && !f.has_time,
          "invalid GLL time does not refresh navigation");
    nmea_init(&f);
    feed_body(&f, "GPGGA,123519,4807.038,N,01131.000,E,1,xx,0.9,545.4,M,46.9,M,,");
    CHECK(f.nav_sentences == 0 && !f.has_pos, "non-numeric satellite count is rejected");
    nmea_init(&f);
    feed_body(&f, "GNZDA,104840.000,31,02,2025,00,00");
    CHECK(!f.has_date, "invalid ZDA calendar date is rejected");
    nmea_init(&f);
    feed_body(&f, "BDGSV,1,1,256");
    CHECK(f.sats_in_view == 0, "out-of-range GSV count is rejected");

    printf("Test 17: navigation freshness excludes telemetry\n");
    nmea_init(&f);
    feed_str(&f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    feed_str(&f, "$GNVTG,,,,,,,,,N*2E\r\n");
    feed_str(&f, "$BDGSV,1,1,01,32,,,21*6B\r\n");
    feed_str(&f, "$GNZDA,104840.000,01,08,2026,00,00*4E\r\n");
    CHECK(f.sentences == 4, "navigation and telemetry sentences accepted");
    CHECK(f.nav_sentences == 1, "telemetry does not refresh navigation data");

    printf("Test 18: active RMC without coordinates clears position\n");
    nmea_init(&f);
    feed_str(&f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    feed_str(&f, "$GPRMC,123519,A,,,,,,,010826,,,*0A\r\n");
    CHECK(f.sentences == 2, "active RMC accepted without coordinates");
    CHECK(f.has_fix && !f.has_pos, "active RMC cannot retain stale position");

    printf("Test 19: GGA fix claim without coordinates is not a position\n");
    nmea_init(&f);
    feed_body(&f, "GPGGA,123519,,,,,1,08,0.9,,,,,,");
    CHECK(f.nav_sentences == 1, "fix-claim GGA refreshes navigation");
    CHECK(f.fix_quality == 1 && f.sats == 8, "fix quality and sats tracked");
    CHECK(f.has_fix, "receiver fix claim recorded (quality=1)");
    CHECK(!f.has_pos, "fix claim without coordinates is NOT a usable position");

    printf("Test 20: malformed coordinates under a fix claim clear position\n");
    nmea_init(&f);
    feed_str(&f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    CHECK(f.has_fix && f.has_pos, "valid GGA first gives fix and position");
    feed_body(&f, "GPGGA,123520,99999.0,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    CHECK(f.has_fix, "receiver still claims a fix in the malformed sentence");
    CHECK(!f.has_pos, "unparseable coordinates clear the usable position");

    printf("Test 21: valid RMC+GGA together keep fix and position\n");
    nmea_init(&f);
    feed_body(&f, "GPRMC,123519,A,4807.038,N,01131.000,E,12.5,90.0,010826,,,");
    CHECK(f.has_fix && f.has_pos, "valid RMC gives fix and position");
    feed_body(&f, "GPGGA,123520,4807.040,N,01131.002,E,2,09,0.8,545.5,M,46.9,M,,");
    CHECK(f.has_fix && f.has_pos, "valid GGA keeps fix and position");
    CHECK(f.fix_quality == 2, "DGPS quality tracked from GGA");

    printf("\n%s (%d failure%s)\n", failures ? "RESULT: FAIL" : "RESULT: ALL PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

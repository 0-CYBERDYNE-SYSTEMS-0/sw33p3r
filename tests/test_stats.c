/* Host tests: per-device RSSI evidence stats (pure, no Furi).
 * See specs/full-capability-expansion-2026-09-20.md Phase 7 — integer math
 * only (-Wdouble-promotion is fatal), sum stores SIGMA(rssi + 120) so
 * uint32 cannot overflow, avg rounds to nearest, and every composed detail
 * token stays whole inside the 48-byte CSV bound (truncation may never
 * fabricate a value). */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_stats.h"
#include "../room_sweep_ui_layout.h"

static int fails;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        fails++;
    }
}

static void check_str(const char* name, const char* got, const char* want) {
    int ok = got && want && strcmp(got, want) == 0;
    if(ok) {
        printf("PASS: %s (%s)\n", name, got);
    } else {
        printf("FAIL: %s got=[%s] want=[%s]\n", name, got ? got : "(null)", want ? want : "(null)");
        fails++;
    }
}

int main(void) {
    /* ---------------------------------------------------------- */
    /* absorb: init + monotonic min/max                           */
    /* ---------------------------------------------------------- */
    int8_t min = 0, max = 0;
    uint32_t sum = 0;
    room_sweep_stats_absorb(&min, &max, &sum, -60, 0);
    check(min == -60, "absorb: first sample inits min");
    check(max == -60, "absorb: first sample inits max");
    check(sum == (uint32_t)(-60 + 120), "absorb: first sample sums rssi+120");
    room_sweep_stats_absorb(&min, &max, &sum, -40, 1);
    check(min == -60 && max == -40, "absorb: stronger sample raises max only");
    room_sweep_stats_absorb(&min, &max, &sum, -90, 2);
    check(min == -90 && max == -40, "absorb: weaker sample lowers min only");
    room_sweep_stats_absorb(&min, &max, &sum, -55, 3);
    check(min == -90 && max == -40, "absorb: mid sample touches neither bound");
    check(sum == (uint32_t)(60 + 80 + 30 + 65), "absorb: sum is exact offsets");

    /* INT8 extremes keep true min/max. */
    int8_t emin = 0, emax = -128;
    uint32_t esum = 0;
    room_sweep_stats_absorb(&emin, &emax, &esum, 127, 0);
    room_sweep_stats_absorb(&emin, &emax, &esum, -128, 1);
    check(emin == -128 && emax == 127, "absorb: int8 extremes recorded verbatim");
    /* -128 clamps to the -120 floor inside the sum; -128+120=-8 -> 0. */
    check(esum == (uint32_t)(127 + 120), "absorb: sum clamps below -120");

    /* NULL pointers are refused, not crashed. */
    room_sweep_stats_absorb(NULL, NULL, NULL, -50, 0);
    check(1, "absorb: NULL pointers are safe");

    /* ---------------------------------------------------------- */
    /* avg: rounded, integer-only                                 */
    /* ---------------------------------------------------------- */
    check(room_sweep_stats_avg((int32_t)(-60 + 120), 1) == -60, "avg: single sample is exact");
    /* -60 and -61 alternate: exact mean -60.5 -> rounds up to -60. */
    check(room_sweep_stats_avg((int32_t)(60 + 59), 2) == -60, "avg: half rounds up");
    /* -60 and -62: exact mean -61. */
    check(room_sweep_stats_avg((int32_t)(60 + 58), 2) == -61, "avg: exact mean needs no rounding");
    /* -90,-40,-55: sum offsets 30+80+65=175, /3 = 58.33 -> 58 -> -62. */
    check(room_sweep_stats_avg(175, 3) == -62, "avg: three samples");
    check(room_sweep_stats_avg(0, 0) == 0, "avg: zero observations yields 0");
    check(room_sweep_stats_avg(-5, 3) == 0, "avg: negative sum refused");

    /* ---------------------------------------------------------- */
    /* No overflow at the uint16 observation ceiling              */
    /* ---------------------------------------------------------- */
    int8_t wmin = 0, wmax = 0;
    uint32_t wsum = 0;
    room_sweep_stats_absorb(&wmin, &wmax, &wsum, -127, 0);
    for(uint32_t i = 1; i < 65535U; i++) {
        room_sweep_stats_absorb(&wmin, &wmax, &wsum, -1, (uint16_t)i);
    }
    check(wmin == -127, "worst case: min stays -127");
    check(wmax == -1, "worst case: max reaches -1");
    check(wsum <= 4000000000U, "worst case: uint32 sum never wraps");
    /* 65535 samples: offsets 0 + 65534*119 = 7798546. */
    check(wsum == 65534U * 119U, "worst case: sum exact");
    check(room_sweep_stats_avg((int32_t)wsum, 65535) == -1, "worst case: avg sane");

    /* ---------------------------------------------------------- */
    /* Detail-page stats line: pinned format + pixel budget       */
    /* ---------------------------------------------------------- */
    char line[48];
    room_sweep_stats_line(line, sizeof(line), -38, -72, -38, 14);
    check_str("line: range format", line, "-38dBm -72..-38 n14");
    room_sweep_stats_line(line, sizeof(line), -38, -38, -38, 1);
    check_str("line: single sighting collapses the range", line, "-38dBm n1");
    room_sweep_stats_line(line, sizeof(line), -100, -120, -100, 9999);
    check_str("line: count shown verbatim below the cap", line, "-100dBm -120..-100 n9999");
    room_sweep_stats_line(line, sizeof(line), -100, -120, -100, 10000);
    check_str("line: count caps honestly above 9999", line, "-100dBm -120..-100 n>9k");
    check(
        strlen(line) <= ROOM_SWEEP_STATS_LINE_MAX,
        "line: worst case respects the pinned 23-char bound");
    room_sweep_stats_line(line, sizeof(line), -128, -128, 127, 65535);
    check(
        strlen(line) <= ROOM_SWEEP_STATS_LINE_MAX,
        "line: absolute int8/uint16 worst case still bounded");
    /* Arithmetic pixel budget: UI_MARGIN_X + chars * FontKeyboard <= edge. */
    check(
        UI_MARGIN_X + (unsigned)ROOM_SWEEP_STATS_LINE_MAX * UI_FONT_KEYBOARD_PX <=
            UI_TEXT_RIGHT_EDGE,
        "line: 23 keyboard chars from x=2 fit the 128px panel");
    check(strlen("-38dBm n1") <= ROOM_SWEEP_STATS_LINE_MAX, "line: collapsed form is shorter");

    /* ---------------------------------------------------------- */
    /* Detail composer: token order, fit, all-or-nothing          */
    /* Fits are counted against the 48-byte CSV bound (47 + NUL). */
    /* ---------------------------------------------------------- */
    char detail[48];
    /* 11 + 25 + 11 = 47 chars: everything fits. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "TP-Link",
        "",
        0,
        false,
        -45,
        -45,
        (uint32_t)(75 * 3), /* 3 samples at -45 dBm */
        3,
        "AP beacon");
    check_str(
        "detail: plain row keeps the beacon phrase",
        detail,
        "oui=TP-Link; min=-45 max=-45 avg=-45; AP beacon");

    /* 13 + 25 = 38 chars of stats; the tail would reach 49 -> dropped. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "Espressif",
        "",
        0,
        false,
        -72,
        -38,
        (uint32_t)(48 + 82 + 80), /* -72, -38, -40 */
        3,
        "AP beacon");
    check_str(
        "detail: tight buffer drops the tail, never a value",
        detail,
        "oui=Espressif; min=-72 max=-38 avg=-50");
    check(strlen(detail) < sizeof(detail), "detail: fits the 48-byte CSV bound");

    /* Hints + stats no longer fit together: the stats token is skipped
     * WHOLE and the tail fills the rest — no mid-value cuts. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "Espressif",
        "DEV?",
        0,
        false,
        -72,
        -38,
        (uint32_t)(48 + 82 + 80),
        3,
        "AP beacon");
    check_str(
        "detail: hint combo yields stats whole, keeps tail",
        detail,
        "oui=Espressif; hints=DEV?; AP beacon");

    /* 12 + 9 + 25 = 46: rogue and stats coexist; tail drops. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "unlisted",
        "",
        3,
        false,
        -60,
        -58,
        (uint32_t)(60 + 62 + 62), /* -60, -58, -58 */
        3,
        "AP beacon");
    check_str(
        "detail: rogue token included when duped",
        detail,
        "oui=unlisted; rogue=3; min=-60 max=-58 avg=-59");

    /* 12 + 25 + 9 = 46: BLE variant with its own evidence function. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        NULL,
        "",
        0,
        false,
        -50,
        -50,
        (uint32_t)(-50 + 120),
        1,
        "BLE adv");
    /* evidence NULL + has_mac means no oui token at all (caller passes NULL
     * only for MAC-less rows; here MAC is present so emulate unlisted). */
    check_str("detail: stats carry without oui token", detail, "min=-50 max=-50 avg=-50; BLE adv");

    /* No MAC: tail leads, no oui/rogue tokens, stats still carry. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        false,
        NULL,
        "",
        0,
        false,
        -70,
        -65,
        (uint32_t)(50 + 55 + 55), /* -70, -65, -65 */
        3,
        "AP beacon");
    check_str(
        "detail: no-MAC row leads with the tail",
        detail,
        "AP beacon; no MAC; min=-70 max=-65 avg=-67");

    /* Degenerate inputs: empty output, tiny buffer stays terminated. */
    detail[0] = 'X';
    room_sweep_stats_detail(detail, 0, true, "x", "", 0,
        false, 0, 0, 0, 0, "");
    check(detail[0] == 'X', "detail: zero capacity writes nothing");
    char tiny[4];
    tiny[0] = 'Y';
    room_sweep_stats_detail(tiny, sizeof(tiny), true, "Apple", "", 0,
        false, -1, -1, 119, 1, "");
    check(tiny[0] == '\0', "detail: buffer too small for a whole token stays empty");
    char small[12];
    room_sweep_stats_detail(small, sizeof(small), true, "Apple", "", 0,
        false, -1, -1, 119, 1, "");
    check_str("detail: whole tokens only fill a small buffer", small, "oui=Apple");

    /* The hint+rogue+stats worst case never cuts mid-token. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "Espressif",
        "IOT?",
        12,
        false,
        -120,
        -38,
        (uint32_t)(0 + 82),
        2,
        "AP beacon");
    check_str(
        "detail: worst combo keeps early tokens and tail whole",
        detail,
        "oui=Espressif; hints=IOT?; rogue=12; AP beacon");
    check(strstr(detail, "min=") == NULL, "detail: stats skipped whole when they cannot fit");

    /* ---------------------------------------------------------- */
    /* Phase 9 watchlist token (user-flagged cross-session hit)   */
    /* ---------------------------------------------------------- */
    /* Watched row: watch=1 rides after rogue, before stats. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "TP-Link",
        "",
        0,
        true,
        -45,
        -45,
        (uint32_t)(75 * 3),
        3,
        "AP beacon");
    /* 11 + 9 + 25 = 45 chars fit; the tail would push past the bound and
     * yields whole — same yield rule as before, one token later. */
    check_str(
        "detail: watch token rides after rogue, before stats",
        detail,
        "oui=TP-Link; watch=1; min=-45 max=-45 avg=-45");

    /* Unwatched row keeps the legacy detail verbatim. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "TP-Link",
        "",
        0,
        false,
        -45,
        -45,
        (uint32_t)(75 * 3),
        3,
        "AP beacon");
    check(strstr(detail, "watch=") == NULL, "detail: unwatched row carries no watch token");

    /* Watch + hints + rogue together: stats yield whole, watch stays. */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        true,
        "Espressif",
        "IOT?",
        12,
        true,
        -120,
        -38,
        (uint32_t)(0 + 82),
        2,
        "AP beacon");
    check_str(
        "detail: watch coexists with hints and rogue; stats yield",
        detail,
        "oui=Espressif; hints=IOT?; rogue=12; watch=1");
    check(strlen(detail) < sizeof(detail), "detail: watch combo fits the 48-byte CSV bound");
    check(strstr(detail, "watch=1") != NULL && strstr(detail, "min=") == NULL &&
              strstr(detail, "AP beacon") == NULL,
          "detail: in tight buffers stats and tail yield, watch stays whole");

    /* MAC-less rows can never be watched (identity needs the address). */
    room_sweep_stats_detail(
        detail,
        sizeof(detail),
        false,
        NULL,
        "",
        0,
        true,
        -70,
        -65,
        (uint32_t)(50 + 55 + 55),
        3,
        "AP beacon");
    check(strstr(detail, "watch=") == NULL,
          "detail: no watch token on a MAC-less row even if the caller asks");

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

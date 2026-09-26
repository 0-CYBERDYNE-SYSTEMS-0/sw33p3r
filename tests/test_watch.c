/* Host tests: RF burst watch state machine (pure, no Furi).
 * See specs/full-capability-expansion-2026-09-20.md Phase 8 — open/close
 * edge math, gap boundary, max tracking, integer duty percent, honest
 * LAST age, and no overflow on long runs or across a uint32 tick wrap.
 * INTEGER MATH ONLY (-Wdouble-promotion is fatal on device). */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_watch.h"

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
    /* First tick and edges                                       */
    /* ---------------------------------------------------------- */
    RoomSweepWatch w;
    room_sweep_watch_reset(&w, 0);
    check(w.window_start_tick == 0 && !w.open && w.bursts == 0, "reset: fresh window, closed");
    check(!room_sweep_watch_has_burst(&w), "has_burst: false before any energy");
    check(room_sweep_watch_last_age_s(&w, 5000) == 0, "age: 0 before any burst");
    check(room_sweep_watch_duty_percent(&w, 5000) == 0, "duty: 0 before any burst");

    /* A quiet first tick must not open anything. */
    check(room_sweep_watch_tick(&w, 0, -90, true) == 0, "first tick: quiet rssi does not open");
    check(!w.open && w.bursts == 0, "first tick: watch stays closed");
    /* A quiet first tick with RX inactive likewise. */
    check(room_sweep_watch_tick(&w, 5, -60, false) == 0, "first tick: rx inactive does not open");
    check(!w.open, "first tick: still closed with strong rssi but rx inactive");

    /* Rising edge above the shared energy gate opens. */
    check(room_sweep_watch_tick(&w, 10, -70, true) == 0, "edge: opening tick returns 0");
    check(w.open && w.open_tick == 10 && w.last_above_tick == 10, "edge: burst opens at the tick");
    check(w.max_rssi == -70, "edge: max starts at the opening sample");
    check(room_sweep_watch_has_burst(&w), "has_burst: true while open");

    /* Below-threshold but inside the gap stays open. */
    check(room_sweep_watch_tick(&w, 15, -90, true) == 0, "gap: quiet tick inside gap keeps burst");
    check(w.open && w.last_above_tick == 10, "gap: last_above unchanged by silence");

    /* Max tracking inside one burst. */
    check(room_sweep_watch_tick(&w, 20, -55, true) == 0, "burst: stronger sample returns 0");
    check(w.max_rssi == -55, "burst: max tracks the strongest sample");
    check(room_sweep_watch_tick(&w, 25, -60, true) == 0, "burst: mid sample returns 0");
    check(w.max_rssi == -55, "burst: mid sample does not lower max");

    /* ---------------------------------------------------------- */
    /* Gap boundary: closes at exactly WATCH_BURST_GAP_MS          */
    /* last above at 25; 25+599 = 624 stays open, 625 closes.      */
    /* ---------------------------------------------------------- */
    check(room_sweep_watch_tick(&w, 624, -90, true) == 0, "gap boundary: 599 ms of silence stays open");
    check(w.open, "gap boundary: still open at gap-1");
    check(room_sweep_watch_tick(&w, 625, -90, true) == 1, "gap boundary: 600 ms of silence closes (+1)");
    check(!w.open, "gap boundary: closed watch is not open");
    check(w.bursts == 1, "gap boundary: one burst counted");
    check(w.total_open_ms == 15, "gap boundary: active span is first-to-last above (25-10)");
    check(w.last_close_tick == 625, "gap boundary: close tick stamped");

    /* Duration counts ACTIVE span, not the 600 ms gap tail. */
    check(w.total_open_ms < 600, "duration: detection gap is not logged as burst time");

    /* ---------------------------------------------------------- */
    /* Duty percent over the same window                           */
    /* ---------------------------------------------------------- */
    /* span = 625 ms, open = 15 ms -> 1500/625 = 2.4 -> 2 */
    check(room_sweep_watch_duty_percent(&w, 625) == 2, "duty: integer floor of the true ratio");
    /* An open burst contributes its span so far. */
    check(room_sweep_watch_tick(&w, 1000, -60, true) == 0, "duty: second burst opens");
    /* open contribution = 1000-1000 = 0 so far -> still 1500/1000 -> 1 */
    check(room_sweep_watch_duty_percent(&w, 1000) == 1, "duty: open burst adds its span so far");
    check(room_sweep_watch_tick(&w, 1050, -61, true) == 0, "duty: burst continues");
    /* open contribution = 50 -> (1500+5000... no: (15+50)*100)/1050 = 6500/1050 = 6 */
    check(room_sweep_watch_duty_percent(&w, 1050) == 6, "duty: open span grows the numerator");
    check(room_sweep_watch_tick(&w, 1050 + 600, -90, true) == 1, "duty: second burst closes");
    check(w.bursts == 2 && w.total_open_ms == 65, "duty: second burst banked (15 + (1050-1000))");

    /* ---------------------------------------------------------- */
    /* Max restarts per burst                                      */
    /* ---------------------------------------------------------- */
    RoomSweepWatch w2;
    room_sweep_watch_reset(&w2, 0);
    (void)room_sweep_watch_tick(&w2, 0, -50, true); /* open burst #1 */
    (void)room_sweep_watch_tick(&w2, 600, -110, true); /* silence closes #1 */
    (void)room_sweep_watch_tick(&w2, 2000, -70, true); /* burst #2 weaker but above the gate */
    check(w2.open && w2.max_rssi == -70, "max: restarts at the new burst's first sample");
    (void)room_sweep_watch_tick(&w2, 2005, -74, true);
    check(w2.max_rssi == -70, "max: weaker (still above threshold) sample ignored");
    (void)room_sweep_watch_tick(&w2, 2605, -110, true);
    check(w2.max_rssi == -70, "max: closed burst keeps its own max until the next open");

    /* ---------------------------------------------------------- */
    /* RX inactive decays an open burst (never stuck open)         */
    /* ---------------------------------------------------------- */
    RoomSweepWatch w3;
    room_sweep_watch_reset(&w3, 0);
    (void)room_sweep_watch_tick(&w3, 5000, -60, true);
    check(w3.open, "decay: burst open while tuned");
    check(room_sweep_watch_tick(&w3, 5005, -120, false) == 0, "decay: first untuned tick keeps it");
    check(room_sweep_watch_tick(&w3, 5600, -120, false) == 1, "decay: untuned gap closes the burst");
    check(w3.bursts == 1 && !w3.open, "decay: closed cleanly without samples");
    check(w3.total_open_ms == 0, "decay: zero-span burst banks 0 ms honestly");

    /* ---------------------------------------------------------- */
    /* LAST age                                                    */
    /* ---------------------------------------------------------- */
    check(room_sweep_watch_last_age_s(&w3, 5700) == 0, "age: under a second reads 0");
    check(room_sweep_watch_last_age_s(&w3, 5000 + 4600) == 4, "age: whole seconds since last energy");
    check(room_sweep_watch_last_age_s(&w2, 100000) == (100000 - 2005) / 1000, "age: anchored at last_above");

    /* ---------------------------------------------------------- */
    /* Duty helpers: clamps and overflow-safe arithmetic           */
    /* ---------------------------------------------------------- */
    RoomSweepWatch d;
    room_sweep_watch_reset(&d, 1000);
    check(room_sweep_watch_duty_percent(&d, 1000) == 0, "duty: zero-span window reads 0");
    check(room_sweep_watch_duty_percent(&d, 999) == 0, "duty: pre-window tick (wrap math) reads 0");

    room_sweep_watch_reset(&d, 0);
    d.total_open_ms = 250;
    check(room_sweep_watch_duty_percent(&d, 1000) == 25, "duty: exact quarter");
    d.total_open_ms = 1000;
    check(room_sweep_watch_duty_percent(&d, 1000) == 100, "duty: saturated window reads 100");
    d.total_open_ms = 500;
    check(room_sweep_watch_duty_percent(&d, 400) == 100, "duty: open exceeding span clamps to 100");

    /* Overflow guard: 80000000*100 would wrap uint32 without the shift. */
    d.total_open_ms = 80000000U;
    check(room_sweep_watch_duty_percent(&d, 100000000U) == 80, "duty: 80%% survives the uint32 multiply");

    /* Extreme scale: ~1%% at a giant window stays in range. */
    d.total_open_ms = 42949673U;
    {
        uint32_t pct = room_sweep_watch_duty_percent(&d, 4294967295U);
        check(pct <= 1U, "duty: giant window degrades gracefully (<=1 percent)");
    }

    /* ---------------------------------------------------------- */
    /* Long run: 1000 bursts over 1e6 simulated ticks (5000 s). Burst
     * period 1000 ms (longer than the 600 ms gap), 20 ms loud each. */
    RoomSweepWatch L;
    room_sweep_watch_reset(&L, 0);
    uint32_t t = 0;
    uint32_t closes = 0;
    for(uint32_t i = 0; i < 200000U; i++) {
        int8_t rssi = (i % 200U) < 4U ? (int8_t)-60 : (int8_t)-110;
        closes += (uint32_t)room_sweep_watch_tick(&L, t, rssi, true);
        t += 5U;
    }
    check(closes == 1000U, "long run: every burst closed exactly once");
    check(L.bursts == 1000U && closes == L.bursts, "long run: counter matches returns");
    check(L.total_open_ms == 15000U, "long run: banked spans exact (1000 x 15 ms)");
    check(!L.open, "long run: watch rests closed");
    check(room_sweep_watch_duty_percent(&L, t) == 1, "long run: duty stays 1 percent (15000/1000000)");
    check(room_sweep_watch_last_age_s(&L, t) == (t - L.last_above_tick) / 1000U, "long run: age honest");

    /* ---------------------------------------------------------- */
    /* uint32 tick wrap: open across 0xFFFFFFFF -> 0                */
    /* ---------------------------------------------------------- */
    RoomSweepWatch W;
    room_sweep_watch_reset(&W, 0);
    W.window_start_tick = 0xFFFFF000U; /* window anchored pre-wrap */
    (void)room_sweep_watch_tick(&W, 0xFFFFFFF0U, -60, true); /* open pre-wrap */
    (void)room_sweep_watch_tick(&W, 0xFFFFFFFAU, -60, true); /* last above pre-wrap */
    check(W.open, "wrap: burst open at 0xFFFFFFFA");
    check(room_sweep_watch_tick(&W, 0x000000FFU, -110, true) == 0, "wrap: 389 ms past wrap still open");
    check(room_sweep_watch_tick(&W, 0x00000252U, -110, true) == 1, "wrap: closes 600 ms past the wrap");
    check(W.bursts == 1 && W.total_open_ms == 10, "wrap: span banked across the wrap (0xFA-0xF0)");
    check(room_sweep_watch_last_age_s(&W, 0x00000252U) == 0, "wrap: age uses wrapped subtraction");
    /* The duty DENOMINATOR spans the wrap: window 0xFFFFF000..0x252 is a
     * wrapped 4690 ms span; 600 ms open in it reads 12 percent. */
    RoomSweepWatch W2 = {0};
    W2.window_start_tick = 0xFFFFF000U;
    W2.total_open_ms = 600U;
    check(room_sweep_watch_duty_percent(&W2, 0x00000252U) == 12, "wrap: duty denominator spans the wrap");

    /* ---------------------------------------------------------- */
    /* NULL / degenerate safety                                    */
    /* ---------------------------------------------------------- */
    check(room_sweep_watch_tick(NULL, 0, -60, true) == 0, "NULL watch refused");
    room_sweep_watch_reset(NULL, 0);
    check(!room_sweep_watch_has_burst(NULL), "has_burst: NULL is false");
    check(room_sweep_watch_last_age_s(NULL, 1) == 0, "age: NULL is 0");
    check(room_sweep_watch_duty_percent(NULL, 1) == 0, "duty: NULL is 0");

    /* Auto-arm: a zeroed struct starts its window on the first tick. */
    RoomSweepWatch z = {0};
    check(room_sweep_watch_tick(&z, 7777, -90, true) == 0, "auto-arm: zeroed struct ticks");
    check(z.window_start_tick == 7777, "auto-arm: window anchored at first tick");

    /* ---------------------------------------------------------- */
    /* Presentation helpers                                        */
    /* ---------------------------------------------------------- */
    char buf[24];
    room_sweep_watch_freq_label(buf, sizeof(buf), 433920000U);
    check_str("label: 433.9 MHz", buf, "433.9M");
    room_sweep_watch_freq_label(buf, sizeof(buf), 915000000U);
    check_str("label: 915.0 MHz", buf, "915.0M");
    room_sweep_watch_freq_label(buf, sizeof(buf), 303875000U);
    check_str("label: 303.875 rounds to 303.9", buf, "303.9M");
    room_sweep_watch_freq_label(buf, sizeof(buf), 927960000U);
    check_str("label: carry into whole MHz", buf, "928.0M");
    room_sweep_watch_freq_label(buf, sizeof(buf), 0U);
    check_str("label: no lock renders dash", buf, "-");
    room_sweep_watch_freq_label(buf, sizeof(buf), 300000000U);
    check(strlen(buf) + 1U <= ROOM_SWEEP_WATCH_LABEL_MAX, "label: fits the pinned buffer bound");

    char det[ROOM_SWEEP_WATCH_DETAIL_MAX];
    room_sweep_watch_detail(det, sizeof(det), 4, 7);
    check_str("detail: pinned format", det, "duty=4% bursts=7");
    room_sweep_watch_detail(det, sizeof(det), 100, 4294967295U);
    check_str("detail: worst-case counters", det, "duty=100% bursts=4294967295");
    check(
        strlen("duty=100% bursts=4294967295") + 1U <= ROOM_SWEEP_WATCH_DETAIL_MAX,
        "detail: worst case fits the pinned buffer bound");
    det[0] = 'X';
    room_sweep_watch_detail(det, 0, 1, 1);
    check(det[0] == 'X', "detail: zero capacity writes nothing");

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

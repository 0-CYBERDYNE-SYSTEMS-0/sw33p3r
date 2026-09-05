#include <stdio.h>

#include "../room_sweep_nrf24_state.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    RoomSweepNrf24State s;
    room_sweep_nrf24_init(&s);
    check(s.phase == RoomSweepNrf24Idle, "idle init");
    check(ROOM_SWEEP_NRF24_CHANNELS == 126U, "126 channels");

    check(room_sweep_nrf24_start(&s), "start scan");
    check(s.phase == RoomSweepNrf24Scanning, "scanning");
    check(room_sweep_nrf24_note_sample(&s, 40, true), "sample active");
    check(room_sweep_nrf24_note_sample(&s, 40, true), "second hit");
    check(s.hits[40] == 2, "hit count");
    check(room_sweep_nrf24_note_sample(&s, 10, false), "quiet channel");
    check(s.hits[10] == 0, "no hit when inactive");

    /* Fast-forward to end of pass. */
    s.channel = (uint8_t)(ROOM_SWEEP_NRF24_CHANNELS - 1U);
    check(room_sweep_nrf24_step_channel(&s), "last step finishes pass");
    check(s.phase == RoomSweepNrf24Done, "done after pass");
    check(s.active_channels == 1, "one active channel");
    check(s.top_channels[0] == 40, "top channel is 40");
    check(s.top_hits[0] == 2, "top hits");

    room_sweep_nrf24_init(&s);
    room_sweep_nrf24_start(&s);
    room_sweep_nrf24_set_present(&s, false);
    check(s.phase == RoomSweepNrf24Error, "missing module → error while scanning");

    room_sweep_nrf24_init(&s);
    room_sweep_nrf24_start(&s);
    for(uint16_t ch = 0; ch < 5; ch++) {
        room_sweep_nrf24_note_sample(&s, (uint8_t)ch, true);
        if(ch > 0) room_sweep_nrf24_note_sample(&s, (uint8_t)ch, true);
    }
    room_sweep_nrf24_stop(&s);
    check(s.phase == RoomSweepNrf24Done, "stop finalizes");
    check(s.active_channels == 5, "five active");
    check(s.top_channels[0] == 4 || s.top_hits[0] >= s.top_hits[1], "top ordered by hits");

    /* --- Activity integrator (feedback peak source) --- */
    printf("activity integrator\n");
    room_sweep_nrf24_init(&s);
    check(s.activity_score == 0 && s.activity_last_tick == 0, "activity init zero");
    check(room_sweep_nrf24_activity_to_rssi(&s) == -110, "zero activity maps to floor");
    check(room_sweep_nrf24_start(&s), "start for activity");
    check(room_sweep_nrf24_note_sample(&s, 10, true), "one active sample");
    check(s.activity_score == ROOM_SWEEP_NRF24_ACTIVITY_STEP, "one sample nudges score");
    check(room_sweep_nrf24_note_sample(&s, 10, false), "inactive sample");
    check(s.activity_score == ROOM_SWEEP_NRF24_ACTIVITY_STEP,
          "inactive sample does not raise score");

    for(int i = 0; i < 500; i++) room_sweep_nrf24_note_sample(&s, 10, true);
    check(s.activity_score == ROOM_SWEEP_NRF24_ACTIVITY_MAX, "sustained traffic saturates");

    /* 1 s of idle: ten 100 ms decay steps -> far below max. */
    room_sweep_nrf24_activity_tick(&s, 1000);
    check(s.activity_score < ROOM_SWEEP_NRF24_ACTIVITY_MAX / 2,
          "1s idle decays well below max");
    check(s.activity_last_tick == 1000, "stamp advances by consumed time");

    /* Keep idling: reaches zero within ~4 s. */
    uint32_t t = 1000;
    for(int i = 0; i < 50; i++) {
        t += 100;
        room_sweep_nrf24_activity_tick(&s, t);
    }
    check(s.activity_score == 0, "decays to zero within ~4s idle");

    /* 0 elapsed is a no-op (and does not advance the stamp). */
    check(room_sweep_nrf24_note_sample(&s, 20, true), "blip after idle");
    uint16_t before = s.activity_score;
    room_sweep_nrf24_activity_tick(&s, t);
    check(s.activity_score == before, "0 elapsed tick is a no-op");
    check(s.activity_last_tick == t, "0 elapsed does not advance stamp");

    /* Sub-100 ms elapsed is also a no-op. */
    room_sweep_nrf24_activity_tick(&s, t + 99);
    check(s.activity_score == before, "99ms elapsed is a no-op");

    /* Very old stamp: bounded path clears immediately and consumes time. */
    room_sweep_nrf24_activity_tick(&s, t + 100000U);
    check(s.activity_score == 0, "ancient elapsed clears score");

    /* Mapping: monotonic in score and bounded. */
    int prev = -1000;
    int mono_and_bounded = 1;
    for(uint32_t v = 0; v <= ROOM_SWEEP_NRF24_ACTIVITY_MAX; v += 25U) {
        s.activity_score = (uint16_t)v;
        int r = room_sweep_nrf24_activity_to_rssi(&s);
        if(r < -110 || r > -30) mono_and_bounded = 0;
        if(r < prev) mono_and_bounded = 0;
        prev = r;
    }
    check(mono_and_bounded, "activity_to_rssi monotonic and bounded");

    /* New scan resets the integrator. */
    check(room_sweep_nrf24_start(&s), "restart scan");
    check(s.activity_score == 0, "scan start resets activity");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

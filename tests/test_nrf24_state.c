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

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

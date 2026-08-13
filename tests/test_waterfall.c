#include <stdio.h>

#include "../room_sweep_waterfall.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

static void fill_snapshot(int8_t* snap, int8_t value) {
    for(int ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) snap[ch] = value;
}

int main(void) {
    RoomSweepWaterfallState s;

    /* Init: every cell and peak -127, newest 0, count 0. */
    room_sweep_waterfall_init(&s);
    int cells_empty = 1;
    for(int i = 0; i < ROOM_SWEEP_WATERFALL_COLS; i++) {
        for(int ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) {
            if(s.col[i][ch] != -127) cells_empty = 0;
        }
    }
    check(cells_empty, "init fills every cell with -127");
    check(s.newest == 0, "init sets newest to 0");
    check(s.count == 0, "init sets count to 0");
    int peaks_empty = 1;
    for(int ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) {
        if(s.peak[ch] != -127) peaks_empty = 0;
    }
    check(peaks_empty, "init sets every peak to -127");
    check(room_sweep_waterfall_col_at(&s, 0) == NULL, "col_at is NULL before any push");

    /* NULL snapshot: age-0 column is all -127, count becomes 1. */
    room_sweep_waterfall_push(&s, NULL);
    check(s.count == 1, "NULL push bumps count to 1");
    const int8_t* c0 = room_sweep_waterfall_col_at(&s, 0);
    check(c0 != NULL, "age-0 column exists after one push");
    int null_col_empty = (c0 != NULL);
    if(c0) {
        for(int ch = 0; ch < ROOM_SWEEP_WATERFALL_CHANNELS; ch++) {
            if(c0[ch] != -127) null_col_empty = 0;
        }
    }
    check(null_col_empty, "NULL snapshot writes -127 into every channel");
    check(
        room_sweep_waterfall_channel_at(&s, 0, 0) == -127,
        "NULL snapshot channel reads -127");

    /* Two real snapshots: age0 == second, age1 == first. */
    room_sweep_waterfall_init(&s);
    int8_t snap[ROOM_SWEEP_WATERFALL_CHANNELS];
    fill_snapshot(snap, -50);
    room_sweep_waterfall_push(&s, snap);
    fill_snapshot(snap, -60);
    room_sweep_waterfall_push(&s, snap);
    check(s.count == 2, "two pushes leave count at 2");
    const int8_t* age0 = room_sweep_waterfall_col_at(&s, 0);
    const int8_t* age1 = room_sweep_waterfall_col_at(&s, 1);
    check(
        age0 && age1 && age0[0] == -60 && age1[0] == -50,
        "age0 holds the second snapshot and age1 the first");
    check(room_sweep_waterfall_col_at(&s, 2) == NULL, "col_at beyond count is NULL");

    /* Clamp: below -127 stored as -127, above 0 stored as 0. */
    room_sweep_waterfall_init(&s);
    fill_snapshot(snap, -128);
    room_sweep_waterfall_push(&s, snap);
    fill_snapshot(snap, 5);
    room_sweep_waterfall_push(&s, snap);
    check(
        room_sweep_waterfall_channel_at(&s, 1, 0) == -127,
        "values below -127 clamp to -127");
    check(
        room_sweep_waterfall_channel_at(&s, 0, 0) == 0,
        "values above 0 clamp to 0");

    /* Peak-hold per channel; a -127 sample never clears it. */
    room_sweep_waterfall_init(&s);
    fill_snapshot(snap, -127);
    snap[3] = -80;
    room_sweep_waterfall_push(&s, snap);
    check(s.peak[3] == -80, "peak tracks the first signal on channel 3");
    fill_snapshot(snap, -127);
    snap[3] = -60;
    room_sweep_waterfall_push(&s, snap);
    check(s.peak[3] == -60, "peak rises to the strongest sample");
    fill_snapshot(snap, -127);
    room_sweep_waterfall_push(&s, snap);
    check(s.peak[3] == -60, "a -127 sample does not clear the peak");
    check(s.peak[0] == -127, "untouched channels keep an empty peak");

    /* Wrap: COLS+3 pushes saturate count and rotate the ring. */
    room_sweep_waterfall_init(&s);
    for(int k = 0; k < ROOM_SWEEP_WATERFALL_COLS + 3; k++) {
        fill_snapshot(snap, (int8_t)(-(k + 1)));
        room_sweep_waterfall_push(&s, snap);
    }
    check(s.count == ROOM_SWEEP_WATERFALL_COLS, "count saturates at COLS");
    check(
        s.newest == (uint8_t)((ROOM_SWEEP_WATERFALL_COLS + 3) % ROOM_SWEEP_WATERFALL_COLS),
        "newest index wraps with the ring");
    const int8_t* oldest = room_sweep_waterfall_col_at(&s, ROOM_SWEEP_WATERFALL_COLS - 1);
    check(
        oldest && oldest[0] == -4,
        "age COLS-1 holds the 24th-latest snapshot");
    check(
        room_sweep_waterfall_col_at(&s, ROOM_SWEEP_WATERFALL_COLS) == NULL,
        "age COLS is NULL");

    /* channel_at bounds. */
    check(
        room_sweep_waterfall_channel_at(&s, 0, ROOM_SWEEP_WATERFALL_CHANNELS) == -127,
        "channel_at rejects an out-of-range channel");
    check(
        room_sweep_waterfall_channel_at(&s, ROOM_SWEEP_WATERFALL_COLS, 0) == -127,
        "channel_at rejects an age past the ring");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

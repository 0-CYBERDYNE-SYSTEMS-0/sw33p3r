#pragma once

#include <stdint.h>

/*
 * Room Sweep — RF survey presets.
 *
 * Header-only and Flipper-header-free so host tests compile with plain cc:
 * tests/test_rf_presets.c pins the invariants that keep the four parallel
 * arrays in sync (the band filter table is indexed directly by preset index,
 * so silent drift there is an out-of-range tune).
 *
 * 20 presets, all inside the CC1101's three real bands (300-348 / 387-464 /
 * 779-928 MHz). Labels are display-width aware: FontKeyboard is 5 px/glyph
 * and the survey strip draws a label every RF_LABEL_STEP presets, so labels
 * stay ≤ 4 chars.
 */

#define RF_NUM_CHANNELS   20
#define RF_SAMPLES_PER_CH 8

/* Survey label stride: 20 presets over the 128 px strip leaves 6 px per bar. */
#define RF_LABEL_STEP 4

/* ISM / common surveillance frequencies (Hz), within CC1101 bands. */
static const uint32_t rf_channels[RF_NUM_CHANNELS] = {
    303875000, 315000000, 330000000, 345000000, /* CC1101 low band */
    390000000, 418000000, 433075000, 433420000, /* mid band, 433 cluster */
    433920000, 434420000, 434775000, 420000000, /* mid band */
    450000000, 868350000, 850000000, 880000000, /* 450 + EU 868 + uplink edge */
    902000000, 915000000, 925000000, 927500000, /* US 902-928 ISM */
};

static const char* rf_labels[RF_NUM_CHANNELS] = {
    "304", "315", "330", "345", "390", "418", "433", "433b", "434", "434b",
    "435", "420", "450", "868", "850", "880", "902", "915", "925", "927",
};

/* 0=low300, 1=mid400, 2=high900 — for EXT dual-CC1101 band filter.
 * Indexed directly by preset index (room_sweep.c rf_channel_allowed). */
static const uint8_t rf_channel_band[RF_NUM_CHANNELS] = {
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2,
};

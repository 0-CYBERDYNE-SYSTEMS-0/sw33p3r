#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "room_sweep_nrf24_state.h"

/*
 * Device-side nRF24 RPD channel survey on Flipper external SPI.
 * Detect-only: configure RX, sample RPD, never continuous jam TX.
 *
 * CE defaults to gpio_ext_pc3 (common Flipper nRF24 wiring / Momentum NRF24 SPI).
 * BFFB: bottom switch down = nRF24 on SPI; OTG power may be required.
 */

bool nrf24_survey_probe(RoomSweepNrf24State* state);
bool nrf24_survey_begin(RoomSweepNrf24State* state);
void nrf24_survey_end(void);
/* Sample current channel once, then optionally step. Returns true if pass done. */
bool nrf24_survey_sample_step(RoomSweepNrf24State* state, uint8_t dwell_ms);

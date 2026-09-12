#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "room_sweep_nrf24_state.h"

/*
 * Device-side nRF24 2.4 GHz ENERGY DETECTION survey on Flipper external SPI.
 * Detect-only: configure RX, sample the RPD bit, never transmit or jam.
 *
 * What is actually read: the nRF24L01+ RPD register (0x09 bit 0) — per the
 * Nordic product spec it is set when received power in the CURRENT channel
 * exceeds about -64 dBm, whatever the emitter. It carries no packet, address,
 * protocol, or identity information. Packet-based nRF24 identification is NOT
 * practical here: it needs a 40-bit address known a priori, and discovering
 * unknown addresses passively requires mousejack-class attack techniques that
 * MISSION.md/PROMPT.md ban. The nR mode therefore reports channel energy only.
 *
 * CE defaults to gpio_ext_pc3 (common Flipper nRF24 wiring / Momentum NRF24 SPI).
 * BFFB: bottom switch down = nRF24 on SPI; OTG power may be required.
 */

bool nrf24_survey_probe(RoomSweepNrf24State* state);
bool nrf24_survey_begin(RoomSweepNrf24State* state);
void nrf24_survey_end(void);
/* Sample current channel once, then optionally step. Returns true if pass done. */
bool nrf24_survey_sample_step(RoomSweepNrf24State* state, uint8_t dwell_ms);

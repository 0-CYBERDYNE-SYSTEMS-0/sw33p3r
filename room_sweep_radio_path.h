#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * BFFB bottom SPI mux: CC1101 pair XOR nRF24.
 * When operator chooses nRF24 path, Sub-GHz must use Flipper internal CC1101.
 */

typedef enum {
    RoomSweepSpiPathCc1101 = 0, /* prefer external dual CC1101 when present */
    RoomSweepSpiPathNrf24 = 1, /* nRF24 on external SPI; force internal Sub-GHz */
} RoomSweepSpiPath;

static inline const char* room_sweep_spi_path_label(RoomSweepSpiPath path) {
    switch(path) {
    case RoomSweepSpiPathNrf24:
        return "nRF24";
    case RoomSweepSpiPathCc1101:
    default:
        return "CC1101";
    }
}

static inline RoomSweepSpiPath room_sweep_spi_path_step(RoomSweepSpiPath path) {
    return path == RoomSweepSpiPathCc1101 ? RoomSweepSpiPathNrf24 : RoomSweepSpiPathCc1101;
}

/* True → open internal CC1101 only; do not claim external dual path. */
static inline bool room_sweep_force_internal_cc1101(RoomSweepSpiPath path) {
    return path == RoomSweepSpiPathNrf24;
}

/* External dual CC1101 is only usable when SPI path prefers CC1101. */
static inline bool room_sweep_external_cc1101_allowed(RoomSweepSpiPath path) {
    return path == RoomSweepSpiPathCc1101;
}

/* nRF24 survey is only meaningful when SPI path is nRF24. */
static inline bool room_sweep_nrf24_spi_selected(RoomSweepSpiPath path) {
    return path == RoomSweepSpiPathNrf24;
}

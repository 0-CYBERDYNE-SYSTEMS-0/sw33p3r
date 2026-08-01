#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

// ---------------------------------------------------------------------------
// App modes
// ---------------------------------------------------------------------------
typedef enum {
    SweepModeRF,    // Flipper-native sub-GHz RSSI sweep (analog bugs/cams)
    SweepModeWifi,  // Marauder WiFi AP scan over UART (BFFB ESP32)
    SweepModeBle,   // Marauder BLE sniff over UART (BFFB ESP32)
    SweepModeGps,   // Passive GPS NMEA listener on UART (BFFB GPS)
    SweepModeInfo,  // Capabilities / limits reference
    SweepModeCount
} SweepMode;

// ---------------------------------------------------------------------------
// RF (sub-GHz) sweep configuration
// ---------------------------------------------------------------------------
#define RF_NUM_CHANNELS   16
#define RF_SAMPLES_PER_CH 8

// ISM / common surveillance frequencies to sweep (Hz)
// All channels within CC1101 bands: 300-348 / 387-464 / 779-928 MHz
static const uint32_t rf_channels[RF_NUM_CHANNELS] = {
    303875000, 315000000, 330000000, 345000000,
    390000000, 418000000, 433075000, 433420000,
    433920000, 434420000, 434775000, 420000000,
    450000000, 868350000, 915000000, 925000000,
};

static const char* rf_labels[RF_NUM_CHANNELS] = {
    "304", "315", "330", "345",
    "390", "418", "433", "433b",
    "434", "434b", "435", "420",
    "450", "868", "915", "925",
};

// RSSI threshold for a "signal" alert (dBm)
#define RF_ALERT_THRESHOLD (-75.0f)

// ---------------------------------------------------------------------------
// Marauder UART configuration (BFFB -> Flipper USART1: PC0 TX / PC1 RX)
// ---------------------------------------------------------------------------
#define MARAUDER_BAUD        115200UL
#define MARAUDER_RX_BUF_SIZE 1024          // ring buffer for ISR-fed bytes
#define MARAUDER_LINE_MAX    128           // max chars per captured line
#define MARAUDER_MAX_LINES   8            // rolling lines kept for display

// GPS: alternate baud to try if no NMEA at Marauder baud. Common GPS = 9600.
#define GPS_BAUD_ALT         9600UL
#define GPS_SENTENCE_MAX     96            // display cap for one NMEA sentence

// ---------------------------------------------------------------------------
// Marauder scan state
// ---------------------------------------------------------------------------
typedef enum {
    MarauderIdle,
    MarauderScanning,
    MarauderError,
} MarauderState;

#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "room_sweep_state.h"

/* ---------------------------------------------------------------------------
 * App modes (tabs)
 * --------------------------------------------------------------------------- */
typedef enum {
    SweepModeRF,    // Sub-GHz RSSI (survey / band sweep / peak refine)
    SweepModeWifi,  // Marauder WiFi AP scan over UART
    SweepModeBle,   // Marauder BLE sniff over UART
    SweepModeGps,   // Passive GPS NMEA listener
    SweepModeTx,    // Dedicated TX tab (safety-gated)
    SweepModeInfo,  // Live capability / status card
    SweepModeCount
} SweepMode;

/* ---------------------------------------------------------------------------
 * RF sub-modes (cycled with Up/Down on RF tab)
 * --------------------------------------------------------------------------- */
typedef enum {
    RfSubSurvey,    // 16-point preset sweep (fast room check)
    RfSubSweep,     // Coarse band sweep with progress + peak hold
    RfSubPeak,      // Fine refinement around a detected peak
    RfSubCount
} RfSubMode;

/* ---------------------------------------------------------------------------
 * TX state machine (strict one-way arming, auto-disarm after TX)
 * --------------------------------------------------------------------------- */
typedef enum {
    TxDisarmed,      // Default. No TX possible without arming.
    TxArmed,         // User confirmed safety. Long-OK to transmit.
    TxStarting,      // Worker is preparing; TX has not started.
    TxTransmitting,  // Active carrier (bounded). Auto-disarms on finish.
} TxState;

/* ---------------------------------------------------------------------------
 * RF (sub-GHz) survey configuration — 16 preset channels
 * --------------------------------------------------------------------------- */
#define RF_NUM_CHANNELS   16
#define RF_SAMPLES_PER_CH 8

/* ISM / common surveillance frequencies (Hz), within CC1101 bands */
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

/* 0=low300, 1=mid400, 2=high900 — for EXT dual-CC1101 band filter */
static const uint8_t rf_channel_band[RF_NUM_CHANNELS] = {
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
};

typedef enum {
    ExtBandAuto = 0,
    ExtBand400,
    ExtBand900,
} ExtBandPref;

typedef enum {
    TargetNone = 0,
    TargetRF,
    TargetWifi,
    TargetBle,
} TargetKind;

/* RSSI threshold for a "signal" alert (dBm) */
#define RF_ALERT_THRESHOLD ROOM_SWEEP_SIGNAL_THRESHOLD_DBM

/* ---------------------------------------------------------------------------
 * Band sweep configuration (CC1101 three operating bands)
 * --------------------------------------------------------------------------- */
typedef struct {
    uint32_t start_hz;
    uint32_t stop_hz;
    const char* label;
} RfBand;

#define RF_BAND_COUNT 3
static const RfBand rf_bands[RF_BAND_COUNT] = {
    {300000000, 348000000, "300-348"},
    {387000000, 464000000, "387-464"},
    {779000000, 928000000, "779-928"},
};

/* Sweep step (Hz) — 250 kHz coarse, 25 kHz fine refinement */
#define SWEEP_STEP_COARSE  250000UL
#define SWEEP_STEP_FINE     25000UL
#define SWEEP_DWELL_MS          8    /* ms per frequency step */
#define SWEEP_SAMPLES           4    /* RSSI samples per step */
#define SWEEP_MAX_POINTS      600
#define PEAK_REFINE_SPAN  1000000UL  /* +/- 1 MHz around peak for refinement */

/* ---------------------------------------------------------------------------
 * JCMK BFFB / Marauder UART (matches 0xchocolate companion + Momentum FAP)
 *
 * USART (pins 13/14): Marauder CLI @ 115200 after expansion_disable().
 * LPUART (pins 15/16): GPIO NMEA GPS @ 9600 (Momentum "NMEA GPS UART" Extra
 * 15,16 — used by BFFB and many combo boards so GPS + WiFi can coexist).
 * Marauder `nmea` remains a fallback if GPIO is silent.
 *
 * Bottom switch routes the SPI radio: up = CC1101 pair, down = nRF24
 * (operator-verified 2026-08-02). ESP32 is on UART 13/14, unaffected by it.
 * Top switch selects the CC1101 path: up = 900 MHz, down = 400 MHz.
 * --------------------------------------------------------------------------- */
#define MARAUDER_BAUD        115200UL
#define MARAUDER_RX_BUF_SIZE 1024
#define MARAUDER_LINE_MAX    128
#define MARAUDER_MAX_LINES   24 /* deeper ring — AP/BLE bursts drop less */

/* JCMK CLI: sniffbeacon prints "-RSSI Ch: n MAC ESSID:" (WIFI_SCAN_AP).
 * sniffbt = BT_SNIFF_CMD. */
#define MARAUDER_CMD_WIFI    "sniffbeacon"
#define MARAUDER_CMD_BLE     "sniffbt"
#define MARAUDER_CMD_STOP    "stopscan"

/* GPIO GPS (LPUART). Stock modules default 9600; some boards use 115200. */
#define GPS_GPIO_BAUD_PRIMARY   9600UL
#define GPS_GPIO_BAUD_ALT      115200UL
#define GPS_SENTENCE_MAX         96

/* ---------------------------------------------------------------------------
 * Marauder scan state
 * --------------------------------------------------------------------------- */
typedef enum {
    MarauderNoDevice,   // UART handle not acquired
    MarauderIdle,       // Connected, no scan running
    MarauderScanning,   // Scan in progress
    MarauderDone,       // Scan completed with results
    MarauderError,      // Error response received
} MarauderState;

/* ---------------------------------------------------------------------------
 * Parsed WiFi/BLE results
 * --------------------------------------------------------------------------- */
#define MAX_WIFI_APS  12
#define MAX_BLE_DEVS  12

typedef struct {
    char ssid[33];       // SSID (max 32 chars + null)
    int8_t rssi;         // signal strength dBm
    uint8_t channel;     // WiFi channel
    char bssid[18];      // MAC string "AA:BB:CC:DD:EE:FF"
    uint32_t first_seen; // tick when first observed
    uint32_t last_seen;  // tick when last updated
    uint16_t observations;
    bool valid;
} WifiAp;

typedef struct {
    char name[33];       // device name
    int8_t rssi;         // signal strength dBm
    char mac[18];        // MAC string
    uint32_t first_seen; // tick when first observed
    uint32_t last_seen;  // tick when last updated
    uint16_t observations;
    bool valid;
} BleDev;

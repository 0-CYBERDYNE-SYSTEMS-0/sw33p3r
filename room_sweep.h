#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

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

/* RSSI threshold for a "signal" alert (dBm) */
#define RF_ALERT_THRESHOLD (-75.0f)

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
 * Marauder UART configuration (BFFB -> Flipper USART1)
 * --------------------------------------------------------------------------- */
#define MARAUDER_BAUD        115200UL
#define MARAUDER_RX_BUF_SIZE 1024
#define MARAUDER_LINE_MAX    128
#define MARAUDER_MAX_LINES   8

/* GPS alternate baud */
#define GPS_BAUD_ALT         9600UL
#define GPS_SENTENCE_MAX     96

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
    uint32_t last_seen;  // tick when last updated
    bool valid;
} WifiAp;

typedef struct {
    char name[33];       // device name
    int8_t rssi;         // signal strength dBm
    char mac[18];        // MAC string
    uint32_t last_seen;  // tick when last updated
    bool valid;
} BleDev;

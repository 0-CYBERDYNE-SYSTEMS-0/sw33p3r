/* Room Sweep v3.2 — multi-tab wireless assessment tool
 * Tabs: RF (survey/sweep/peak) | WiFi | BLE | GPS | TX | Info
 * RF prefers BFFB external CC1101 (cc1101_ext) via subghz_devices;
 * falls back to Flipper internal CC1101. WiFi/BLE/GPS via Marauder UART.
 * Momentum mntm-012, API 87.1.
 */
#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <furi_hal_power.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_types.h>
#include <furi_hal_serial_control.h>
#include <expansion/expansion.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <notification/notification_messages_notes.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/toolbox/level_duration.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "room_sweep.h"
#include "room_sweep_input.h"
#include "room_sweep_scan.h"
#include "session_log.h"
#include "nmea.h"
#include <storage/storage.h>

typedef enum {
    RadioPathNone = 0,
    RadioPathInternal,
    RadioPathExternal, /* BFFB dual CC1101 on SPI (Momentum cc1101_ext) */
} RadioPath;

/* ================================================================== */
/* Notification sequences (actual sound + vibro)                       */
/* NotificationSequence = NULL-terminated array of message pointers    */
/* ================================================================== */

/* Short Geiger click */
static const NotificationSequence seq_geiger_click = {
    &message_force_speaker_volume_setting_1f,
    &message_note_c6,
    &message_delay_10,
    &message_sound_off,
    NULL,
};

/* Vibro pulse */
static const NotificationSequence seq_vibro_pulse = {
    &message_force_vibro_setting_on,
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    NULL,
};

/* Test beep (confirming sound ON) */
static const NotificationSequence seq_test_beep = {
    &message_force_speaker_volume_setting_1f,
    &message_note_e5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

/* Test vibro (confirming vibro ON) */
static const NotificationSequence seq_test_vibro = {
    &message_force_vibro_setting_on,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    NULL,
};

/* Signal lock tone */
static const NotificationSequence seq_lock_tone = {
    &message_force_speaker_volume_setting_1f,
    &message_note_g4,
    &message_delay_100,
    NULL,
};

/* Stop sound */
static const NotificationSequence seq_sound_stop = {
    &message_sound_off,
    NULL,
};

/* TX arm warning: double beep */
static const NotificationSequence seq_tx_alert = {
    &message_force_speaker_volume_setting_1f,
    &message_note_a3,
    &message_delay_250,
    &message_sound_off,
    &message_delay_100,
    &message_note_a3,
    &message_delay_250,
    &message_sound_off,
    NULL,
};

/* ================================================================== */
/* App state                                                           */
/* ================================================================== */
typedef struct {
    SweepMode mode;
    RfSubMode rf_sub;
    volatile bool running;
    FuriMutex* mutex;
    FuriMutex* radio_mutex;

    /* RF survey state */
    volatile float rssi[RF_NUM_CHANNELS];
    volatile uint8_t sweep_ch;
    volatile bool rf_alert;
    volatile float peak_rssi;
    volatile uint8_t peak_ch;
    FuriThread* rf_thread;

    /* RF band sweep state */
    volatile bool sweep_running;
    uint8_t sweep_band_idx;
    volatile uint32_t sweep_freq;
    volatile uint8_t sweep_progress;   /* 0-100% */
    volatile float sweep_peak_rssi;
    volatile uint32_t sweep_peak_freq;
    volatile uint16_t sweep_points_done;
    volatile uint16_t sweep_points_total;

    /* Peak refinement state */
    volatile bool peak_running;
    volatile float peak_fine_rssi;
    volatile uint32_t peak_fine_freq;

    /* Last detected signal (for TX pre-load) */
    uint32_t last_signal_freq;

    /* SubGHz radio: prefer BFFB external SPI CC1101, else internal */
    const SubGhzDevice* radio;
    RadioPath radio_path;
    bool radio_otg_on;

    /* Marauder UART */
    MarauderState marauder_state;
    FuriHalSerialHandle* serial;
    char pending_lines[MARAUDER_MAX_LINES][MARAUDER_LINE_MAX];
    volatile uint8_t uart_line_head;
    volatile uint8_t uart_line_tail;
    char line_buf[MARAUDER_LINE_MAX];
    uint8_t line_pos;
    char last_uart_line[40]; /* debug: last non-empty RX line (UI) */
    uint32_t uart_rx_tick;   /* any Marauder line received */
    uint16_t uart_line_count;

    /* WiFi parsed results */
    WifiAp wifi_aps[MAX_WIFI_APS];
    uint8_t wifi_count;
    int8_t wifi_strongest;
    uint32_t wifi_last_scan_tick;
    uint8_t wifi_scroll;

    /* BLE parsed results */
    BleDev ble_devs[MAX_BLE_DEVS];
    uint8_t ble_count;
    int8_t ble_strongest;
    uint32_t ble_last_scan_tick;
    uint8_t ble_scroll;

    /* Rescan timer */
    uint32_t last_rescan_tick;
    bool auto_rescan;
    bool session_log_on;
    Storage* storage;
    uint32_t last_log_hit_ms;

    /* Baseline RSSI map (survey channels) */
    float baseline_rssi[RF_NUM_CHANNELS];
    bool baseline_set;

    /* Target lock — Geiger follows one source only */
    TargetKind target_kind;
    uint32_t target_freq_hz;
    char target_id[33];
    int8_t target_rssi;

    /* EXT dual-CC1101 band preference (BFFB top switch analogue) */
    ExtBandPref ext_band;

    /* UART dump ring for BFFB capture */
    char dump_lines[16][64];
    uint8_t dump_head;
    uint8_t dump_count;

    /* Notification / feedback */
    NotificationApp* notif;
    bool sound_on;
    bool vibro_on;
    uint32_t tick_count;
    uint32_t last_click_ms;
    uint32_t last_vibro_ms;
    bool was_alerting;
    uint8_t lock_ticks;

    /* GPS — GPIO LPUART primary, Marauder nmea fallback */
    GpsFix gps;
    volatile bool gps_active;
    volatile uint32_t gps_last_valid_tick;
    uint32_t gps_last_request_tick;
    bool gps_mark_set;
    float gps_mark_lat;
    float gps_mark_lon;
    bool gps_had_fix;
    FuriHalSerialHandle* gps_serial; /* LPUART pins 15/16 */
    uint32_t gps_gpio_baud;
    bool gps_gpio_open;
    bool gps_from_gpio; /* last nav sentence came from LPUART */

    /* TX (dedicated tab, safety-gated) */
    TxState tx_state;
    volatile bool tx_active;
    FuriThread* tx_thread;
    volatile uint32_t tx_freq_hz;
    volatile bool tx_level;
    volatile bool tx_started;
    uint8_t tx_freq_idx;
    uint8_t tx_duration_s;       /* 1-10 seconds, default 3 */
    volatile uint32_t tx_remaining_ms;

    /* Settings overlay */
    bool settings_active;
    uint8_t settings_sel;
} App;

/* ================================================================== */
/* TX frequency presets                                                */
/* ================================================================== */
#define TX_FREQ_PRESET_COUNT 6
static const uint32_t tx_freq_presets[TX_FREQ_PRESET_COUNT] = {
    433920000, 868350000, 915000000, 315000000, 390000000, 418000000,
};

/* Settings menu items */
enum {
    SET_SOUND = 0,
    SET_VIBRO,
    SET_RESCAN,
    SET_LOG,
    SET_EXTBAND,
    SET_BASELINE,
    SET_DUMP,
    SET_TXDUR,
    SET_COUNT,
};

#define RESCAN_INTERVAL_MS 5000
#define MARAUDER_SCAN_TIMEOUT_MS 30000
#define GPS_STALE_TIMEOUT_MS 5000
#define TX_MAX_DURATION_S  10
#define TX_DEFAULT_DURATION 3
#define LOG_HIT_MIN_MS 1500

static bool rf_channel_allowed(App* app, uint8_t ch) {
    if(app->radio_path != RadioPathExternal || app->ext_band == ExtBandAuto) return true;
    uint8_t b = rf_channel_band[ch];
    if(app->ext_band == ExtBand400) return b == 1;
    if(app->ext_band == ExtBand900) return b == 2;
    return true;
}

static uint8_t rf_default_sweep_band(App* app) {
    if(app->radio_path == RadioPathExternal) {
        if(app->ext_band == ExtBand400) return 1;
        if(app->ext_band == ExtBand900) return 2;
    }
    return app->sweep_band_idx;
}

static void dump_push_line(App* app, const char* line) {
    if(!line || !line[0]) return;
    strncpy(app->dump_lines[app->dump_head], line, 63);
    app->dump_lines[app->dump_head][63] = '\0';
    app->dump_head = (app->dump_head + 1) % 16;
    if(app->dump_count < 16) app->dump_count++;
}

static void log_hit_throttled(
    App* app,
    const char* kind,
    const char* id,
    int rssi,
    uint32_t freq_hz) {
    if(!app->session_log_on || !app->storage) return;
    uint32_t now = furi_get_tick();
    if(now - app->last_log_hit_ms < LOG_HIT_MIN_MS) return;
    app->last_log_hit_ms = now;
    bool has_pos = app->gps.has_pos && app->gps.has_fix &&
                   app->gps_last_valid_tick > 0 &&
                   now - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
    session_log_hit(
        kind,
        id,
        rssi,
        freq_hz,
        app->gps.latitude,
        app->gps.longitude,
        has_pos);
}

static void tx_preload_detected_frequency(App* app) {
    if(app->mode == SweepModeTx && app->last_signal_freq > 0) {
        app->tx_freq_hz = app->last_signal_freq;
    }
}

/* ================================================================== */
/* UART ISR callback                                                   */
/* ================================================================== */
static void uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(handle);
    App* app = ctx;
    if(event != FuriHalSerialRxEventData) return;

    uint8_t byte = furi_hal_serial_async_rx(app->serial);

    if(byte == '\n' || byte == '\r') {
        if(app->line_pos > 0) {
            uint8_t next_head = (app->uart_line_head + 1) % MARAUDER_MAX_LINES;
            if(next_head != app->uart_line_tail) {
                app->line_buf[app->line_pos] = '\0';
                strncpy(app->pending_lines[app->uart_line_head], app->line_buf, MARAUDER_LINE_MAX - 1);
                app->pending_lines[app->uart_line_head][MARAUDER_LINE_MAX - 1] = '\0';
                app->uart_line_head = next_head;
            }
            app->line_pos = 0;
        }
    } else {
        if(app->line_pos < MARAUDER_LINE_MAX - 1) {
            app->line_buf[app->line_pos++] = (char)byte;
        }
    }
}

/* ================================================================== */
/* Marauder UART open / close / send                                   */
/* ================================================================== */
static bool marauder_open(App* app) {
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial) {
        app->marauder_state = MarauderNoDevice;
        Expansion* expansion = furi_record_open(RECORD_EXPANSION);
        expansion_enable(expansion);
        furi_record_close(RECORD_EXPANSION);
        return false;
    }

    furi_hal_serial_init(app->serial, MARAUDER_BAUD);
    furi_hal_serial_async_rx_start(app->serial, uart_rx_cb, app, false);
    app->marauder_state = MarauderIdle;
    app->uart_line_head = 0;
    app->uart_line_tail = 0;
    app->line_pos = 0;
    return true;
}

static void marauder_close(App* app) {
    if(!app->serial) return;
    furi_hal_serial_async_rx_stop(app->serial);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);
    app->serial = NULL;

    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
}

/* Marauder CLI (JCMK): readStringUntil('\\n') + trim. Official Flipper
 * companion (0xchocolate) transmits command + '\\n' only — not CRLF. */
static void marauder_send(App* app, const char* cmd) {
    if(!app->serial) return;
    furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx(app->serial, (const uint8_t*)"\n", 1);
    furi_hal_serial_tx_wait_complete(app->serial);
}

static void clear_wifi_results(App* app) {
    memset(app->wifi_aps, 0, sizeof(app->wifi_aps));
    app->wifi_count = 0;
    app->wifi_strongest = -127;
    app->wifi_last_scan_tick = 0;
    app->wifi_scroll = 0;
}

static void clear_ble_results(App* app) {
    memset(app->ble_devs, 0, sizeof(app->ble_devs));
    app->ble_count = 0;
    app->ble_strongest = -127;
    app->ble_last_scan_tick = 0;
    app->ble_scroll = 0;
}

static void marauder_reset_results(App* app) {
    clear_wifi_results(app);
    clear_ble_results(app);
}

static void clear_uart_lines(App* app) {
    app->uart_line_tail = app->uart_line_head;
}

static void marauder_stop_scan(App* app) {
    /* Always poke stopscan when UART is up — leaves nmea/sniff cleanly. */
    if(app->serial && app->marauder_state != MarauderNoDevice) {
        marauder_send(app, MARAUDER_CMD_STOP);
        furi_delay_ms(80);
    }
    if(app->marauder_state != MarauderNoDevice) app->marauder_state = MarauderIdle;
}

/* clear_results: true on manual OK / tab enter; false keeps table on soft restart */
static void marauder_start_scan(App* app, const char* command, bool clear_results) {
    if(!app->serial) return;
    marauder_stop_scan(app);
    if(clear_results) {
        if(app->mode == SweepModeWifi) clear_wifi_results(app);
        if(app->mode == SweepModeBle) clear_ble_results(app);
        clear_uart_lines(app);
    }
    marauder_send(app, command);
    app->marauder_state = MarauderScanning;
    app->last_rescan_tick = furi_get_tick();
}

static void marauder_start_for_mode(App* app) {
    if(!app->serial) return;
    if(app->mode == SweepModeWifi) {
        marauder_start_scan(app, MARAUDER_CMD_WIFI, true);
    } else if(app->mode == SweepModeBle) {
        marauder_start_scan(app, MARAUDER_CMD_BLE, true);
    }
}

/* GPIO GPS on LPUART (PC1/PC0 = Flipper pins 15/16). Coexists with USART
 * Marauder on 13/14. Momentum setting: NMEA GPS UART = Extra 15,16. */
static void gps_gpio_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(handle);
    App* app = ctx;
    if(event != FuriHalSerialRxEventData || !app->gps_active) return;

    uint8_t byte = furi_hal_serial_async_rx(app->gps_serial);
    uint32_t nav_before = app->gps.nav_sentences;
    nmea_feed(&app->gps, (char)byte);
    if(app->gps.nav_sentences != nav_before) {
        app->gps_last_valid_tick = furi_get_tick();
        app->gps_from_gpio = true;
    }
}

static void gps_gpio_close(App* app) {
    if(!app->gps_serial) return;
    furi_hal_serial_async_rx_stop(app->gps_serial);
    furi_hal_serial_deinit(app->gps_serial);
    furi_hal_serial_control_release(app->gps_serial);
    app->gps_serial = NULL;
    app->gps_gpio_open = false;
}

static bool gps_gpio_open(App* app, uint32_t baud) {
    if(app->gps_serial) gps_gpio_close(app);

    app->gps_serial = furi_hal_serial_control_acquire(FuriHalSerialIdLpuart);
    if(!app->gps_serial) return false;

    furi_hal_serial_init(app->gps_serial, baud);
    furi_hal_serial_async_rx_start(app->gps_serial, gps_gpio_rx_cb, app, false);
    app->gps_gpio_baud = baud;
    app->gps_gpio_open = true;
    return true;
}

/* Marauder CLI fallback when GPIO is silent (ESP32-streamed NMEA). */
static void gps_marauder_stream(App* app) {
    if(!app->serial) return;
    marauder_send(app, "nmea");
    app->gps_last_request_tick = furi_get_tick();
    if(app->marauder_state != MarauderNoDevice) {
        app->marauder_state = MarauderScanning;
    }
}

static void gps_marauder_poll(App* app) {
    if(!app->serial) return;
    marauder_send(app, "gps -g nmea");
    app->gps_last_request_tick = furi_get_tick();
}

static void update_gps_mode(App* app) {
    bool active = app->mode == SweepModeGps;
    if(active && !app->gps_active) {
        nmea_init(&app->gps);
        app->gps_last_valid_tick = 0;
        app->gps_had_fix = false;
        app->gps_from_gpio = false;
        /* 5V often needed for external GPS modules */
        if(!furi_hal_power_is_otg_enabled()) {
            furi_hal_power_enable_otg();
            app->radio_otg_on = true; /* share OTG ownership with radio path */
            furi_delay_ms(30);
        }
        gps_gpio_open(app, GPS_GPIO_BAUD_PRIMARY);
        /* Don't start Marauder nmea yet — avoid UART noise; fallback after silence */
        app->gps_last_request_tick = furi_get_tick();
    } else if(!active && app->gps_active) {
        gps_gpio_close(app);
        if(app->serial && app->marauder_state == MarauderScanning) {
            marauder_send(app, "stopscan");
            if(app->marauder_state != MarauderNoDevice) app->marauder_state = MarauderIdle;
        }
    }
    app->gps_active = active;
}

/* ================================================================== */
/* Marauder line parser — JCMK ESP32Marauder (BFFB = Dev Board Pro)    */
/*                                                                     */
/* WiFi (scanall / sniffbeacon → AP beacon path in WiFiScan.cpp):      */
/*   "-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00"           */
/* BLE (sniffbt → BT_SCAN_ALL callback):                               */
/*   "<rssi> Device: <name|mac>"  e.g. "-60 Device: AirPods"           */
/* GPS (nmea): Serial.println of NMEA + generateGXgga/GXrmc.         */
/* Lines starting with '#' are echoes — skip. No scan "done" marker.   */
/* ================================================================== */

/* Find integer value after a key string. Returns true if found. */
static bool parse_int_after(const char* line, const char* key, int* out) {
    const char* p = strstr(line, key);
    if(!p) return false;
    p += strlen(key);
    while(*p == ' ' || *p == ':' || *p == '"' || *p == ',') p++;
    char* end;
    long val = strtol(p, &end, 10);
    if(end == p) return false;
    *out = (int)val;
    return true;
}

/* Extract string value after key (up to space/comma/quote/end). */
static void parse_str_after(const char* line, const char* key, char* out, size_t out_sz) {
    out[0] = '\0';
    const char* p = strstr(line, key);
    if(!p) return;
    p += strlen(key);
    while(*p == ' ' || *p == ':' || *p == '"') p++;
    size_t i = 0;
    while(*p && *p != '"' && *p != ',' && i < out_sz - 1) {
        if(*p == ' ' && i > 0) break; /* stop at first space after content */
        out[i++] = *p++;
    }
    out[i] = '\0';
}

/* Extract ESSID which may contain spaces (everything after "ESSID: " to end) */
static void parse_essid(const char* line, char* out, size_t out_sz) {
    out[0] = '\0';
    const char* p = strstr(line, "ESSID: ");
    if(!p) p = strstr(line, "ESSID:");
    if(!p) return;
    p += 6; /* skip "ESSID:" */
    while(*p == ' ') p++;
    size_t i = 0;
    /* ESSID goes to end of line (may contain spaces), strip trailing hex */
    const char* end = line + strlen(line);
    /* Trim trailing " XX XX" capability bytes if present */
    const char* trim = end;
    while(trim > p && *(trim-1) == ' ') trim--;
    /* Check for trailing 2 hex byte pairs " XX XX" */
    if(trim - p > 6 && *(trim-6) == ' ' && *(trim-3) == ' ') {
        trim -= 6;
    }
    while(p < trim && i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

static bool copy_mac(const char* line, char* out) {
    size_t line_len = strlen(line);
    if(line_len < 17) return false;
    for(size_t offset = 0; offset + 17 <= line_len; offset++) {
        bool match = true;
        for(size_t i = 0; i < 17; i++) {
            if(i % 3 == 2) {
                if(line[offset + i] != ':') match = false;
            } else if(!is_hex_digit(line[offset + i])) {
                match = false;
            }
        }
        if(!match) continue;
        if(offset > 0 && is_hex_digit(line[offset - 1])) continue;
        if(offset + 17 < line_len && is_hex_digit(line[offset + 17])) continue;
        memcpy(out, line + offset, 17);
        out[17] = '\0';
        return true;
    }
    return false;
}

static void parse_device_name(const char* line, char* out, size_t out_sz) {
    const char* key = strstr(line, "Device:");
    if(!key) key = strstr(line, "Name:");
    if(!key) return;
    key = strchr(key, ':') + 1;
    while(*key == ' ' || *key == '"') key++;
    const char* end = strstr(key, " MAC:");
    if(!end) end = strstr(key, " RSSI");
    if(!end) end = line + strlen(line);
    while(end > key && (end[-1] == ' ' || end[-1] == '"')) end--;
    size_t len = (size_t)(end - key);
    if(len >= out_sz) len = out_sz - 1;
    memcpy(out, key, len);
    out[len] = '\0';
}

/* Parse a WiFi AP result line into the AP table.
 * Format: "-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: Name 00 00" */
static bool parse_wifi_line(App* app, const char* line) {
    if(line[0] == '#') return false; /* command echo */
    if(line[0] == '>') return false; /* prompt */

    int rssi_val = 0;
    bool found_rssi = false;

    /* Primary: line starts with negative number (Marauder format) */
    if(line[0] == '-' && line[1] >= '0' && line[1] <= '9') {
        char* end;
        long v = strtol(line, &end, 10);
        if(v >= -120 && v <= 0 && (*end == ' ' || *end == '\0')) {
            rssi_val = (int)v;
            found_rssi = true;
        }
    }
    /* Fallback: "RSSI: -45" or "rssi":-45 (other firmware) */
    if(!found_rssi) {
        if(!parse_int_after(line, "RSSI", &rssi_val) &&
           !parse_int_after(line, "rssi", &rssi_val)) {
            return false;
        }
    }
    if(rssi_val > 0 || rssi_val < -120) return false;

    /* Must have ESSID or Ch: to be a WiFi AP line */
    if(!strstr(line, "ESSID") && !strstr(line, "Ch:") && !strstr(line, "essid")) {
        return false;
    }

    char ssid[33] = {0};
    parse_essid(line, ssid, sizeof(ssid));
    if(ssid[0] == '\0') {
        parse_str_after(line, "ESSID", ssid, sizeof(ssid));
    }
    if(ssid[0] == '\0') snprintf(ssid, sizeof(ssid), "AP_%d", app->wifi_count);

    int ch_val = 0;
    parse_int_after(line, "Ch:", &ch_val);
    if(ch_val == 0) parse_int_after(line, "Channel", &ch_val);

    /* BSSID: look for MAC pattern (XX:XX:XX:XX:XX:XX) */
    char bssid[18] = {0};
    copy_mac(line, bssid);

    /* Update existing or add new */
    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
        if(app->wifi_aps[i].valid &&
           (strcmp(app->wifi_aps[i].ssid, ssid) == 0 ||
            (bssid[0] && strcmp(app->wifi_aps[i].bssid, bssid) == 0))) {
            app->wifi_aps[i].rssi = (int8_t)rssi_val;
            app->wifi_aps[i].channel = (uint8_t)ch_val;
            app->wifi_aps[i].last_seen = now;
            return true;
        }
    }
    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
        if(!app->wifi_aps[i].valid) {
            app->wifi_aps[i].valid = true;
            strncpy(app->wifi_aps[i].ssid, ssid, 32);
            app->wifi_aps[i].ssid[32] = '\0';
            app->wifi_aps[i].rssi = (int8_t)rssi_val;
            app->wifi_aps[i].channel = (uint8_t)ch_val;
            strncpy(app->wifi_aps[i].bssid, bssid, 17);
            app->wifi_aps[i].bssid[17] = '\0';
            app->wifi_aps[i].last_seen = now;
            app->wifi_count++;
            return true;
        }
    }
    return true;
}

/* Parse a BLE device result line.
 * Format: "-60 Device: DeviceName" */
static bool parse_ble_line(App* app, const char* line) {
    if(line[0] == '#') return false;
    if(line[0] == '>') return false;

    int rssi_val = 0;
    bool found_rssi = false;

    /* Primary: line starts with negative number */
    if(line[0] == '-' && line[1] >= '0' && line[1] <= '9') {
        char* end;
        long v = strtol(line, &end, 10);
        if(v >= -120 && v <= 0 && (*end == ' ' || *end == '\0')) {
            rssi_val = (int)v;
            found_rssi = true;
        }
    }
    /* Fallback */
    if(!found_rssi) {
        if(!parse_int_after(line, "RSSI", &rssi_val) &&
           !parse_int_after(line, "rssi", &rssi_val)) {
            return false;
        }
    }
    if(rssi_val > 0 || rssi_val < -120) return false;

        /* Marauder: "Device:" / "Name:" — or bare MAC after RSSI */
    char mac_early[18] = {0};
    copy_mac(line, mac_early);
    if(!strstr(line, "Device") && !strstr(line, "Name") && !strstr(line, "name") &&
       mac_early[0] == '\0') {
        return false;
    }

    char name[33] = {0};
    parse_device_name(line, name, sizeof(name));
    if(name[0] == '\0') parse_str_after(line, "Device", name, sizeof(name));
    if(name[0] == '\0') parse_str_after(line, "Name", name, sizeof(name));
    if(name[0] == '\0' && mac_early[0]) strncpy(name, mac_early, sizeof(name) - 1);
    if(name[0] == '\0') snprintf(name, sizeof(name), "BLE_%d", app->ble_count);

    char mac[18] = {0};
    parse_str_after(line, "MAC", mac, sizeof(mac));
    /* Marauder prints MAC as the Device field when the peer has no name. */
    if(mac[0] == '\0') copy_mac(line, mac);
    if(mac[0] == '\0' && name[0] != '\0') {
        char maybe[18] = {0};
        if(copy_mac(name, maybe)) {
            strncpy(mac, maybe, sizeof(mac) - 1);
        }
    }

    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
        if(app->ble_devs[i].valid &&
           (strcmp(app->ble_devs[i].name, name) == 0 ||
            (mac[0] && strcmp(app->ble_devs[i].mac, mac) == 0))) {
            app->ble_devs[i].rssi = (int8_t)rssi_val;
            app->ble_devs[i].last_seen = now;
            return true;
        }
    }
    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
        if(!app->ble_devs[i].valid) {
            app->ble_devs[i].valid = true;
            strncpy(app->ble_devs[i].name, name, 32);
            app->ble_devs[i].name[32] = '\0';
            app->ble_devs[i].rssi = (int8_t)rssi_val;
            strncpy(app->ble_devs[i].mac, mac, 17);
            app->ble_devs[i].mac[17] = '\0';
            app->ble_devs[i].last_seen = now;
            app->ble_count++;
            return true;
        }
    }
    return true;
}

/* Process new UART lines — route to appropriate parser.
 * Marauder streams results continuously until stopscan — no "done" marker.
 * Lines starting with '#' are command echoes; '> ' is the prompt. */
static void process_uart_lines(App* app) {
    char latest[MARAUDER_LINE_MAX];
    while(app->uart_line_tail != app->uart_line_head) {
        uint8_t tail = app->uart_line_tail;
        strncpy(latest, app->pending_lines[tail], MARAUDER_LINE_MAX - 1);
        latest[MARAUDER_LINE_MAX - 1] = '\0';
        app->uart_line_tail = (tail + 1) % MARAUDER_MAX_LINES;

        if(app->gps_active) {
            uint32_t nav_sentences = app->gps.nav_sentences;
            for(const char* p = latest; *p; p++) nmea_feed(&app->gps, *p);
            if(app->gps.nav_sentences != nav_sentences) {
                app->gps_last_valid_tick = furi_get_tick();
                app->gps_from_gpio = false; /* came from Marauder USART */
            }
        }

        /* Any non-empty line proves BFFB UART is alive */
        if(latest[0]) {
            app->uart_rx_tick = furi_get_tick();
            app->uart_line_count++;
            strncpy(app->last_uart_line, latest, sizeof(app->last_uart_line) - 1);
            app->last_uart_line[sizeof(app->last_uart_line) - 1] = '\0';
            dump_push_line(app, latest);
        }

        if(latest[0] == '#' || latest[0] == '>') continue;

        if(strstr(latest, "not supported") || strstr(latest, "Index not in range")) {
            app->marauder_state = MarauderError;
            continue;
        }

        if(app->mode == SweepModeWifi) {
            if(parse_wifi_line(app, latest)) {
                app->wifi_strongest = -127;
                int best_i = -1;
                for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
                    if(app->wifi_aps[i].valid && app->wifi_aps[i].rssi > app->wifi_strongest) {
                        app->wifi_strongest = app->wifi_aps[i].rssi;
                        best_i = (int)i;
                    }
                }
                app->wifi_last_scan_tick = furi_get_tick();
                if(app->marauder_state == MarauderIdle ||
                   app->marauder_state == MarauderError) {
                    app->marauder_state = MarauderScanning;
                }
                if(best_i >= 0 && app->wifi_aps[best_i].rssi > RF_ALERT_THRESHOLD) {
                    log_hit_throttled(
                        app,
                        "WIFI",
                        app->wifi_aps[best_i].ssid,
                        app->wifi_aps[best_i].rssi,
                        0);
                }
                if(app->target_kind == TargetWifi && best_i >= 0 &&
                   (strcmp(app->target_id, app->wifi_aps[best_i].ssid) == 0 ||
                    (app->wifi_aps[best_i].bssid[0] &&
                     strcmp(app->target_id, app->wifi_aps[best_i].bssid) == 0))) {
                    app->target_rssi = app->wifi_aps[best_i].rssi;
                }
            }
        } else if(app->mode == SweepModeBle) {
            if(parse_ble_line(app, latest)) {
                app->ble_strongest = -127;
                int best_i = -1;
                for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
                    if(app->ble_devs[i].valid && app->ble_devs[i].rssi > app->ble_strongest) {
                        app->ble_strongest = app->ble_devs[i].rssi;
                        best_i = (int)i;
                    }
                }
                app->ble_last_scan_tick = furi_get_tick();
                if(app->marauder_state == MarauderIdle ||
                   app->marauder_state == MarauderError) {
                    app->marauder_state = MarauderScanning;
                }
                if(best_i >= 0 && app->ble_devs[best_i].rssi > RF_ALERT_THRESHOLD) {
                    log_hit_throttled(
                        app,
                        "BLE",
                        app->ble_devs[best_i].name,
                        app->ble_devs[best_i].rssi,
                        0);
                }
                if(app->target_kind == TargetBle && best_i >= 0 &&
                   (strcmp(app->target_id, app->ble_devs[best_i].name) == 0 ||
                    (app->ble_devs[best_i].mac[0] &&
                     strcmp(app->target_id, app->ble_devs[best_i].mac) == 0))) {
                    app->target_rssi = app->ble_devs[best_i].rssi;
                }
            }
        }
    }
}

/* ================================================================== */
/* SubGHz radio — BFFB external CC1101 (SPI) preferred, else internal  */
/* BFFB wiki: dual CC1101 on Flipper SPI; top switch 400 vs 900 MHz;   */
/* bottom switch ESP32 for CC1101 access. Momentum: cc1101_ext.        */
/* ================================================================== */
static void radio_otg_on(App* app) {
    if(!furi_hal_power_is_otg_enabled()) {
        furi_hal_power_enable_otg();
        app->radio_otg_on = true;
        furi_delay_ms(50);
    }
}

static void radio_otg_off(App* app) {
    if(app->radio_otg_on) {
        furi_hal_power_disable_otg();
        app->radio_otg_on = false;
    }
}

static bool radio_open(App* app) {
    app->radio = NULL;
    app->radio_path = RadioPathNone;
    app->radio_otg_on = false;

    subghz_devices_init();

    /* Prefer external SPI CC1101 (BFFB dual modules / Flux Capacitor / etc.) */
    radio_otg_on(app);
    const SubGhzDevice* ext = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    if(ext && subghz_devices_is_connect(ext)) {
        if(subghz_devices_begin(ext)) {
            app->radio = ext;
            app->radio_path = RadioPathExternal;
        }
    }

    if(!app->radio) {
        radio_otg_off(app);
        const SubGhzDevice* inter = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        if(inter) {
            app->radio = inter;
            app->radio_path = RadioPathInternal;
        }
    }

    if(!app->radio) {
        subghz_devices_deinit();
        return false;
    }

    subghz_devices_reset(app->radio);
    subghz_devices_idle(app->radio);
    subghz_devices_load_preset(app->radio, FuriHalSubGhzPresetOok650Async, NULL);
    return true;
}

static void radio_close(App* app) {
    if(app->radio) {
        subghz_devices_idle(app->radio);
        subghz_devices_sleep(app->radio);
        if(app->radio_path == RadioPathExternal) {
            subghz_devices_end(app->radio);
        }
        app->radio = NULL;
    }
    radio_otg_off(app);
    subghz_devices_deinit();
    app->radio_path = RadioPathNone;
}

static void radio_rx_at(App* app, uint32_t hz) {
    if(!app->radio) return;
    subghz_devices_idle(app->radio);
    subghz_devices_set_frequency(app->radio, hz);
    subghz_devices_flush_rx(app->radio);
    subghz_devices_set_rx(app->radio);
}

static float radio_rssi(App* app) {
    if(!app->radio) return -120.0f;
    return subghz_devices_get_rssi(app->radio);
}

static void radio_idle(App* app) {
    if(app->radio) subghz_devices_idle(app->radio);
}

/* ================================================================== */
/* RF sweep thread (handles survey / band sweep / peak refine)         */
/* ================================================================== */
static int32_t rf_sweep_thread(void* ctx) {
    App* app = ctx;

    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    if(app->radio) {
        subghz_devices_reset(app->radio);
        subghz_devices_idle(app->radio);
        subghz_devices_load_preset(app->radio, FuriHalSubGhzPresetOok650Async, NULL);
    }
    furi_mutex_release(app->radio_mutex);

    while(app->running) {
        if(app->tx_active || !app->radio) {
            furi_delay_ms(20);
            continue;
        }

        if(app->rf_sub == RfSubSurvey) {
            bool any_alert = false;
            float peak = -120.0f;
            uint8_t peak_idx = 0;

            for(uint8_t ch = 0; ch < RF_NUM_CHANNELS && app->running; ch++) {
                if(app->tx_active) break;
                if(!rf_channel_allowed(app, ch)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->rssi[ch] = -120.0f;
                    furi_mutex_release(app->mutex);
                    continue;
                }
                furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
                if(!app->running || app->tx_active || !app->radio) {
                    furi_mutex_release(app->radio_mutex);
                    break;
                }
                radio_rx_at(app, rf_channels[ch]);

                float sum = 0;
                for(uint8_t s = 0; s < RF_SAMPLES_PER_CH && app->running && !app->tx_active; s++) {
                    sum += radio_rssi(app);
                    furi_delay_ms(5);
                }
                bool sample_valid = app->running && !app->tx_active;
                float avg = sum / RF_SAMPLES_PER_CH;
                radio_idle(app);
                furi_mutex_release(app->radio_mutex);
                if(!sample_valid) break;

                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->rssi[ch] = avg;
                app->sweep_ch = ch;
                furi_mutex_release(app->mutex);

                if(avg > RF_ALERT_THRESHOLD) any_alert = true;
                if(avg > peak) {
                    peak = avg;
                    peak_idx = ch;
                }
            }

            app->peak_rssi = peak;
            app->peak_ch = peak_idx;
            app->rf_alert = any_alert;
            if(peak > RF_ALERT_THRESHOLD) {
                app->last_signal_freq = rf_channels[peak_idx];
                char id[12];
                snprintf(id, sizeof(id), "%s", rf_labels[peak_idx]);
                log_hit_throttled(app, "RF", id, (int)peak, rf_channels[peak_idx]);
            }
            if(app->target_kind == TargetRF && app->target_freq_hz > 0) {
                /* Use peak if it matches locked channel band, else keep last */
                for(uint8_t ch = 0; ch < RF_NUM_CHANNELS; ch++) {
                    if(rf_channels[ch] == app->target_freq_hz) {
                        app->target_rssi = (int8_t)app->rssi[ch];
                        break;
                    }
                }
            }

        } else if(app->rf_sub == RfSubSweep && app->sweep_running) {
            if(app->radio_path == RadioPathExternal && app->ext_band != ExtBandAuto) {
                app->sweep_band_idx = rf_default_sweep_band(app);
            }
            const RfBand* band = &rf_bands[app->sweep_band_idx];
            uint32_t total_steps = (band->stop_hz - band->start_hz) / SWEEP_STEP_COARSE;
            if(total_steps > SWEEP_MAX_POINTS) total_steps = SWEEP_MAX_POINTS;
            app->sweep_points_total = (uint16_t)total_steps;
            app->sweep_points_done = 0;

            float best_rssi = -120.0f;
            uint32_t best_freq = band->start_hz;

            for(uint32_t step = 0; step < total_steps && app->running && app->sweep_running; step++) {
                if(app->tx_active) break;
                uint32_t freq = band->start_hz + step * SWEEP_STEP_COARSE;
                app->sweep_freq = freq;

                furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
                if(!app->running || app->tx_active || !app->sweep_running || !app->radio) {
                    furi_mutex_release(app->radio_mutex);
                    break;
                }
                radio_rx_at(app, freq);

                float sum = 0;
                for(uint8_t s = 0; s < SWEEP_SAMPLES && app->running && !app->tx_active && app->sweep_running; s++) {
                    sum += radio_rssi(app);
                    furi_delay_ms(SWEEP_DWELL_MS / SWEEP_SAMPLES);
                }
                bool sample_valid = app->running && !app->tx_active && app->sweep_running;
                float avg = sum / SWEEP_SAMPLES;
                radio_idle(app);
                furi_mutex_release(app->radio_mutex);
                if(!sample_valid) break;

                if(avg > best_rssi) {
                    best_rssi = avg;
                    best_freq = freq;
                }

                app->sweep_peak_rssi = best_rssi;
                app->sweep_peak_freq = best_freq;
                app->sweep_points_done = (uint16_t)(step + 1);
                app->sweep_progress = (uint8_t)((step + 1) * 100 / total_steps);
            }

            app->sweep_running = false;
            if(best_rssi > RF_ALERT_THRESHOLD) {
                app->last_signal_freq = best_freq;
            }
            app->peak_rssi = best_rssi;

        } else if(app->rf_sub == RfSubPeak && app->peak_running) {
            uint32_t center = app->last_signal_freq;
            const RfBand* band = NULL;
            for(uint8_t i = 0; i < RF_BAND_COUNT; i++) {
                if(center >= rf_bands[i].start_hz && center <= rf_bands[i].stop_hz) {
                    band = &rf_bands[i];
                    break;
                }
            }
            if(!band) {
                app->peak_running = false;
                continue;
            }
            uint32_t start = center > PEAK_REFINE_SPAN ? center - PEAK_REFINE_SPAN : 0;
            uint32_t stop = center + PEAK_REFINE_SPAN;
            if(start < band->start_hz) start = band->start_hz;
            if(stop > band->stop_hz) stop = band->stop_hz;
            uint32_t total_steps = (stop - start) / SWEEP_STEP_FINE;

            float best_rssi = -120.0f;
            uint32_t best_freq = center;

            for(uint32_t step = 0; step < total_steps && app->running && app->peak_running; step++) {
                if(app->tx_active) break;
                uint32_t freq = start + step * SWEEP_STEP_FINE;
                furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
                if(!app->running || app->tx_active || !app->peak_running || !app->radio) {
                    furi_mutex_release(app->radio_mutex);
                    break;
                }
                radio_rx_at(app, freq);

                float sum = 0;
                for(uint8_t s = 0; s < SWEEP_SAMPLES && app->running && !app->tx_active && app->peak_running; s++) {
                    sum += radio_rssi(app);
                    furi_delay_ms(SWEEP_DWELL_MS / SWEEP_SAMPLES);
                }
                bool sample_valid = app->running && !app->tx_active && app->peak_running;
                float avg = sum / SWEEP_SAMPLES;
                radio_idle(app);
                furi_mutex_release(app->radio_mutex);
                if(!sample_valid) break;

                if(avg > best_rssi) {
                    best_rssi = avg;
                    best_freq = freq;
                }
                app->peak_fine_rssi = best_rssi;
                app->peak_fine_freq = best_freq;
                app->sweep_progress = (uint8_t)((step + 1) * 100 / total_steps);
            }

            app->peak_running = false;
            app->last_signal_freq = best_freq;
            app->peak_rssi = best_rssi;
            app->peak_fine_rssi = best_rssi;
        } else {
            furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
            radio_idle(app);
            furi_mutex_release(app->radio_mutex);
            furi_delay_ms(50);
        }
    }

    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    if(app->radio) subghz_devices_sleep(app->radio);
    furi_mutex_release(app->radio_mutex);
    return 0;
}

/* ================================================================== */
/* TX thread — bounded OOK carrier                                     */
/* ================================================================== */
static LevelDuration tx_carrier_cb(void* context) {
    App* app = context;
    if(!app->tx_started) {
        app->tx_started = true;
        return level_duration_wait();
    }
    app->tx_level = !app->tx_level;
    return level_duration_make(app->tx_level, app->tx_level ? 1000000U : 2U);
}

static int32_t tx_thread(void* ctx) {
    App* app = ctx;

    uint32_t duration_ms = app->tx_duration_s * 1000U;
    app->tx_remaining_ms = duration_ms;

    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    if(app->running && app->tx_active && app->radio) {
        app->tx_level = false;
        app->tx_started = false;
        subghz_devices_idle(app->radio);
        subghz_devices_load_preset(app->radio, FuriHalSubGhzPresetOok650Async, NULL);
        subghz_devices_set_frequency(app->radio, app->tx_freq_hz);
        if(subghz_devices_start_async_tx(app->radio, tx_carrier_cb, app)) {
            uint32_t elapsed = 0;
            while(app->running && app->tx_active && elapsed < duration_ms) {
                furi_delay_ms(50);
                elapsed += 50;
                app->tx_remaining_ms = duration_ms - elapsed;
            }
            subghz_devices_stop_async_tx(app->radio);
        }
        subghz_devices_idle(app->radio);
    }
    furi_mutex_release(app->radio_mutex);

    app->tx_active = false;
    app->tx_state = TxDisarmed;
    app->tx_remaining_ms = 0;
    return 0;
}

static void tx_thread_cleanup(App* app) {
    if(app->tx_thread && !app->tx_active) {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }
}

/* ================================================================== */
/* Feedback tick — per-tab LED + continuous Geiger audio + vibro       */
/* Each tab drives feedback from ITS signals (see specs/per-tab).      */
/* ================================================================== */
static void led_from_rssi(NotificationApp* notif, float peak, bool blink_phase) {
    if(peak >= -55.0f) {
        notification_message(notif, blink_phase ? &sequence_set_red_255 : &sequence_reset_rgb);
    } else if(peak >= -65.0f) {
        notification_message(notif, &sequence_set_red_255);
    } else if(peak >= -75.0f) {
        notification_message(notif, &sequence_solid_yellow);
    } else if(peak >= -85.0f) {
        notification_message(notif, &sequence_set_green_255);
    } else {
        notification_message(notif, &sequence_reset_rgb);
    }
}

static void led_from_gps(NotificationApp* notif, App* app, bool fresh, bool blink_phase) {
    if(!fresh || app->gps.sentences == 0) {
        notification_message(notif, &sequence_reset_rgb);
        return;
    }
    if(app->gps.has_fix && blink_phase && app->gps.sats >= 6) {
        notification_message(notif, &sequence_set_green_255);
        return;
    }
    if(app->gps.has_fix) {
        if(app->gps.sats >= 6) notification_message(notif, &sequence_set_green_255);
        else if(app->gps.sats >= 3) notification_message(notif, &sequence_solid_yellow);
        else notification_message(notif, &sequence_set_red_255);
    } else {
        notification_message(notif, blink_phase ? &sequence_solid_yellow : &sequence_reset_rgb);
    }
}

static void feedback_tick(App* app) {
    uint32_t now = furi_get_tick();
    static bool blink_phase = false;
    if((now / 250) % 2 == 0) blink_phase = true;
    else blink_phase = false;

    float peak = -120.0f;
    bool alerting = false;
    bool use_rssi_geiger = false;
    bool gps_mode = false;
    bool tx_mode = false;

    /* Target lock overrides ambient peak for Geiger */
    if(app->target_kind != TargetNone && app->target_rssi > -127) {
        peak = (float)app->target_rssi;
        alerting = (peak > RF_ALERT_THRESHOLD);
        use_rssi_geiger = true;
        led_from_rssi(app->notif, peak, blink_phase);
        /* fall through to sound/vibro with locked peak */
        goto feedback_sound;
    }

    switch(app->mode) {
    case SweepModeRF:
        peak = app->peak_rssi;
        alerting = (peak > RF_ALERT_THRESHOLD) || app->rf_alert;
        use_rssi_geiger = true;
        led_from_rssi(app->notif, peak, blink_phase);
        break;
    case SweepModeWifi:
        peak = (app->wifi_strongest > -127) ? (float)app->wifi_strongest : -120.0f;
        alerting = (peak > RF_ALERT_THRESHOLD);
        use_rssi_geiger = true;
        led_from_rssi(app->notif, peak, blink_phase);
        break;
    case SweepModeBle:
        peak = (app->ble_strongest > -127) ? (float)app->ble_strongest : -120.0f;
        alerting = (peak > RF_ALERT_THRESHOLD);
        use_rssi_geiger = true;
        led_from_rssi(app->notif, peak, blink_phase);
        break;
    case SweepModeGps: {
        gps_mode = true;
        bool fresh = app->gps_last_valid_tick > 0 &&
                     now - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
        led_from_gps(app->notif, app, fresh, blink_phase);
        alerting = fresh && app->gps.has_fix;
        /* Fix-acquire edge for sound/vibro */
        if(alerting && !app->gps_had_fix) {
            if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
            if(app->vibro_on) notification_message(app->notif, &seq_vibro_pulse);
        }
        app->gps_had_fix = alerting;
        peak = fresh ? (-100.0f + (float)app->gps.sats * 4.0f) : -120.0f;
        use_rssi_geiger = true;
        break;
    }
    case SweepModeTx:
        tx_mode = true;
        if(app->tx_state == TxTransmitting) {
            notification_message(
                app->notif, blink_phase ? &sequence_set_red_255 : &sequence_reset_rgb);
            alerting = true;
            peak = -40.0f;
            use_rssi_geiger = app->sound_on;
        } else if(app->tx_state == TxArmed) {
            notification_message(app->notif, &sequence_solid_yellow);
            alerting = true;
            peak = -70.0f;
            use_rssi_geiger = false;
        } else {
            notification_message(app->notif, &sequence_reset_rgb);
        }
        break;
    case SweepModeInfo:
    default:
        notification_message(app->notif, &sequence_reset_rgb);
        peak = -120.0f;
        use_rssi_geiger = app->sound_on; /* idle heartbeat only */
        break;
    }

feedback_sound:
    app->lock_ticks = alerting ? (app->lock_ticks + 1) : 0;

    if(app->sound_on && use_rssi_geiger) {
        uint32_t interval;
        if(gps_mode) {
            if(peak > -70.0f) interval = 200;
            else if(peak > -90.0f) interval = 500;
            else if(peak > -110.0f) interval = 1000;
            else interval = 2000;
        } else if(tx_mode && app->tx_state == TxTransmitting) {
            interval = 120;
        } else {
            if(peak > -50.0f) interval = 60;
            else if(peak > -60.0f) interval = 100;
            else if(peak > -70.0f) interval = 180;
            else if(peak > -80.0f) interval = 350;
            else if(peak > -90.0f) interval = 700;
            else if(peak > -100.0f) interval = 1200;
            else interval = 2000;
        }

        if(now - app->last_click_ms >= interval) {
            app->last_click_ms = now;
            notification_message(app->notif, &seq_geiger_click);
        }

        if(!gps_mode && app->lock_ticks == 5) {
            notification_message(app->notif, &seq_lock_tone);
        }
        if(!alerting && app->was_alerting) {
            notification_message(app->notif, &seq_sound_stop);
        }
    }

    if(app->vibro_on && !gps_mode) {
        if(alerting && !app->was_alerting) {
            notification_message(app->notif, &seq_vibro_pulse);
        }
        if(app->lock_ticks > 5 && now - app->last_vibro_ms >= 800) {
            app->last_vibro_ms = now;
            notification_message(app->notif, &seq_vibro_pulse);
        }
        if(!alerting && now - app->last_vibro_ms >= 4000) {
            app->last_vibro_ms = now;
            notification_message(app->notif, &seq_vibro_pulse);
        }
    } else if(app->vibro_on && gps_mode && app->gps.has_fix) {
        if(now - app->last_vibro_ms >= 4000) {
            app->last_vibro_ms = now;
            notification_message(app->notif, &seq_vibro_pulse);
        }
    }

    app->was_alerting = alerting;
}

/* ================================================================== */
/* Drawing: RF Survey sub-view                                         */
/* ================================================================== */
static void draw_rf_survey(Canvas* canvas, App* app) {
    /* Header */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "RF Survey");

    /* Sub-mode + radio path (EXT = BFFB SPI CC1101) */
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 52, 12,
                    app->radio_path == RadioPathExternal ? "[SURVEY EXT]" : "[SURVEY INT]");

    /* Peak RSSI (right-aligned) */
    char buf[16];
    float peak = app->peak_rssi;
    snprintf(buf, sizeof(buf), "%.0fdBm", (double)peak);
    canvas_set_font(canvas, FontSecondary);
    uint16_t w = canvas_string_width(canvas, buf);
    canvas_draw_str(canvas, 127 - (int)w, 12, buf);

    /* Bar chart (y 15..56) */
    const uint8_t bar_w = 7, bar_gap = 1, area_h = 38, base_y = 54;

    /* Threshold line */
    int th_y = base_y - (int)((RF_ALERT_THRESHOLD + 100.0f) * area_h / 70.0f);
    for(uint8_t dx = 0; dx < 128; dx += 4) {
        canvas_draw_dot(canvas, dx, th_y);
        canvas_draw_dot(canvas, dx + 1, th_y);
    }

    /* Bars */
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) {
        float r = app->rssi[i];
        int h = (int)((r + 100.0f) * area_h / 70.0f);
        if(h < 0) h = 0;
        if(h > area_h) h = area_h;
        uint8_t x = i * (bar_w + bar_gap);

        /* Baseline delta: filled if above baseline, frame if below */
        float base = app->baseline_set ? app->baseline_rssi[i] : -120.0f;
        bool above_base = app->baseline_set && (r > base + 3.0f);
        if(r > RF_ALERT_THRESHOLD || above_base) {
            canvas_draw_box(canvas, x, base_y - (h > 0 ? h : 1), bar_w, h > 0 ? (uint8_t)h : 1);
        } else if(h >= 3) {
            canvas_draw_frame(canvas, x, base_y - h, bar_w, h);
        } else if(h >= 1) {
            canvas_draw_box(canvas, x, base_y - 1, bar_w, 1);
        }
        /* baseline tick mark */
        if(app->baseline_set) {
            int bh = (int)((base + 100.0f) * area_h / 70.0f);
            if(bh < 0) bh = 0;
            if(bh > area_h) bh = area_h;
            canvas_draw_dot(canvas, x + bar_w / 2, base_y - bh);
        }
    }
    furi_mutex_release(app->mutex);

    canvas_draw_line(canvas, 0, base_y, 127, base_y);

    /* Frequency labels */
    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i += 4) {
        canvas_draw_str(canvas, i * (bar_w + bar_gap), 63, rf_labels[i]);
    }

    /* SIGNAL / lock / baseline markers */
    canvas_set_font(canvas, FontKeyboard);
    if(app->target_kind == TargetRF) {
        canvas_draw_str(canvas, 1, 22, "LOCK");
    } else if(app->baseline_set) {
        canvas_draw_str(canvas, 1, 22, "BASE");
    }
    if(app->rf_alert) {
        canvas_set_font(canvas, FontSecondary);
        const char* sig = "SIGNAL!";
        uint16_t sw = canvas_string_width(canvas, sig);
        uint8_t sx = (128 - sw) / 2;
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, sx - 2, 24, sw + 4, 12);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str(canvas, sx, 34, sig);
    }
}

/* ================================================================== */
/* Drawing: RF Band Sweep sub-view                                     */
/* ================================================================== */
static void draw_rf_sweep(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "RF Sweep");
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 52, 12,
                    app->radio_path == RadioPathExternal ? "[SWEEP EXT]" : "[SWEEP INT]");

    const RfBand* band = &rf_bands[app->sweep_band_idx];

    if(app->sweep_running) {
        /* Active sweep: progress bar + current freq + peak */
        canvas_set_font(canvas, FontSecondary);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu.%02lu MHz",
                 (unsigned long)(app->sweep_freq / 1000000),
                 (unsigned long)((app->sweep_freq % 1000000) / 10000));
        canvas_draw_str(canvas, 2, 26, buf);

        snprintf(buf, sizeof(buf), "Peak: %.0f dBm", (double)app->sweep_peak_rssi);
        canvas_draw_str(canvas, 2, 38, buf);

        /* Progress bar */
        canvas_draw_frame(canvas, 2, 48, 124, 8);
        uint8_t fill = (uint8_t)(122 * app->sweep_progress / 100);
        if(fill > 0) canvas_draw_box(canvas, 3, 49, fill, 6);

        snprintf(buf, sizeof(buf), "%d%%", app->sweep_progress);
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 108, 45, buf);
    } else {
        /* Idle: band selection + instructions */
        canvas_set_font(canvas, FontSecondary);
        char buf[32];
        snprintf(buf, sizeof(buf), "Band: %s MHz", band->label);
        canvas_draw_str(canvas, 2, 26, buf);

        if(app->sweep_peak_rssi > -120.0f) {
            snprintf(buf, sizeof(buf), "Last peak: %.0f dBm", (double)app->sweep_peak_rssi);
            canvas_draw_str(canvas, 2, 38, buf);
            snprintf(buf, sizeof(buf), "@ %lu.%02lu MHz",
                     (unsigned long)(app->sweep_peak_freq / 1000000),
                     (unsigned long)((app->sweep_peak_freq % 1000000) / 10000));
            canvas_draw_str(canvas, 2, 49, buf);
        }

        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 62, "OK=start hold LR=band");
    }
}

/* ================================================================== */
/* Drawing: RF Peak Refinement sub-view                                */
/* ================================================================== */
static void draw_rf_peak(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "RF Peak");
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 52, 12,
                    app->radio_path == RadioPathExternal ? "[PEAK EXT]" : "[PEAK INT]");

    if(app->peak_running) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f dBm", (double)app->peak_fine_rssi);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 30, buf);

        snprintf(buf, sizeof(buf), "%lu.%03lu MHz",
                 (unsigned long)(app->peak_fine_freq / 1000000),
                 (unsigned long)((app->peak_fine_freq % 1000000) / 1000));
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 44, buf);

        /* Progress */
        canvas_draw_frame(canvas, 2, 52, 124, 8);
        uint8_t fill = (uint8_t)(122 * app->sweep_progress / 100);
        if(fill > 0) canvas_draw_box(canvas, 3, 53, fill, 6);
    } else if(app->last_signal_freq > 0) {
        canvas_set_font(canvas, FontSecondary);
        char buf[32];
        snprintf(buf, sizeof(buf), "Center: %lu.%03lu MHz",
                 (unsigned long)(app->last_signal_freq / 1000000),
                 (unsigned long)((app->last_signal_freq % 1000000) / 1000));
        canvas_draw_str(canvas, 2, 28, buf);

        if(app->peak_fine_rssi > -120.0f) {
            snprintf(buf, sizeof(buf), "Refined: %.0f dBm", (double)app->peak_fine_rssi);
            canvas_draw_str(canvas, 2, 40, buf);
            snprintf(buf, sizeof(buf), "@ %lu.%03lu MHz",
                     (unsigned long)(app->peak_fine_freq / 1000000),
                     (unsigned long)((app->peak_fine_freq % 1000000) / 1000));
            canvas_draw_str(canvas, 2, 52, buf);
        }
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 63, "OK=refine B=settings");
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 30, "No peak detected.");
        canvas_draw_str(canvas, 2, 42, "Run Survey or Sweep");
        canvas_draw_str(canvas, 2, 53, "to find a signal first.");
    }
}

/* ================================================================== */
/* Drawing: WiFi tab (meter + parsed AP list)                          */
/* ================================================================== */
static void draw_wifi_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "WiFi");

    if(!app->serial) {
        canvas_draw_str(canvas, 8, 32, "No UART");
        canvas_draw_str(canvas, 8, 44, "USART unavailable");
        return;
    }

    /* State indicator */
    canvas_set_font(canvas, FontKeyboard);
    const char* state = app->marauder_state == MarauderScanning ? "scan..." :
                        app->marauder_state == MarauderDone ? "done" :
                        app->marauder_state == MarauderError ? "ERR" : "idle";
    canvas_draw_str(canvas, 90 - (int)canvas_string_width(canvas, state), 12, state);

    /* Signal meter: strongest RSSI */
    char buf[32];
    if(app->wifi_strongest > -127) {
        snprintf(buf, sizeof(buf), "%d dBm", app->wifi_strongest);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 26, buf);

        /* Meter bar (-100 to -30 range) */
        int pct = (app->wifi_strongest + 100) * 100 / 70;
        if(pct < 0) pct = 0;
        if(pct > 100) pct = 100;
        canvas_draw_frame(canvas, 2, 30, 80, 7);
        uint8_t fill = (uint8_t)(78 * pct / 100);
        if(fill > 0) canvas_draw_box(canvas, 3, 31, fill, 5);

        snprintf(buf, sizeof(buf), "%d APs", app->wifi_count);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 86, 37, buf);
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 26, "RSSI: --");
        if(app->uart_rx_tick > 0) {
            canvas_set_font(canvas, FontKeyboard);
            canvas_draw_str(canvas, 2, 36, "UART ok, parse wait");
        } else if(app->marauder_state == MarauderScanning) {
            canvas_set_font(canvas, FontKeyboard);
            canvas_draw_str(canvas, 2, 36, "sniffbeacon...");
        }
    }

    /* Freshness */
    if(app->wifi_last_scan_tick > 0) {
        uint32_t age_ms = furi_get_tick() - app->wifi_last_scan_tick;
        snprintf(buf, sizeof(buf), "%lus ago", (unsigned long)(age_ms / 1000));
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 86, 26, buf);
    }

    canvas_set_font(canvas, FontKeyboard);
    bool used[MAX_WIFI_APS] = {false};
    uint8_t shown = 0;
    while(shown < 3) {
        int best = -1;
        for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
            if(app->wifi_aps[i].valid && !used[i] &&
               (best < 0 || app->wifi_aps[i].rssi > app->wifi_aps[best].rssi)) {
                best = i;
            }
        }
        if(best < 0) break;
        used[best] = true;
        snprintf(buf, sizeof(buf), "%-12s %ddBm",
                 app->wifi_aps[best].ssid, app->wifi_aps[best].rssi);
        canvas_draw_str(canvas, 2, 43 + shown * 7, buf);
        shown++;
    }

    if(shown == 0 && app->last_uart_line[0]) {
        canvas_draw_str(canvas, 2, 50, app->last_uart_line);
    }

    if(app->marauder_state != MarauderScanning) {
        canvas_draw_str(canvas, 2, 63, "OK=scan");
    }
}

/* ================================================================== */
/* Drawing: BLE tab                                                    */
/* ================================================================== */
static void draw_ble_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "BLE");

    if(!app->serial) {
        canvas_draw_str(canvas, 8, 32, "No UART");
        canvas_draw_str(canvas, 8, 44, "USART unavailable");
        return;
    }

    canvas_set_font(canvas, FontKeyboard);
    const char* state = app->marauder_state == MarauderScanning ? "sniff..." :
                        app->marauder_state == MarauderDone ? "done" :
                        app->marauder_state == MarauderError ? "ERR" : "idle";
    canvas_draw_str(canvas, 90 - (int)canvas_string_width(canvas, state), 12, state);

    char buf[32];
    if(app->ble_strongest > -127) {
        snprintf(buf, sizeof(buf), "%d dBm", app->ble_strongest);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 26, buf);

        int pct = (app->ble_strongest + 100) * 100 / 70;
        if(pct < 0) pct = 0;
        if(pct > 100) pct = 100;
        canvas_draw_frame(canvas, 2, 30, 80, 7);
        uint8_t fill = (uint8_t)(78 * pct / 100);
        if(fill > 0) canvas_draw_box(canvas, 3, 31, fill, 5);

        snprintf(buf, sizeof(buf), "%d devs", app->ble_count);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 86, 37, buf);
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 26, "RSSI: --");
        if(app->uart_rx_tick > 0) {
            canvas_set_font(canvas, FontKeyboard);
            canvas_draw_str(canvas, 2, 36, "UART ok, parse wait");
        } else if(app->marauder_state == MarauderScanning) {
            canvas_set_font(canvas, FontKeyboard);
            canvas_draw_str(canvas, 2, 36, "sniffbt...");
        }
    }

    if(app->ble_last_scan_tick > 0) {
        uint32_t age_ms = furi_get_tick() - app->ble_last_scan_tick;
        snprintf(buf, sizeof(buf), "%lus ago", (unsigned long)(age_ms / 1000));
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 86, 26, buf);
    }

    canvas_set_font(canvas, FontKeyboard);
    bool used[MAX_BLE_DEVS] = {false};
    uint8_t shown = 0;
    while(shown < 3) {
        int best = -1;
        for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
            if(app->ble_devs[i].valid && !used[i] &&
               (best < 0 || app->ble_devs[i].rssi > app->ble_devs[best].rssi)) {
                best = i;
            }
        }
        if(best < 0) break;
        used[best] = true;
        snprintf(buf, sizeof(buf), "%-12s %ddBm",
                 app->ble_devs[best].name, app->ble_devs[best].rssi);
        canvas_draw_str(canvas, 2, 43 + shown * 7, buf);
        shown++;
    }

    if(shown == 0 && app->last_uart_line[0]) {
        canvas_draw_str(canvas, 2, 50, app->last_uart_line);
    }

    if(app->marauder_state != MarauderScanning) {
        canvas_draw_str(canvas, 2, 63, "OK=sniff");
    }
}

/* ================================================================== */
/* Drawing: GPS tab                                                    */
/* ================================================================== */
static void draw_gps_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "GPS");

    bool gps_fresh = app->gps_last_valid_tick > 0 &&
                     furi_get_tick() - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
    canvas_set_font(canvas, FontKeyboard);
    if(app->gps.has_fix && gps_fresh) {
        canvas_draw_str(canvas, 36, 12, app->gps_from_gpio ? "FIX G" : "FIX M");
    } else if(app->gps.sentences > 0 && gps_fresh) {
        canvas_draw_str(canvas, 36, 12, "NOFIX");
    } else if(app->gps.sentences > 0) {
        canvas_draw_str(canvas, 36, 12, "STALE");
    } else {
        canvas_draw_str(canvas, 36, 12, "WAIT");
    }

    char buf[40];
    canvas_set_font(canvas, FontSecondary);

    if(app->gps.sentences == 0) {
        canvas_draw_str(canvas, 2, 26, "GPIO LPUART 15/16");
        snprintf(buf, sizeof(buf), "@ %lu baud", (unsigned long)app->gps_gpio_baud);
        canvas_draw_str(canvas, 2, 38, buf);
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 50, app->gps_gpio_open ? "listening..." : "LPUART fail");
        canvas_draw_str(canvas, 2, 60, "OK=retry  MNTM:15,16");
        return;
    }

    if(gps_fresh && app->gps.has_time) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 app->gps.hour, app->gps.minute, app->gps.second);
        canvas_draw_str(canvas, 2, 24, buf);
    } else {
        canvas_draw_str(canvas, 2, 24, "--:--:--");
    }

    /* Sat quality bar (used sats, max 12) */
    uint8_t sats = app->gps.sats;
    if(sats > 12) sats = 12;
    canvas_draw_frame(canvas, 56, 16, 50, 7);
    if(sats > 0) canvas_draw_box(canvas, 57, 17, (uint8_t)(48 * sats / 12), 5);
    snprintf(buf, sizeof(buf), "%d/%d", app->gps.sats, app->gps.sats_in_view);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 108, 23, buf);

    canvas_set_font(canvas, FontSecondary);
    if(gps_fresh && app->gps.has_pos) {
        snprintf(buf, sizeof(buf), "%.5f %.5f",
                 (double)app->gps.latitude, (double)app->gps.longitude);
        canvas_draw_str(canvas, 2, 36, buf);
    } else {
        canvas_draw_str(canvas, 2, 36, "No position yet");
    }

    /* Speed (kts→km/h) and course — fields already parsed from RMC */
    if(gps_fresh && app->gps.has_fix) {
        float kmh = app->gps.speed_kts * 1.852f;
        snprintf(buf, sizeof(buf), "%.1fkm/h %03.0fdeg",
                 (double)kmh, (double)app->gps.course);
        canvas_draw_str(canvas, 2, 48, buf);
    } else {
        canvas_draw_str(canvas, 2, 48, "spd/crs --");
    }

    /* Mark distance */
    canvas_set_font(canvas, FontKeyboard);
    if(app->gps_mark_set && gps_fresh && app->gps.has_pos) {
        float dist = geo_distance_m(
            app->gps_mark_lat, app->gps_mark_lon,
            app->gps.latitude, app->gps.longitude);
        if(dist >= 1000.0f) {
            snprintf(buf, sizeof(buf), "mark %.2fkm", (double)(dist / 1000.0f));
        } else {
            snprintf(buf, sizeof(buf), "mark %.0fm", (double)dist);
        }
        canvas_draw_str(canvas, 2, 60, buf);
    } else if(app->gps_mark_set) {
        canvas_draw_str(canvas, 2, 60, "mark set (no pos)");
    } else {
        canvas_draw_str(canvas, 2, 60, "OK=mark");
    }

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->gps.sentences);
    canvas_draw_str(canvas, 110, 60, buf);
}

/* ================================================================== */
/* Drawing: TX tab (safety-gated, multi-step arming)                   */
/* ================================================================== */
static void draw_tx_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "TX Control");

    char buf[40];

    if(app->tx_state == TxDisarmed) {
        /* DISARMED — show safety contract */
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 28, "DISARMED");

        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 38, "Own property / licensed only.");
        canvas_draw_str(canvas, 2, 47, "Radiates RF energy.");

        snprintf(buf, sizeof(buf), "Freq: %lu.%03lu MHz Dur:%ds",
                 (unsigned long)(app->tx_freq_hz / 1000000),
                 (unsigned long)((app->tx_freq_hz % 1000000) / 1000),
                 app->tx_duration_s);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 60, buf);

        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 88, 12, "OK=arm");

    } else if(app->tx_state == TxArmed) {
        /* ARMED — inverse video warning, frequency selectable */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 14, 128, 16);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 8, 27, "!! ARMED !!");
        canvas_set_color(canvas, ColorBlack);

        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "%lu.%03lu MHz %ds max",
                 (unsigned long)(app->tx_freq_hz / 1000000),
                 (unsigned long)((app->tx_freq_hz % 1000000) / 1000),
                 app->tx_duration_s);
        canvas_draw_str(canvas, 2, 44, buf);

        if(app->last_signal_freq > 0) {
            snprintf(buf, sizeof(buf), "Signal: %lu.%02lu MHz",
                     (unsigned long)(app->last_signal_freq / 1000000),
                     (unsigned long)((app->last_signal_freq % 1000000) / 10000));
            canvas_draw_str(canvas, 2, 55, buf);
        }

        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 63, "LongOK=TX Up/Dn=freq B=disarm");

    } else if(app->tx_state == TxTransmitting) {
        /* TRANSMITTING — countdown, inverse video */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, 128, 64);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 12, 20, "TRANSMITTING");

        char fbuf[32];
        snprintf(fbuf, sizeof(fbuf), "%lu.%03lu MHz",
                 (unsigned long)(app->tx_freq_hz / 1000000),
                 (unsigned long)((app->tx_freq_hz % 1000000) / 1000));
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 30, 38, fbuf);

        snprintf(fbuf, sizeof(fbuf), "%lu.%lus remaining",
                 (unsigned long)(app->tx_remaining_ms / 1000),
                 (unsigned long)((app->tx_remaining_ms % 1000) / 100));
        canvas_draw_str(canvas, 24, 52, fbuf);
        canvas_set_color(canvas, ColorBlack);
    }
}

/* ================================================================== */
/* Drawing: Info tab (live capability card)                            */
/* ================================================================== */
static void draw_info_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontKeyboard);
    char buf[48];

    snprintf(buf, sizeof(buf), "RF:%s band:%s",
             app->radio_path == RadioPathExternal ? "EXT" :
             app->radio_path == RadioPathInternal ? "INT" : "?",
             app->ext_band == ExtBand400 ? "400" :
             app->ext_band == ExtBand900 ? "900" : "AUTO");
    canvas_draw_str(canvas, 2, 10, buf);

    snprintf(buf, sizeof(buf), "UART:%s GPS:%s@%lu",
             app->serial ? "ok" : "no",
             app->gps_from_gpio ? "G" : (app->gps.sentences ? "M" : "-"),
             (unsigned long)(app->gps_gpio_baud ? app->gps_gpio_baud : 9600));
    canvas_draw_str(canvas, 2, 19, buf);

    snprintf(buf, sizeof(buf), "Log:%s Base:%s Lock:%s",
             app->session_log_on ? "ON" : "off",
             app->baseline_set ? "Y" : "n",
             app->target_kind == TargetNone ? "-" :
             app->target_kind == TargetRF ? "RF" :
             app->target_kind == TargetWifi ? "Wi" : "BT");
    canvas_draw_str(canvas, 2, 28, buf);

    if(app->target_kind != TargetNone) {
        char tid[12];
        strncpy(tid, app->target_id, 11);
        tid[11] = '\0';
        snprintf(buf, sizeof(buf), "T:%s %ddBm", tid, (int)app->target_rssi);
        canvas_draw_str(canvas, 2, 37, buf);
    } else {
        canvas_draw_str(canvas, 2, 37, "LongOK=lock peak/AP");
    }

    snprintf(buf, sizeof(buf), "dump:%u lines", app->dump_count);
    canvas_draw_str(canvas, 2, 46, buf);

    if(app->last_uart_line[0]) {
        canvas_draw_str(canvas, 2, 55, app->last_uart_line);
    } else {
        canvas_draw_str(canvas, 2, 55, "UART: (no lines yet)");
    }

    canvas_draw_str(canvas, 2, 63, "SW: bot=ESP32 top=400/900");
}

/* ================================================================== */
/* Drawing: Settings overlay                                           */
/* ================================================================== */
static void draw_settings(Canvas* canvas, App* app) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, 128, 64);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 1, 1, 126, 62);

    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 4, 9, "Settings");

    const char* labels[SET_COUNT] = {
        "Sound", "Vibro", "Rescan", "Log", "ExtBand", "Baseline", "Dump", "TXDur",
    };

    /* Show 5 rows, scroll with selection */
    int start = 0;
    if(app->settings_sel > 3) start = (int)app->settings_sel - 3;
    if(start > SET_COUNT - 5) start = SET_COUNT - 5;
    if(start < 0) start = 0;

    for(int row = 0; row < 5 && start + row < SET_COUNT; row++) {
        int i = start + row;
        uint8_t y = (uint8_t)(18 + row * 9);
        if(i == (int)app->settings_sel) {
            canvas_draw_box(canvas, 2, y - 7, 124, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, labels[i]);

        char val[16];
        if(i == SET_SOUND) snprintf(val, sizeof(val), "%s", app->sound_on ? "ON" : "off");
        else if(i == SET_VIBRO) snprintf(val, sizeof(val), "%s", app->vibro_on ? "ON" : "off");
        else if(i == SET_RESCAN) snprintf(val, sizeof(val), "%s", app->auto_rescan ? "ON" : "off");
        else if(i == SET_LOG) snprintf(val, sizeof(val), "%s", app->session_log_on ? "ON" : "off");
        else if(i == SET_EXTBAND)
            snprintf(
                val,
                sizeof(val),
                "%s",
                app->ext_band == ExtBand400 ? "400" :
                app->ext_band == ExtBand900 ? "900" : "AUTO");
        else if(i == SET_BASELINE)
            snprintf(val, sizeof(val), "%s", app->baseline_set ? "set" : "OK=");
        else if(i == SET_DUMP) snprintf(val, sizeof(val), "%u", app->dump_count);
        else if(i == SET_TXDUR) snprintf(val, sizeof(val), "%ds", app->tx_duration_s);
        else val[0] = '\0';

        uint16_t w = canvas_string_width(canvas, val);
        canvas_draw_str(canvas, 124 - w, y, val);
        if(i == (int)app->settings_sel) canvas_set_color(canvas, ColorBlack);
    }
}

/* ================================================================== */
/* Main draw callback                                                  */
/* ================================================================== */
static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    canvas_clear(canvas);

    switch(app->mode) {
    case SweepModeRF:
        if(app->rf_sub == RfSubSurvey) draw_rf_survey(canvas, app);
        else if(app->rf_sub == RfSubSweep) draw_rf_sweep(canvas, app);
        else draw_rf_peak(canvas, app);
        break;
    case SweepModeWifi: draw_wifi_tab(canvas, app); break;
    case SweepModeBle:  draw_ble_tab(canvas, app);  break;
    case SweepModeGps:  draw_gps_tab(canvas, app);  break;
    case SweepModeTx:   draw_tx_tab(canvas, app);   break;
    case SweepModeInfo: draw_info_tab(canvas, app); break;
    default: break;
    }

    /* Tab strip (y 0..3) */
    static const char* tab_labels[] = {"RF", "Wi", "BT", "GPS", "TX", "i"};
    canvas_draw_line(canvas, 0, 0, 127, 0);
    for(int t = 0; t < SweepModeCount; t++) {
        uint8_t x = 1 + t * 21;
        if(t == (int)app->mode) {
            canvas_draw_box(canvas, x, 1, 20, 3);
        } else {
            canvas_draw_frame(canvas, x, 1, 20, 3);
        }
    }
    UNUSED(tab_labels);

    /* RX/TX status (top-right, small) */
    canvas_set_font(canvas, FontKeyboard);
    if(app->tx_active) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 112, 5, 15, 8);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 114, 12, "TX");
        canvas_set_color(canvas, ColorBlack);
    }

    /* Sound/vibro indicators (only non-RF, non-TX tabs) */
    if(app->mode != SweepModeRF && app->mode != SweepModeTx) {
        canvas_draw_str(canvas, 92, 12, app->sound_on ? "S" : "s");
        canvas_draw_str(canvas, 99, 12, app->vibro_on ? "V" : "v");
    }

    /* Settings overlay (highest priority) */
    if(app->settings_active) {
        draw_settings(canvas, app);
    }
}

/* ================================================================== */
/* Input handler                                                       */
/* ================================================================== */
static void input_cb(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, 0);
}

/* ================================================================== */
/* App entry                                                           */
/* ================================================================== */
int32_t room_sweep_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->radio_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->running = true;
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) app->rssi[i] = -120.0f;
    app->mode = SweepModeRF;
    app->rf_sub = RfSubSurvey;
    app->sound_on = false;
    app->vibro_on = false;
    app->peak_rssi = -120.0f;
    app->sweep_peak_rssi = -120.0f;
    app->peak_fine_rssi = -120.0f;
    app->wifi_strongest = -127;
    app->ble_strongest = -127;
    app->auto_rescan = true;
    app->session_log_on = false;
    app->baseline_set = false;
    app->target_kind = TargetNone;
    app->target_rssi = -127;
    app->ext_band = ExtBandAuto;
    app->dump_head = 0;
    app->dump_count = 0;
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) app->baseline_rssi[i] = -120.0f;

    /* TX defaults */
    app->tx_state = TxDisarmed;
    app->tx_freq_hz = 433920000;
    app->tx_freq_idx = 0;
    app->tx_duration_s = TX_DEFAULT_DURATION;
    app->tx_active = false;
    app->tx_thread = NULL;

    /* GPS init */
    nmea_init(&app->gps);
    app->gps_active = false;
    app->gps_last_valid_tick = 0;
    app->gps_last_request_tick = 0;
    app->gps_mark_set = false;
    app->gps_had_fix = false;

    /* Notification service */
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    /* SubGHz: BFFB external CC1101 if present, else internal */
    radio_open(app);

    /* UART (BFFB Marauder) */
    marauder_open(app);

    /* RF sweep thread */
    app->rf_thread = furi_thread_alloc_ex("RoomSweepRF", 2048, rf_sweep_thread, app);
    furi_thread_start(app->rf_thread);

    /* GUI */
    FuriMessageQueue* input_queue = furi_message_queue_alloc(4, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_cb, app);
    view_port_input_callback_set(view_port, input_cb, input_queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    /* Main loop */
    InputEvent event;
    while(app->running) {
        tx_thread_cleanup(app);
        if(furi_message_queue_get(input_queue, &event, 100) != FuriStatusOk) {
            app->tick_count++;
            feedback_tick(app);
            process_uart_lines(app);

            if(app->serial && (app->mode == SweepModeWifi || app->mode == SweepModeBle)) {
                uint32_t now = furi_get_tick();
                uint8_t result_count = app->mode == SweepModeWifi ?
                    app->wifi_count : app->ble_count;
                /* Timeout from scan start only; zero results required for ERR.
                 * BLE dedup silence after first sightings is NOT an error. */
                if(marauder_scan_should_error(
                       app->marauder_state == MarauderScanning,
                       now,
                       app->last_rescan_tick,
                       MARAUDER_SCAN_TIMEOUT_MS,
                       result_count)) {
                    marauder_stop_scan(app);
                    app->marauder_state = MarauderError;
                    app->last_rescan_tick = now;
                }
                /* Do NOT restart every 5s while Scanning — that wiped results.
                 * Only auto-start when idle/error. */
                if(app->auto_rescan &&
                   (app->marauder_state == MarauderIdle ||
                    app->marauder_state == MarauderError) &&
                   now - app->last_rescan_tick >= RESCAN_INTERVAL_MS) {
                    marauder_start_for_mode(app);
                }
            }

            /* GPS: GPIO baud cycle, then Marauder nmea fallback */
            if(app->mode == SweepModeGps && app->gps_active) {
                uint32_t now = furi_get_tick();
                bool fresh = app->gps_last_valid_tick > 0 &&
                             now - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
                if(!fresh && now - app->gps_last_request_tick >= 5000) {
                    app->gps_last_request_tick = now;
                    if(app->gps.sentences == 0) {
                        /* Try alternate GPIO baud, then Marauder stream */
                        if(app->gps_gpio_baud == GPS_GPIO_BAUD_PRIMARY) {
                            gps_gpio_open(app, GPS_GPIO_BAUD_ALT);
                        } else if(app->gps_gpio_baud == GPS_GPIO_BAUD_ALT) {
                            gps_gpio_open(app, GPS_GPIO_BAUD_PRIMARY);
                            if(app->serial) gps_marauder_stream(app);
                        } else {
                            gps_gpio_open(app, GPS_GPIO_BAUD_PRIMARY);
                        }
                    } else if(app->serial) {
                        gps_marauder_poll(app);
                    }
                }
            }

            view_port_update(view_port);
            continue;
        }
        if(event.type != InputTypeShort && event.type != InputTypeLong) continue;

        if(event.key == InputKeyBack) {
            RoomSweepBackAction action = room_sweep_back_action(
                app->settings_active,
                app->mode == SweepModeTx && app->tx_state != TxDisarmed,
                event.type == InputTypeLong);
            if(action == RoomSweepBackExit) {
                app->running = false;
                break;
            }
            if(action == RoomSweepBackCloseSettings) {
                app->settings_active = false;
            } else if(action == RoomSweepBackDisarm) {
                app->tx_active = false;
                app->tx_state = TxDisarmed;
            } else {
                app->settings_active = true;
                app->settings_sel = 0;
            }
            continue;
        }

        /* --- Settings overlay input --- */
        if(app->settings_active) {
            if(event.key == InputKeyUp) {
                app->settings_sel = (app->settings_sel + SET_COUNT - 1) % SET_COUNT;
            } else if(event.key == InputKeyDown) {
                app->settings_sel = (app->settings_sel + 1) % SET_COUNT;
            } else if(event.key == InputKeyOk) {
                if(app->settings_sel == SET_SOUND) {
                    app->sound_on = !app->sound_on;
                    if(app->sound_on) notification_message(app->notif, &seq_test_beep);
                } else if(app->settings_sel == SET_VIBRO) {
                    app->vibro_on = !app->vibro_on;
                    if(app->vibro_on) notification_message(app->notif, &seq_test_vibro);
                } else if(app->settings_sel == SET_RESCAN) {
                    app->auto_rescan = !app->auto_rescan;
                } else if(app->settings_sel == SET_LOG) {
                    app->session_log_on = !app->session_log_on;
                    if(app->session_log_on) {
                        if(!app->storage) app->storage = furi_record_open(RECORD_STORAGE);
                        if(session_log_begin(app->storage)) {
                            notification_message(app->notif, &seq_test_beep);
                        } else {
                            app->session_log_on = false;
                        }
                    } else {
                        session_log_end(app->storage);
                    }
                } else if(app->settings_sel == SET_EXTBAND) {
                    app->ext_band = (ExtBandPref)((app->ext_band + 1) % 3);
                    if(app->radio_path == RadioPathExternal && app->ext_band != ExtBandAuto) {
                        app->sweep_band_idx = rf_default_sweep_band(app);
                    }
                } else if(app->settings_sel == SET_BASELINE) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) {
                        app->baseline_rssi[i] = app->rssi[i];
                    }
                    furi_mutex_release(app->mutex);
                    app->baseline_set = true;
                    notification_message(app->notif, &seq_test_beep);
                } else if(app->settings_sel == SET_DUMP) {
                    if(!app->storage) app->storage = furi_record_open(RECORD_STORAGE);
                    const char* lines[16];
                    uint8_t n = app->dump_count;
                    if(n > 16) n = 16;
                    /* oldest first */
                    uint8_t start = (app->dump_head + 16 - n) % 16;
                    for(uint8_t i = 0; i < n; i++) {
                        lines[i] = app->dump_lines[(start + i) % 16];
                    }
                    if(session_log_write_dump(app->storage, lines, n)) {
                        notification_message(app->notif, &seq_test_beep);
                    }
                } else if(app->settings_sel == SET_TXDUR) {
                    app->tx_duration_s = (app->tx_duration_s % TX_MAX_DURATION_S) + 1;
                }
            }
            continue;
        }

        /* --- TX tab: safety-critical input handling --- */
        if(app->mode == SweepModeTx) {
            if(event.key == InputKeyOk) {
                if(event.type == InputTypeShort && app->tx_state == TxDisarmed) {
                    app->tx_state = TxArmed;
                    notification_message(app->notif, &seq_tx_alert);
                } else if(event.type == InputTypeLong && app->tx_state == TxArmed && !app->tx_active) {
                    /* LONG OK while armed: TRANSMIT */
                    app->tx_state = TxTransmitting;
                    app->tx_active = true;
                    tx_thread_cleanup(app);
                    if(app->tx_thread == NULL) {
                        app->tx_thread = furi_thread_alloc_ex("RoomSweepTX", 2048, tx_thread, app);
                    }
                    furi_thread_start(app->tx_thread);
                }
            }
            if(event.key == InputKeyUp && app->tx_state == TxArmed) {
                app->tx_freq_idx = (app->tx_freq_idx + TX_FREQ_PRESET_COUNT - 1) % TX_FREQ_PRESET_COUNT;
                app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
            }
            if(event.key == InputKeyDown && app->tx_state == TxArmed) {
                app->tx_freq_idx = (app->tx_freq_idx + 1) % TX_FREQ_PRESET_COUNT;
                app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
            }
            if(event.key == InputKeyLeft || event.key == InputKeyRight) {
                app->tx_active = false;
                app->tx_state = TxDisarmed;
                if(app->serial) {
                    marauder_stop_scan(app);
                    marauder_reset_results(app);
                    clear_uart_lines(app);
                    app->last_uart_line[0] = '\0';
                    if(app->marauder_state != MarauderNoDevice)
                        app->marauder_state = MarauderIdle;
                }
                app->mode = (app->mode == 0) ? (SweepMode)(SweepModeCount - 1)
                                             : (SweepMode)(app->mode - 1);
                if(event.key == InputKeyRight) {
                    app->mode = (SweepMode)((app->mode + 2) % SweepModeCount);
                }
                tx_preload_detected_frequency(app);
                update_gps_mode(app);
                marauder_start_for_mode(app);
            }
            continue;
        }

        /* --- General input (non-TX tabs) --- */
        if(event.key == InputKeyLeft || event.key == InputKeyRight) {
            bool change_band = app->mode == SweepModeRF &&
                               app->rf_sub == RfSubSweep &&
                               !app->sweep_running && event.type == InputTypeLong;
            if(change_band) {
                if(event.key == InputKeyLeft) {
                    app->sweep_band_idx =
                        (app->sweep_band_idx + RF_BAND_COUNT - 1) % RF_BAND_COUNT;
                } else {
                    app->sweep_band_idx = (app->sweep_band_idx + 1) % RF_BAND_COUNT;
                }
            } else {
                if(event.key == InputKeyLeft) {
                    app->mode = (app->mode == 0) ? (SweepMode)(SweepModeCount - 1)
                                                 : (SweepMode)(app->mode - 1);
                } else {
                    app->mode = (SweepMode)((app->mode + 1) % SweepModeCount);
                }
                if(app->serial) {
                    marauder_stop_scan(app);
                    marauder_reset_results(app);
                    clear_uart_lines(app);
                    app->last_uart_line[0] = '\0';
                    if(app->marauder_state != MarauderNoDevice)
                        app->marauder_state = MarauderIdle;
                }
                tx_preload_detected_frequency(app);
                update_gps_mode(app);
                /* Entering WiFi/BLE starts Marauder immediately (no wait for OK). */
                marauder_start_for_mode(app);
            }
        }

        /* --- RF tab: Up/Down cycles sub-modes, OK triggers actions --- */
        if(app->mode == SweepModeRF) {
            if(event.key == InputKeyUp) {
                app->rf_sub = (app->rf_sub == 0) ? (RfSubMode)(RfSubCount - 1)
                                                 : (RfSubMode)(app->rf_sub - 1);
                app->sweep_running = false;
                app->peak_running = false;
            }
            if(event.key == InputKeyDown) {
                app->rf_sub = (RfSubMode)((app->rf_sub + 1) % RfSubCount);
                app->sweep_running = false;
                app->peak_running = false;
            }
            if(event.key == InputKeyOk) {
                if(app->rf_sub == RfSubSweep && !app->sweep_running) {
                    /* Start band sweep */
                    app->sweep_running = true;
                    app->sweep_peak_rssi = -120.0f;
                    app->sweep_progress = 0;
                } else if(app->rf_sub == RfSubSweep) {
                    /* Cancel running sweep */
                    app->sweep_running = false;
                } else if(app->rf_sub == RfSubPeak && !app->peak_running) {
                    /* Start peak refinement */
                    if(app->last_signal_freq > 0) {
                        app->peak_running = true;
                        app->peak_fine_rssi = -120.0f;
                        app->sweep_progress = 0;
                    }
                }
            }
        }

        /* --- WiFi/BLE: short OK restarts scan; long OK locks strongest --- */
        if(app->mode == SweepModeWifi && app->serial) {
            if(event.key == InputKeyOk && event.type == InputTypeShort) {
                marauder_start_for_mode(app);
            } else if(event.key == InputKeyOk && event.type == InputTypeLong) {
                if(app->target_kind == TargetWifi) {
                    app->target_kind = TargetNone;
                    app->target_id[0] = '\0';
                } else if(app->wifi_count > 0) {
                    int best = -1;
                    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
                        if(app->wifi_aps[i].valid &&
                           (best < 0 || app->wifi_aps[i].rssi > app->wifi_aps[best].rssi))
                            best = (int)i;
                    }
                    if(best >= 0) {
                        app->target_kind = TargetWifi;
                        strncpy(app->target_id, app->wifi_aps[best].ssid, 32);
                        app->target_id[32] = '\0';
                        app->target_rssi = app->wifi_aps[best].rssi;
                        app->target_freq_hz = 0;
                        if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
                    }
                }
            }
        }
        if(app->mode == SweepModeBle && app->serial) {
            if(event.key == InputKeyOk && event.type == InputTypeShort) {
                marauder_start_for_mode(app);
            } else if(event.key == InputKeyOk && event.type == InputTypeLong) {
                if(app->target_kind == TargetBle) {
                    app->target_kind = TargetNone;
                    app->target_id[0] = '\0';
                } else if(app->ble_count > 0) {
                    int best = -1;
                    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
                        if(app->ble_devs[i].valid &&
                           (best < 0 || app->ble_devs[i].rssi > app->ble_devs[best].rssi))
                            best = (int)i;
                    }
                    if(best >= 0) {
                        app->target_kind = TargetBle;
                        strncpy(app->target_id, app->ble_devs[best].name, 32);
                        app->target_id[32] = '\0';
                        app->target_rssi = app->ble_devs[best].rssi;
                        app->target_freq_hz = 0;
                        if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
                    }
                }
            }
        }

        /* RF long-OK: lock peak frequency for Geiger */
        if(app->mode == SweepModeRF && event.key == InputKeyOk && event.type == InputTypeLong) {
            if(app->target_kind == TargetRF) {
                app->target_kind = TargetNone;
                app->target_freq_hz = 0;
            } else if(app->last_signal_freq > 0 || app->peak_rssi > RF_ALERT_THRESHOLD) {
                app->target_kind = TargetRF;
                app->target_freq_hz = app->last_signal_freq ? app->last_signal_freq :
                                                             rf_channels[app->peak_ch];
                snprintf(app->target_id, sizeof(app->target_id), "%lu",
                         (unsigned long)(app->target_freq_hz / 1000000));
                app->target_rssi = (int8_t)app->peak_rssi;
                if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
            }
        }

        /* --- GPS: OK sets/clears mark, or re-opens GPIO / Marauder --- */
        if(event.key == InputKeyOk && app->mode == SweepModeGps) {
            bool gps_fresh = app->gps_last_valid_tick > 0 &&
                             furi_get_tick() - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
            if(app->gps.sentences == 0 || !gps_fresh) {
                nmea_init(&app->gps);
                app->gps_last_valid_tick = 0;
                /* Cycle baud: 9600 → 115200 → 9600+marauder */
                if(app->gps_gpio_baud != GPS_GPIO_BAUD_ALT) {
                    gps_gpio_open(app, GPS_GPIO_BAUD_ALT);
                } else {
                    gps_gpio_open(app, GPS_GPIO_BAUD_PRIMARY);
                    if(app->serial) gps_marauder_stream(app);
                }
                app->gps_last_request_tick = furi_get_tick();
            } else if(gps_fresh && app->gps.has_pos) {
                if(app->gps_mark_set) {
                    app->gps_mark_set = false;
                } else {
                    app->gps_mark_set = true;
                    app->gps_mark_lat = app->gps.latitude;
                    app->gps_mark_lon = app->gps.longitude;
                    if(app->sound_on) notification_message(app->notif, &seq_test_beep);
                    if(app->vibro_on) notification_message(app->notif, &seq_test_vibro);
                }
            }
        }

        /* --- Up/Down toggles sound/vibro (non-RF, non-TX) --- */
        if(app->mode != SweepModeRF && app->mode != SweepModeTx) {
            if(event.key == InputKeyUp) {
                app->sound_on = !app->sound_on;
                if(app->sound_on) notification_message(app->notif, &seq_test_beep);
            }
            if(event.key == InputKeyDown) {
                app->vibro_on = !app->vibro_on;
                if(app->vibro_on) notification_message(app->notif, &seq_test_vibro);
            }
        }

        app->tick_count++;
        feedback_tick(app);
        view_port_update(view_port);
    }

    app->running = false;
    app->tx_active = false;
    app->tx_state = TxDisarmed;

    /* Stop Marauder + GPIO GPS UARTs */
    if(app->serial) {
        marauder_send(app, "stopscan");
    }
    gps_gpio_close(app);
    marauder_close(app);

    tx_thread_cleanup(app);

    furi_thread_join(app->rf_thread);
    furi_thread_free(app->rf_thread);

    radio_close(app);

    if(app->session_log_on) session_log_end(app->storage);
    if(app->storage) furi_record_close(RECORD_STORAGE);

    notification_message(app->notif, &sequence_reset_rgb);
    notification_message(app->notif, &sequence_reset_sound);
    notification_message(app->notif, &sequence_reset_vibro);
    furi_record_close(RECORD_NOTIFICATION);

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(input_queue);
    furi_mutex_free(app->mutex);
    furi_mutex_free(app->radio_mutex);
    free(app);
    return 0;
}

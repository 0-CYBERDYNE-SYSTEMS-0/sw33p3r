/* Room Sweep v3.0 — multi-tab wireless assessment tool
 * Tabs: RF (survey/sweep/peak) | WiFi | BLE | GPS | TX | Info
 * TX is a dedicated safety-gated tab. Sound/vibro are functional.
 * All API calls verified against Momentum mntm-012 API 87.1.
 */
#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <furi_hal_subghz.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_types.h>
#include <furi_hal_serial_control.h>
#include <expansion/expansion.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <notification/notification_messages_notes.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/toolbox/level_duration.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "room_sweep.h"
#include "nmea.h"

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

    /* Marauder UART */
    MarauderState marauder_state;
    FuriHalSerialHandle* serial;
    char lines[MARAUDER_MAX_LINES][MARAUDER_LINE_MAX];
    uint8_t line_count;
    char line_buf[MARAUDER_LINE_MAX];
    uint8_t line_pos;
    volatile bool uart_rx_flag;

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

    /* Notification / feedback */
    NotificationApp* notif;
    bool sound_on;
    bool vibro_on;
    uint32_t tick_count;
    uint32_t last_click_ms;
    uint32_t last_vibro_ms;
    bool was_alerting;
    uint8_t lock_ticks;

    /* GPS */
    GpsFix gps;
    volatile bool gps_active;

    /* TX (dedicated tab, safety-gated) */
    TxState tx_state;
    volatile bool tx_active;
    FuriThread* tx_thread;
    volatile uint32_t tx_freq_hz;
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
static const char* tx_freq_labels[TX_FREQ_PRESET_COUNT] = {
    "433.92", "868.35", "915.00", "315.00", "390.00", "418.00",
};

/* Settings menu items */
enum {
    SET_SOUND = 0,
    SET_VIBRO,
    SET_RESCAN,
    SET_TXDUR,
    SET_COUNT,
};

#define RESCAN_INTERVAL_MS 5000
#define TX_MAX_DURATION_S  10
#define TX_DEFAULT_DURATION 3

/* ================================================================== */
/* UART ISR callback                                                   */
/* ================================================================== */
static void uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(handle);
    App* app = ctx;
    if(event != FuriHalSerialRxEventData) return;

    uint8_t byte = furi_hal_serial_async_rx(app->serial);

    if(app->gps_active) {
        nmea_feed(&app->gps, (char)byte);
    }

    if(byte == '\n' || byte == '\r') {
        if(app->line_pos > 0) {
            app->line_buf[app->line_pos] = '\0';
            for(int i = MARAUDER_MAX_LINES - 1; i > 0; i--) {
                strncpy(app->lines[i], app->lines[i - 1], MARAUDER_LINE_MAX - 1);
            }
            strncpy(app->lines[0], app->line_buf, MARAUDER_LINE_MAX - 1);
            if(app->line_count < MARAUDER_MAX_LINES) app->line_count++;
            app->line_pos = 0;
            app->uart_rx_flag = true;
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
        return false;
    }

    furi_hal_serial_init(app->serial, MARAUDER_BAUD);
    furi_hal_serial_async_rx_start(app->serial, uart_rx_cb, app, false);
    app->marauder_state = MarauderIdle;
    app->line_count = 0;
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

static void marauder_send(App* app, const char* cmd) {
    if(!app->serial) return;
    furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx(app->serial, (const uint8_t*)"\r\n", 2);
}

/* ================================================================== */
/* Marauder line parser — extract RSSI/identity from UART lines        */
/* Actual Marauder scanap format:                                      */
/*   "-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00"           */
/* Actual Marauder sniffbt format:                                     */
/*   "-60 Device: DeviceName"                                          */
/* Also handles generic "RSSI: -45" fallback for other firmware.       */
/* Lines starting with '#' are command echoes — skip.                  */
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

/* Parse a WiFi AP result line into the AP table.
 * Format: "-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: Name 00 00" */
static void parse_wifi_line(App* app, const char* line) {
    if(line[0] == '#') return; /* command echo */
    if(line[0] == '>') return; /* prompt */

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
            return;
        }
    }
    if(rssi_val > 0 || rssi_val < -120) return;

    /* Must have ESSID or Ch: to be a WiFi AP line */
    if(!strstr(line, "ESSID") && !strstr(line, "Ch:") && !strstr(line, "essid")) {
        return;
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
    const char* mac_p = strstr(line, ":");
    if(mac_p && mac_p > line + 2) {
        /* Back up to find start of MAC (17 chars: XX:XX:XX:XX:XX:XX) */
        const char* start = mac_p - 2;
        while(start > line && *(start-1) != ' ') start--;
        size_t len = 0;
        const char* m = start;
        while(*m && *m != ' ' && len < 17) { bssid[len++] = *m++; }
        bssid[len] = '\0';
        if(len < 11) bssid[0] = '\0'; /* not a valid MAC */
    }

    /* Update existing or add new */
    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
        if(app->wifi_aps[i].valid &&
           (strcmp(app->wifi_aps[i].ssid, ssid) == 0 ||
            (bssid[0] && strcmp(app->wifi_aps[i].bssid, bssid) == 0))) {
            app->wifi_aps[i].rssi = (int8_t)rssi_val;
            app->wifi_aps[i].channel = (uint8_t)ch_val;
            app->wifi_aps[i].last_seen = now;
            return;
        }
    }
    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
        if(!app->wifi_aps[i].valid) {
            app->wifi_aps[i].valid = true;
            strncpy(app->wifi_aps[i].ssid, ssid, 32);
            app->wifi_aps[i].rssi = (int8_t)rssi_val;
            app->wifi_aps[i].channel = (uint8_t)ch_val;
            strncpy(app->wifi_aps[i].bssid, bssid, 17);
            app->wifi_aps[i].last_seen = now;
            app->wifi_count++;
            return;
        }
    }
}

/* Parse a BLE device result line.
 * Format: "-60 Device: DeviceName" */
static void parse_ble_line(App* app, const char* line) {
    if(line[0] == '#') return;
    if(line[0] == '>') return;

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
            return;
        }
    }
    if(rssi_val > 0 || rssi_val < -120) return;

    /* Must have "Device:" or "Name:" to be a BLE line */
    if(!strstr(line, "Device") && !strstr(line, "Name") && !strstr(line, "name")) {
        return;
    }

    char name[33] = {0};
    parse_str_after(line, "Device:", name, sizeof(name));
    if(name[0] == '\0') parse_str_after(line, "Device", name, sizeof(name));
    if(name[0] == '\0') parse_str_after(line, "Name", name, sizeof(name));
    if(name[0] == '\0') snprintf(name, sizeof(name), "BLE_%d", app->ble_count);

    char mac[18] = {0};
    parse_str_after(line, "MAC", mac, sizeof(mac));

    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
        if(app->ble_devs[i].valid &&
           (strcmp(app->ble_devs[i].name, name) == 0 ||
            (mac[0] && strcmp(app->ble_devs[i].mac, mac) == 0))) {
            app->ble_devs[i].rssi = (int8_t)rssi_val;
            app->ble_devs[i].last_seen = now;
            return;
        }
    }
    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
        if(!app->ble_devs[i].valid) {
            app->ble_devs[i].valid = true;
            strncpy(app->ble_devs[i].name, name, 32);
            app->ble_devs[i].rssi = (int8_t)rssi_val;
            strncpy(app->ble_devs[i].mac, mac, 17);
            app->ble_devs[i].last_seen = now;
            app->ble_count++;
            return;
        }
    }
}

/* Process new UART lines — route to appropriate parser.
 * Marauder streams results continuously until stopscan — no "done" marker.
 * Lines starting with '#' are command echoes; '> ' is the prompt. */
static void process_uart_lines(App* app) {
    if(!app->uart_rx_flag) return;
    app->uart_rx_flag = false;

    const char* latest = app->lines[0];

    /* Skip command echoes and prompts */
    if(latest[0] == '#' || latest[0] == '>') return;

    /* Error detection */
    if(strstr(latest, "not supported") || strstr(latest, "Index not in range")) {
        app->marauder_state = MarauderError;
        return;
    }

    /* Route based on current tab */
    if(app->mode == SweepModeWifi) {
        parse_wifi_line(app, latest);
        app->wifi_strongest = -127;
        for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
            if(app->wifi_aps[i].valid && app->wifi_aps[i].rssi > app->wifi_strongest) {
                app->wifi_strongest = app->wifi_aps[i].rssi;
            }
        }
        app->wifi_last_scan_tick = furi_get_tick();
        if(app->marauder_state == MarauderIdle) {
            app->marauder_state = MarauderScanning;
        }
    } else if(app->mode == SweepModeBle) {
        parse_ble_line(app, latest);
        app->ble_strongest = -127;
        for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
            if(app->ble_devs[i].valid && app->ble_devs[i].rssi > app->ble_strongest) {
                app->ble_strongest = app->ble_devs[i].rssi;
            }
        }
        app->ble_last_scan_tick = furi_get_tick();
        if(app->marauder_state == MarauderIdle) {
            app->marauder_state = MarauderScanning;
        }
    }
}

/* ================================================================== */
/* RF sweep thread (handles survey / band sweep / peak refine)         */
/* ================================================================== */
static int32_t rf_sweep_thread(void* ctx) {
    App* app = ctx;

    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);

    while(app->running) {
        if(app->tx_active) {
            furi_hal_subghz_idle();
            furi_delay_ms(20);
            continue;
        }

        if(app->rf_sub == RfSubSurvey) {
            /* --- Quick survey: 16 preset channels --- */
            bool any_alert = false;
            float peak = -120.0f;
            uint8_t peak_idx = 0;

            for(uint8_t ch = 0; ch < RF_NUM_CHANNELS && app->running; ch++) {
                if(app->tx_active) break;
                furi_hal_subghz_set_frequency_and_path(rf_channels[ch]);
                furi_hal_subghz_rx();

                float sum = 0;
                for(uint8_t s = 0; s < RF_SAMPLES_PER_CH; s++) {
                    sum += furi_hal_subghz_get_rssi();
                    furi_delay_ms(5);
                }
                float avg = sum / RF_SAMPLES_PER_CH;
                furi_hal_subghz_idle();

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
            }

            /* LED streaming */
            if(peak >= -55.0f) {
                static bool blink = false;
                blink = !blink;
                notification_message(app->notif, blink ? &sequence_set_red_255 : &sequence_reset_rgb);
            } else if(peak >= -65.0f) {
                notification_message(app->notif, &sequence_set_red_255);
            } else if(peak >= -75.0f) {
                notification_message(app->notif, &sequence_solid_yellow);
            } else if(peak >= -85.0f) {
                notification_message(app->notif, &sequence_set_green_255);
            } else {
                notification_message(app->notif, &sequence_reset_rgb);
            }

        } else if(app->rf_sub == RfSubSweep && app->sweep_running) {
            /* --- Coarse band sweep --- */
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

                furi_hal_subghz_set_frequency_and_path(freq);
                furi_hal_subghz_rx();

                float sum = 0;
                for(uint8_t s = 0; s < SWEEP_SAMPLES; s++) {
                    sum += furi_hal_subghz_get_rssi();
                    furi_delay_ms(SWEEP_DWELL_MS / SWEEP_SAMPLES);
                }
                float avg = sum / SWEEP_SAMPLES;
                furi_hal_subghz_idle();

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
            notification_message(app->notif, &sequence_reset_rgb);

        } else if(app->rf_sub == RfSubPeak && app->peak_running) {
            /* --- Fine peak refinement --- */
            uint32_t center = app->last_signal_freq;
            if(center == 0) {
                app->peak_running = false;
                continue;
            }
            uint32_t start = center - PEAK_REFINE_SPAN;
            uint32_t stop = center + PEAK_REFINE_SPAN;
            uint32_t total_steps = (stop - start) / SWEEP_STEP_FINE;

            float best_rssi = -120.0f;
            uint32_t best_freq = center;

            for(uint32_t step = 0; step < total_steps && app->running && app->peak_running; step++) {
                if(app->tx_active) break;
                uint32_t freq = start + step * SWEEP_STEP_FINE;
                furi_hal_subghz_set_frequency_and_path(freq);
                furi_hal_subghz_rx();

                float sum = 0;
                for(uint8_t s = 0; s < SWEEP_SAMPLES; s++) {
                    sum += furi_hal_subghz_get_rssi();
                    furi_delay_ms(SWEEP_DWELL_MS / SWEEP_SAMPLES);
                }
                float avg = sum / SWEEP_SAMPLES;
                furi_hal_subghz_idle();

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
            notification_message(app->notif, &sequence_reset_rgb);
        } else {
            /* Idle sub-mode — brief pause to avoid busy-wait */
            furi_hal_subghz_idle();
            furi_delay_ms(50);
        }
    }

    furi_hal_subghz_sleep();
    return 0;
}

/* ================================================================== */
/* TX thread — bounded OOK carrier                                     */
/* ================================================================== */
static LevelDuration tx_carrier_cb(void* context) {
    UNUSED(context);
    return level_duration_make(true, 1000000);
}

static int32_t tx_thread(void* ctx) {
    App* app = ctx;

    uint32_t waited = 0;
    while(app->tx_active && app->running && waited < 500) {
        furi_delay_ms(10);
        waited += 10;
    }

    uint32_t duration_ms = app->tx_duration_s * 1000U;
    app->tx_remaining_ms = duration_ms;

    furi_hal_subghz_set_frequency_and_path(app->tx_freq_hz);
    if(furi_hal_subghz_start_async_tx(tx_carrier_cb, app)) {
        uint32_t elapsed = 0;
        while(app->running && elapsed < duration_ms) {
            furi_delay_ms(50);
            elapsed += 50;
            app->tx_remaining_ms = duration_ms - elapsed;
        }
        furi_hal_subghz_stop_async_tx();
    }
    furi_hal_subghz_idle();

    app->tx_active = false;
    app->tx_state = TxDisarmed;
    app->tx_remaining_ms = 0;
    return 0;
}

/* ================================================================== */
/* Feedback tick — continuous Geiger audio + vibro                     */
/* When enabled: slow heartbeat always, faster/louder near signals     */
/* ================================================================== */
static void feedback_tick(App* app) {
    float peak = app->peak_rssi;

    /* WiFi/BLE RSSI can drive feedback too */
    if(app->mode == SweepModeWifi && app->wifi_strongest > -127) {
        peak = (float)app->wifi_strongest;
    } else if(app->mode == SweepModeBle && app->ble_strongest > -127) {
        peak = (float)app->ble_strongest;
    }

    bool alerting = (peak > RF_ALERT_THRESHOLD);
    app->lock_ticks = alerting ? (app->lock_ticks + 1) : 0;

    uint32_t now = furi_get_tick();

    if(app->sound_on) {
        /* Geiger click rate: heartbeat at 2s idle → 60ms when very strong */
        uint32_t interval;
        if(peak > -50.0f) interval = 60;
        else if(peak > -60.0f) interval = 100;
        else if(peak > -70.0f) interval = 180;
        else if(peak > -80.0f) interval = 350;
        else if(peak > -90.0f) interval = 700;
        else if(peak > -100.0f) interval = 1200;
        else interval = 2000; /* heartbeat: slow tick confirming audio is live */

        if(now - app->last_click_ms >= interval) {
            app->last_click_ms = now;
            notification_message(app->notif, &seq_geiger_click);
        }

        /* Lock tone when sustained above threshold */
        if(app->lock_ticks == 5) {
            notification_message(app->notif, &seq_lock_tone);
        }
        /* Stop tone when signal drops */
        if(!alerting && app->was_alerting) {
            notification_message(app->notif, &seq_sound_stop);
        }
    }

    if(app->vibro_on) {
        /* Vibro: pulse on rising edge */
        if(alerting && !app->was_alerting) {
            notification_message(app->notif, &seq_vibro_pulse);
        }
        /* Sustained: periodic pulse while locked */
        if(app->lock_ticks > 5 && now - app->last_vibro_ms >= 800) {
            app->last_vibro_ms = now;
            notification_message(app->notif, &seq_vibro_pulse);
        }
        /* Heartbeat vibro: very slow pulse so user knows it's active */
        if(!alerting && now - app->last_vibro_ms >= 4000) {
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

    /* Sub-mode indicator */
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 52, 12, "[SURVEY]");

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

        if(r > RF_ALERT_THRESHOLD) {
            canvas_draw_box(canvas, x, base_y - (h > 0 ? h : 1), bar_w, h > 0 ? (uint8_t)h : 1);
        } else if(h >= 3) {
            canvas_draw_frame(canvas, x, base_y - h, bar_w, h);
        } else if(h >= 1) {
            canvas_draw_box(canvas, x, base_y - 1, bar_w, 1);
        }
    }
    furi_mutex_release(app->mutex);

    canvas_draw_line(canvas, 0, base_y, 127, base_y);

    /* Frequency labels */
    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i += 4) {
        canvas_draw_str(canvas, i * (bar_w + bar_gap), 63, rf_labels[i]);
    }

    /* SIGNAL alert */
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
    canvas_draw_str(canvas, 52, 12, "[SWEEP]");

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
        canvas_draw_str(canvas, 2, 62, "OK=start Up/Down=band");
    }
}

/* ================================================================== */
/* Drawing: RF Peak Refinement sub-view                                */
/* ================================================================== */
static void draw_rf_peak(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "RF Peak");
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 52, 12, "[PEAK]");

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
        canvas_draw_str(canvas, 2, 63, "OK=refine B=survey");
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
        canvas_draw_str(canvas, 8, 32, "No BFFB device");
        canvas_draw_str(canvas, 8, 44, "Connect ESP32 UART");
        return;
    }

    /* State indicator */
    canvas_set_font(canvas, FontKeyboard);
    const char* state = app->marauder_state == MarauderScanning ? "scan..." :
                        app->marauder_state == MarauderDone ? "done" :
                        app->marauder_state == MarauderError ? "ERR" : "idle";
    canvas_draw_str(canvas, 100, 12, state);

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
    }

    /* Freshness */
    if(app->wifi_last_scan_tick > 0) {
        uint32_t age_ms = furi_get_tick() - app->wifi_last_scan_tick;
        snprintf(buf, sizeof(buf), "%lus ago", (unsigned long)(age_ms / 1000));
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 86, 26, buf);
    }

    /* AP list (sorted by RSSI, show top 3) */
    canvas_set_font(canvas, FontKeyboard);
    uint8_t shown = 0;
    for(uint8_t i = 0; i < MAX_WIFI_APS && shown < 3; i++) {
        if(!app->wifi_aps[i].valid) continue;
        snprintf(buf, sizeof(buf), "%-12s %ddBm",
                 app->wifi_aps[i].ssid, app->wifi_aps[i].rssi);
        canvas_draw_str(canvas, 2, 46 + shown * 9, buf);
        shown++;
    }

    /* Controls hint */
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
        canvas_draw_str(canvas, 8, 32, "No BFFB device");
        canvas_draw_str(canvas, 8, 44, "Connect ESP32 UART");
        return;
    }

    canvas_set_font(canvas, FontKeyboard);
    const char* state = app->marauder_state == MarauderScanning ? "sniff..." :
                        app->marauder_state == MarauderDone ? "done" :
                        app->marauder_state == MarauderError ? "ERR" : "idle";
    canvas_draw_str(canvas, 100, 12, state);

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
    }

    if(app->ble_last_scan_tick > 0) {
        uint32_t age_ms = furi_get_tick() - app->ble_last_scan_tick;
        snprintf(buf, sizeof(buf), "%lus ago", (unsigned long)(age_ms / 1000));
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 86, 26, buf);
    }

    canvas_set_font(canvas, FontKeyboard);
    uint8_t shown = 0;
    for(uint8_t i = 0; i < MAX_BLE_DEVS && shown < 3; i++) {
        if(!app->ble_devs[i].valid) continue;
        snprintf(buf, sizeof(buf), "%-12s %ddBm",
                 app->ble_devs[i].name, app->ble_devs[i].rssi);
        canvas_draw_str(canvas, 2, 46 + shown * 9, buf);
        shown++;
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

    if(!app->serial) {
        canvas_draw_str(canvas, 8, 32, "No BFFB device");
        canvas_draw_str(canvas, 8, 44, "Connect ESP32 UART");
        return;
    }

    if(app->gps.has_fix) {
        canvas_draw_str(canvas, 40, 12, "3D FIX");
    } else if(app->gps.sentences > 0) {
        canvas_draw_str(canvas, 40, 12, "NO FIX");
    } else {
        canvas_draw_str(canvas, 8, 28, "Waiting for GPS...");
        canvas_draw_str(canvas, 8, 40, "(passive NMEA @115200)");
        return;
    }

    char buf[32];
    if(app->gps.has_time) {
        snprintf(buf, sizeof(buf), "UTC %02d:%02d:%02d",
                 app->gps.hour, app->gps.minute, app->gps.second);
        canvas_draw_str(canvas, 2, 24, buf);
    }

    if(app->gps.has_date) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 app->gps.year, app->gps.month, app->gps.day);
        canvas_draw_str(canvas, 80, 24, buf);
    }

    snprintf(buf, sizeof(buf), "Sats: %d view / %d used",
             app->gps.sats_in_view, app->gps.sats);
    canvas_draw_str(canvas, 2, 35, buf);

    if(app->gps.has_pos) {
        snprintf(buf, sizeof(buf), "Lat: %.5f", (double)app->gps.latitude);
        canvas_draw_str(canvas, 2, 46, buf);
        snprintf(buf, sizeof(buf), "Lon: %.5f", (double)app->gps.longitude);
        canvas_draw_str(canvas, 2, 57, buf);
    } else {
        canvas_draw_str(canvas, 2, 46, "No position yet");
    }

    canvas_set_font(canvas, FontKeyboard);
    snprintf(buf, sizeof(buf), "%lu snt", (unsigned long)app->gps.sentences);
    canvas_draw_str(canvas, 100, 63, buf);
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

        snprintf(buf, sizeof(buf), "Freq: %s MHz  Dur: %ds",
                 tx_freq_labels[app->tx_freq_idx], app->tx_duration_s);
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
        snprintf(buf, sizeof(buf), "%s MHz  %ds max",
                 tx_freq_labels[app->tx_freq_idx], app->tx_duration_s);
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
        snprintf(fbuf, sizeof(fbuf), "%s MHz", tx_freq_labels[app->tx_freq_idx]);
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
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 12, "Room Sweep v3.0");

    canvas_set_font(canvas, FontKeyboard);
    char buf[40];

    snprintf(buf, sizeof(buf), "Tabs: 6  RF: 3 sub-modes");
    canvas_draw_str(canvas, 2, 23, buf);

    snprintf(buf, sizeof(buf), "UART: %s",
             app->serial ? "connected" : "no device");
    canvas_draw_str(canvas, 2, 32, buf);

    snprintf(buf, sizeof(buf), "Sound: %s  Vibro: %s",
             app->sound_on ? "ON" : "off",
             app->vibro_on ? "ON" : "off");
    canvas_draw_str(canvas, 2, 41, buf);

    snprintf(buf, sizeof(buf), "TX: %s",
             app->tx_state == TxDisarmed ? "disarmed" :
             app->tx_state == TxArmed ? "ARMED" : "ACTIVE");
    canvas_draw_str(canvas, 2, 50, buf);

    canvas_draw_str(canvas, 2, 59, "Legal: own property only.");
    canvas_draw_str(canvas, 2, 64, "API 87.1 / Momentum");
}

/* ================================================================== */
/* Drawing: Settings overlay                                           */
/* ================================================================== */
static void draw_settings(Canvas* canvas, App* app) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, 128, 64);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 1, 1, 126, 62);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 14, "Settings");
    canvas_draw_line(canvas, 8, 17, 120, 17);

    canvas_set_font(canvas, FontSecondary);
    const char* labels[SET_COUNT] = {
        "Sound", "Vibro", "Auto-Rescan", "TX Duration",
    };

    for(int i = 0; i < SET_COUNT; i++) {
        uint8_t y = 28 + i * 10;
        if(i == (int)app->settings_sel) {
            canvas_draw_box(canvas, 4, y - 8, 120, 10);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 8, y, labels[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 8, y, labels[i]);
        }

        char val[16];
        if(i == SET_SOUND) snprintf(val, sizeof(val), "%s", app->sound_on ? "ON" : "OFF");
        else if(i == SET_VIBRO) snprintf(val, sizeof(val), "%s", app->vibro_on ? "ON" : "OFF");
        else if(i == SET_RESCAN) snprintf(val, sizeof(val), "%s", app->auto_rescan ? "ON" : "OFF");
        else if(i == SET_TXDUR) snprintf(val, sizeof(val), "%ds", app->tx_duration_s);

        uint16_t w = canvas_string_width(canvas, val);
        canvas_draw_str(canvas, 118 - w, y, val);
    }

    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 8, 62, "OK=change B=back");
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
    } else {
        canvas_draw_str(canvas, 112, 12, "RX");
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
    app->running = true;
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

    /* Notification service */
    app->notif = furi_record_open(RECORD_NOTIFICATION);

    /* UART */
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
        if(furi_message_queue_get(input_queue, &event, 100) != FuriStatusOk) {
            app->tick_count++;
            feedback_tick(app);
            process_uart_lines(app);

            /* Auto-rescan WiFi/BLE */
            if(app->auto_rescan && app->serial &&
               app->marauder_state != MarauderScanning) {
                uint32_t now = furi_get_tick();
                if(now - app->last_rescan_tick >= RESCAN_INTERVAL_MS) {
                    app->last_rescan_tick = now;
                    if(app->mode == SweepModeWifi) {
                        marauder_send(app, "scanap");
                        app->marauder_state = MarauderScanning;
                    } else if(app->mode == SweepModeBle) {
                        marauder_send(app, "sniffbt");
                        app->marauder_state = MarauderScanning;
                    }
                }
            }

            view_port_update(view_port);
            continue;
        }
        if(event.type != InputTypeShort && event.type != InputTypeLong) continue;

        /* --- Settings overlay input --- */
        if(app->settings_active) {
            if(event.key == InputKeyBack) {
                app->settings_active = false;
            } else if(event.key == InputKeyUp) {
                app->settings_sel = (app->settings_sel + SET_COUNT - 1) % SET_COUNT;
            } else if(event.key == InputKeyDown) {
                app->settings_sel = (app->settings_sel + 1) % SET_COUNT;
            } else if(event.key == InputKeyOk) {
                if(app->settings_sel == SET_SOUND) {
                    app->sound_on = !app->sound_on;
                    if(app->sound_on) {
                        notification_message(app->notif, &seq_test_beep);
                    }
                } else if(app->settings_sel == SET_VIBRO) {
                    app->vibro_on = !app->vibro_on;
                    if(app->vibro_on) {
                        notification_message(app->notif, &seq_test_vibro);
                    }
                } else if(app->settings_sel == SET_RESCAN) {
                    app->auto_rescan = !app->auto_rescan;
                } else if(app->settings_sel == SET_TXDUR) {
                    app->tx_duration_s = (app->tx_duration_s % TX_MAX_DURATION_S) + 1;
                }
            }
            continue;
        }

        /* --- TX tab: safety-critical input handling --- */
        if(app->mode == SweepModeTx) {
            if(event.key == InputKeyBack) {
                if(event.type == InputTypeLong) {
                    app->running = false;
                    break;
                }
                /* Short back: disarm if armed, else open settings */
                if(app->tx_state == TxArmed) {
                    app->tx_state = TxDisarmed;
                } else if(app->tx_state == TxDisarmed) {
                    app->settings_active = true;
                    app->settings_sel = 0;
                }
                continue;
            }
            if(event.key == InputKeyOk) {
                if(event.type == InputTypeShort && app->tx_state == TxDisarmed) {
                    app->tx_state = TxArmed;
                    notification_message(app->notif, &seq_tx_alert);
                } else if(event.type == InputTypeLong && app->tx_state == TxArmed && !app->tx_active) {
                    /* LONG OK while armed: TRANSMIT */
                    app->tx_state = TxTransmitting;
                    app->tx_active = true;
                    app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
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
                app->mode = (app->mode == 0) ? (SweepMode)(SweepModeCount - 1)
                                             : (SweepMode)(app->mode - 1);
                if(event.key == InputKeyRight) {
                    app->mode = (SweepMode)((app->mode + 2) % SweepModeCount);
                }
                app->gps_active = (app->mode == SweepModeGps && app->serial != NULL);
            }
            continue;
        }

        /* --- General input (non-TX tabs) --- */
        if(event.key == InputKeyBack) {
            if(event.type == InputTypeLong) {
                app->running = false;
                break;
            }
            /* Short back: open settings */
            app->settings_active = true;
            app->settings_sel = 0;
            continue;
        }

        if(event.key == InputKeyLeft) {
            app->mode = (app->mode == 0) ? (SweepMode)(SweepModeCount - 1)
                                         : (SweepMode)(app->mode - 1);
            if(app->serial) {
                app->line_count = 0;
                if(app->marauder_state != MarauderNoDevice)
                    app->marauder_state = MarauderIdle;
            }
            app->gps_active = (app->mode == SweepModeGps && app->serial != NULL);
        }
        if(event.key == InputKeyRight) {
            app->mode = (SweepMode)((app->mode + 1) % SweepModeCount);
            if(app->serial) {
                app->line_count = 0;
                if(app->marauder_state != MarauderNoDevice)
                    app->marauder_state = MarauderIdle;
            }
            app->gps_active = (app->mode == SweepModeGps && app->serial != NULL);
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
            /* In sweep mode when not sweeping: Up/Down select band */
            if(app->rf_sub == RfSubSweep && !app->sweep_running) {
                if(event.key == InputKeyUp || event.key == InputKeyDown) {
                    /* Already handled sub-mode cycling above;
                       use Left/Right for band selection in sweep idle */
                }
            }
        }

        /* --- WiFi/BLE: OK starts scan --- */
        if(event.key == InputKeyOk && app->mode == SweepModeWifi && app->serial) {
            marauder_send(app, "scanap");
            app->marauder_state = MarauderScanning;
            app->last_rescan_tick = furi_get_tick();
        }
        if(event.key == InputKeyOk && app->mode == SweepModeBle && app->serial) {
            marauder_send(app, "sniffbt");
            app->marauder_state = MarauderScanning;
            app->last_rescan_tick = furi_get_tick();
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

        /* Band selection with Left/Right in sweep idle */
        if(app->mode == SweepModeRF && app->rf_sub == RfSubSweep && !app->sweep_running) {
            if(event.key == InputKeyLeft) {
                app->sweep_band_idx = (app->sweep_band_idx + RF_BAND_COUNT - 1) % RF_BAND_COUNT;
            }
            if(event.key == InputKeyRight) {
                app->sweep_band_idx = (app->sweep_band_idx + 1) % RF_BAND_COUNT;
            }
        }

        app->tick_count++;
        feedback_tick(app);
        view_port_update(view_port);
    }

    /* Cleanup */
    notification_message(app->notif, &sequence_reset_rgb);
    notification_message(app->notif, &sequence_reset_sound);
    notification_message(app->notif, &sequence_reset_vibro);
    furi_record_close(RECORD_NOTIFICATION);

    app->running = false;

    if(app->tx_thread) {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }

    furi_thread_join(app->rf_thread);
    furi_thread_free(app->rf_thread);

    marauder_close(app);

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(input_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}

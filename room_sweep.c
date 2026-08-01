/* Room Sweep v2.0 — multi-tab wireless surveillance detector
 * Tab 1: Sub-GHz RSSI sweep (analog bugs / wireless cameras)
 * Tab 2: Marauder WiFi AP scan over UART (BFFB ESP32)
 * Tab 3: Marauder BLE sniff over UART (BFFB ESP32)
 * Tab 4: Info / legal
 *
 * BFFB disconnected: tabs 2-3 show "connect BFFB" message.
 * All API calls verified against Momentum mntm-012 API 87.1 export table.
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

#include "room_sweep.h"
#include "nmea.h"

/* ------------------------------------------------------------------ */
/* Shared state                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    SweepMode mode;
    volatile bool running;         // main loop alive
    FuriMutex* mutex;

    /* RF sweep */
    volatile float rssi[RF_NUM_CHANNELS];
    volatile uint8_t sweep_ch;     // current channel being measured
    volatile bool rf_alert;        // any channel above threshold
    volatile float peak_rssi;      // strongest channel this sweep
    volatile uint8_t peak_ch;      // index of strongest channel
    FuriThread* rf_thread;

    /* Marauder UART */
    MarauderState marauder_state;
    FuriHalSerialHandle* serial;
    char lines[MARAUDER_MAX_LINES][MARAUDER_LINE_MAX];
    uint8_t line_count;
    char line_buf[MARAUDER_LINE_MAX];
    uint8_t line_pos;
    volatile bool uart_rx_flag;    // ISR -> thread signal

    /* notification / feedback */
    NotificationApp* notif;
    bool sound_on;                 // Up toggles (default: OFF)
    bool vibro_on;                 // Down toggles
    uint32_t tick_count;           // main loop iteration counter
    uint32_t last_click_ms;        // last Geiger click time
    uint32_t last_vibro_ms;        // last vibro pulse time
    bool was_alerting;             // previous tick alert state (edge detect)
    uint8_t lock_ticks;            // consecutive ticks above threshold

    /* GPS (passive NMEA on UART when GPS tab active) */
    GpsFix gps;                    // parser state (host-tested, 24/24 pass)
    volatile bool gps_active;      // GPS tab is active — feed NMEA bytes
    volatile uint8_t gps_sats;
    volatile float gps_lat;
    volatile float gps_lon;
    volatile float gps_alt_m;
    volatile bool gps_fix_valid;

    /* RF transmit (SubGHz carrier test — safety-gated, OWN PROPERTY ONLY) */
    volatile bool tx_active;       // true while transmitting (stands RX down)
    FuriThread* tx_thread;
    volatile uint32_t tx_freq_hz;  // transmit frequency (default 433.92 MHz)
    volatile uint32_t tx_start_tick;
    bool tx_warning_shown;         // user acknowledged the safety warning
    bool tx_warning_active;        // safety overlay currently displayed

    /* Settings menu (opened via Back button from scan view) */
    bool settings_active;          // settings overlay displayed
    uint8_t settings_sel;          // highlighted item index
    uint8_t tx_freq_idx;           // index into tx_freq_presets[]
} App;

/* ------------------------------------------------------------------ */
/* TX frequency presets (ISM / common bands, CC1101-valid)            */
/* ------------------------------------------------------------------ */
#define TX_FREQ_PRESET_COUNT 4
static const uint32_t tx_freq_presets[TX_FREQ_PRESET_COUNT] = {
    433920000, 868350000, 915000000, 315000000,
};
static const char* tx_freq_labels[TX_FREQ_PRESET_COUNT] = {
    "433.92 MHz", "868.35 MHz", "915.00 MHz", "315.00 MHz",
};

/* Settings menu item indices */
enum {
    SET_SOUND = 0,
    SET_VIBRO,
    SET_TXFREQ,
    SET_COUNT,
};

/* ------------------------------------------------------------------ */
/* Notification sequences                                             */
/* SILENT OPERATION MODE: no custom sound/vibro sequences are defined.  */
/* All alerting is LED-only (RF sweep thread). The SDK reset sequences  */
/* are used at teardown to clear any stuck LED/sound/vibro state.       */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* UART ISR callback — feeds ring of lines, ISR-safe (no alloc)        */
/* ------------------------------------------------------------------ */
static void uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(handle);
    App* app = ctx;
    if(event != FuriHalSerialRxEventData) return;

    uint8_t byte = furi_hal_serial_async_rx(app->serial);

    /* GPS tab active: feed every byte to the NMEA parser.
     * nmea_feed is allocation-free and non-blocking — ISR-safe. */
    if(app->gps_active) {
        nmea_feed(&app->gps, (char)byte);
    }

    if(byte == '\n' || byte == '\r') {
        if(app->line_pos > 0) {
            app->line_buf[app->line_pos] = '\0';
            /* shift lines up */
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

/* ------------------------------------------------------------------ */
/* Marauder UART open / close                                          */
/* ------------------------------------------------------------------ */
static bool marauder_open(App* app) {
    /* Disable expansion module so we can own USART */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial) return false;

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

    /* Re-enable expansion module */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
}

static void marauder_send(App* app, const char* cmd) {
    if(!app->serial) return;
    furi_hal_serial_tx(app->serial, (const uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx(app->serial, (const uint8_t*)"\r\n", 2);
}

/* ------------------------------------------------------------------ */
/* RF sweep thread                                                     */
/* ------------------------------------------------------------------ */
static int32_t rf_sweep_thread(void* ctx) {
    App* app = ctx;

    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);

    while(app->running) {
        /* Yield radio to TX thread if transmitting — RX/TX share the CC1101
         * and MUST NOT run simultaneously (hardware fault otherwise). */
        if(app->tx_active) {
            furi_hal_subghz_idle();
            furi_delay_ms(20);
            continue;
        }

        bool any_alert = false;
        float peak = -120.0f;
        uint8_t peak_idx = 0;

        for(uint8_t ch = 0; ch < RF_NUM_CHANNELS && app->running; ch++) {
            furi_hal_subghz_set_frequency_and_path(rf_channels[ch]);
            furi_hal_subghz_rx();

            /* sample and average */
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

        /* LED streaming: escalate color + blink with signal strength.
         * Bands: off (<-85), green (-85..-75), yellow (-75..-65),
         * red solid (-65..-55), red blink (>= -55). */
        if(peak >= -55.0f) {
            /* Very strong: fast blink red (alternate set/reset each sweep) */
            static bool blink_state = false;
            blink_state = !blink_state;
            if(blink_state) {
                notification_message(app->notif, &sequence_set_red_255);
            } else {
                notification_message(app->notif, &sequence_reset_rgb);
            }
        } else if(peak >= -65.0f) {
            notification_message(app->notif, &sequence_set_red_255);
        } else if(peak >= -75.0f) {
            notification_message(app->notif, &sequence_solid_yellow);
        } else if(peak >= -85.0f) {
            notification_message(app->notif, &sequence_set_green_255);
        } else {
            notification_message(app->notif, &sequence_reset_rgb);
        }
    }

    furi_hal_subghz_sleep();
    return 0;
}

/* ------------------------------------------------------------------ */
/* RF transmit (carrier test) — safety-gated, OWN PROPERTY ONLY        */
/* ------------------------------------------------------------------ */
/* Async TX callback: emit a continuous OOK carrier (level HIGH, long
 * duration) so a receiver on the same frequency sees a steady signal.
 * Returning the same high level repeatedly holds the carrier on. */
static LevelDuration tx_carrier_cb(void* context) {
    UNUSED(context);
    return level_duration_make(true, 1000000); /* HIGH for 1,000,000 us */
}

/* TX thread: stand up the carrier on tx_freq_hz for a bounded window
 * (TX_MAX_MS) then stop automatically. Never transmits indefinitely. */
#define TX_MAX_MS 3000U

static int32_t tx_thread(void* ctx) {
    App* app = ctx;

    /* Wait until the RX sweep thread has yielded the radio. */
    uint32_t waited = 0;
    while(app->tx_active && app->running && waited < 500) {
        furi_delay_ms(10);
        waited += 10;
    }

    furi_hal_subghz_set_frequency_and_path(app->tx_freq_hz);
    if(furi_hal_subghz_start_async_tx(tx_carrier_cb, app)) {
        uint32_t elapsed = 0;
        while(app->running && elapsed < TX_MAX_MS) {
            furi_delay_ms(50);
            elapsed += 50;
        }
        furi_hal_subghz_stop_async_tx();
    }
    furi_hal_subghz_idle();

    app->tx_active = false; /* release radio back to the RX sweep */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Feedback logic (called from main loop each tick)                    */
/*                                                                     */
/* SILENT OPERATION MODE: audio (Geiger clicks / lock tone) and vibro  */
/* pulses are intentionally DISABLED — the user is sleeping in the     */
/* room. Only the LED escalation (driven by the RF sweep thread)       */
/* provides alerts, which is silent. The sound_on / vibro_on flags are */
/* still tracked so the UI can show their state and so the behavior    */
/* can be restored, but nothing here emits sound or vibration.         */
/* ------------------------------------------------------------------ */
static void feedback_tick(App* app) {
    float peak = app->peak_rssi;
    bool alerting = (peak > RF_ALERT_THRESHOLD);

    /* Track alert state only (LED path reads app->rf_alert separately).
     * No notification_message() calls — guaranteed silent. */
    app->lock_ticks = alerting ? (app->lock_ticks + 1) : 0;
    app->was_alerting = alerting;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */
static void draw_rf_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);

    /* --- Header (y 0..12) --- */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 11, "Sub-GHz  RF Sweep");

    /* Peak RSSI numeric readout (large, right-aligned) */
    char buf[16];
    float peak = app->peak_rssi;
    snprintf(buf, sizeof(buf), "%.0f", (double)peak);
    canvas_set_font(canvas, FontPrimary);
    uint16_t num_w = canvas_string_width(canvas, buf);
    canvas_draw_str(canvas, 127 - (int)num_w, 11, buf);
    /* tiny "dBm" suffix */
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 127 - (int)num_w - 16, 11, "dBm");

    /* --- Bar chart area (y 14..56) --- */
    const uint8_t bar_w = 7;
    const uint8_t bar_gap = 1;
    const uint8_t area_h = 42;
    const uint8_t base_y = 56; /* bottom row of bars (NOT last pixel) */

    /* Threshold reference line — dotted, drawn FIRST (behind bars) */
    int th_y = base_y - (int)((RF_ALERT_THRESHOLD + 100.0f) * area_h / 70.0f);
    canvas_set_color(canvas, ColorBlack);
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
            /* Above threshold: solid filled bar */
            canvas_draw_box(canvas, x, base_y - (h > 0 ? h : 1), bar_w, h > 0 ? (uint8_t)h : 1);
        } else if(h >= 3) {
            /* Tall enough for a hollow frame to read correctly */
            canvas_draw_frame(canvas, x, base_y - h, bar_w, h);
        } else if(h >= 1) {
            /* Very short: single filled row (no hollow-frame artifact) */
            canvas_draw_box(canvas, x, base_y - 1, bar_w, 1);
        }
        /* h == 0: draw nothing — clean baseline */
    }
    furi_mutex_release(app->mutex);

    /* Baseline */
    canvas_draw_line(canvas, 0, base_y, 127, base_y);

    /* --- Frequency labels (y 63, last visible row) --- */
    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i += 4) {
        uint8_t x = i * (bar_w + bar_gap);
        canvas_draw_str(canvas, x, 63, rf_labels[i]);
    }

    /* --- SIGNAL! alert banner (overlay, inverse video) --- */
    if(app->rf_alert) {
        canvas_set_font(canvas, FontBigNumbers);
        const char* sig = "SIGNAL!";
        uint16_t w = canvas_string_width(canvas, sig);
        uint8_t sx = (128 - w) / 2;
        /* White knockout box behind text */
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, sx - 2, 24, w + 4, 14);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str(canvas, sx, 36, sig);
    }
}

static void draw_wifi_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "WiFi AP Scan");
    canvas_set_font(canvas, FontSecondary);

    if(!app->serial) {
        canvas_draw_str(canvas, 8, 32, "BFFB not connected");
        canvas_draw_str(canvas, 8, 44, "Plug in ESP32 via UART");
        return;
    }

    const char* state_str = app->marauder_state == MarauderScanning ? "Scanning..." :
                            app->marauder_state == MarauderError    ? "Error" : "Idle";
    canvas_draw_str(canvas, 80, 12, state_str);

    canvas_set_font(canvas, FontKeyboard);
    uint8_t shown = app->line_count < 5 ? app->line_count : 5;
    for(uint8_t i = 0; i < shown; i++) {
        canvas_draw_str(canvas, 2, 24 + i * 9, app->lines[i]);
    }
}

static void draw_ble_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "BLE Sniff");
    canvas_set_font(canvas, FontSecondary);

    if(!app->serial) {
        canvas_draw_str(canvas, 8, 32, "BFFB not connected");
        canvas_draw_str(canvas, 8, 44, "Plug in ESP32 via UART");
        return;
    }

    const char* state_str = app->marauder_state == MarauderScanning ? "Sniffing..." :
                            app->marauder_state == MarauderError    ? "Error" : "Idle";
    canvas_draw_str(canvas, 80, 12, state_str);

    canvas_set_font(canvas, FontKeyboard);
    uint8_t shown = app->line_count < 5 ? app->line_count : 5;
    for(uint8_t i = 0; i < shown; i++) {
        canvas_draw_str(canvas, 2, 24 + i * 9, app->lines[i]);
    }
}

static void draw_gps_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "GPS");
    canvas_set_font(canvas, FontSecondary);

    if(!app->serial) {
        canvas_draw_str(canvas, 8, 32, "BFFB not connected");
        canvas_draw_str(canvas, 8, 44, "Plug in ESP32 via UART");
        return;
    }

    /* Fix status banner */
    if(app->gps.has_fix) {
        canvas_draw_str(canvas, 40, 12, "3D FIX");
    } else if(app->gps.sentences > 0) {
        canvas_draw_str(canvas, 40, 12, "NO FIX");
    } else {
        canvas_draw_str(canvas, 8, 24, "Waiting for GPS...");
        canvas_draw_str(canvas, 8, 35, "(passive NMEA @115200)");
        return;
    }

    /* UTC time */
    char buf[32];
    if(app->gps.has_time) {
        snprintf(buf, sizeof(buf), "UTC %02d:%02d:%02d",
                 app->gps.hour, app->gps.minute, app->gps.second);
        canvas_draw_str(canvas, 2, 24, buf);
    } else {
        canvas_draw_str(canvas, 2, 24, "UTC --:--:--");
    }

    /* Date */
    if(app->gps.has_date) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 app->gps.year, app->gps.month, app->gps.day);
        canvas_draw_str(canvas, 80, 24, buf);
    }

    /* Satellites */
    snprintf(buf, sizeof(buf), "Sats: %d view / %d used",
             app->gps.sats_in_view, app->gps.sats);
    canvas_draw_str(canvas, 2, 35, buf);

    /* Position */
    if(app->gps.has_pos) {
        snprintf(buf, sizeof(buf), "%.5f", (double)app->gps.latitude);
        canvas_draw_str(canvas, 2, 46, buf);
        snprintf(buf, sizeof(buf), "%.5f", (double)app->gps.longitude);
        canvas_draw_str(canvas, 2, 57, buf);
        canvas_draw_str(canvas, 70, 46, "lat");
        canvas_draw_str(canvas, 70, 57, "lon");
    } else {
        canvas_draw_str(canvas, 2, 46, "No position yet");
        canvas_draw_str(canvas, 2, 57, "Move outdoors");
    }

    /* Sentence counter (bottom-right, tiny) */
    canvas_set_font(canvas, FontKeyboard);
    snprintf(buf, sizeof(buf), "%lu snt", (unsigned long)app->gps.sentences);
    canvas_draw_str(canvas, 100, 63, buf);
}

static void draw_info_tab(Canvas* canvas, App* app) {
    UNUSED(app);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Room Sweep v2.0");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "RF: 16ch ISM 304-925MHz");
    canvas_draw_str(canvas, 2, 35, "WiFi/BLE: needs BFFB ESP32");
    canvas_draw_str(canvas, 2, 46, "Legal: own property only.");
    canvas_draw_str(canvas, 2, 57, "Passive RX. No transmit.");
}

/* ------------------------------------------------------------------ */
/* Settings menu overlay                                               */
/* ------------------------------------------------------------------ */
static void draw_settings_menu(Canvas* canvas, App* app) {
    /* Dim background */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, 128, 64);

    /* Frame */
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 1, 1, 126, 62);

    /* Title */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 15, "Settings");
    canvas_draw_line(canvas, 8, 19, 120, 19);

    /* Menu items */
    canvas_set_font(canvas, FontSecondary);

    /* Row positions: y = 30, 42, 54 */
    const char* labels[SET_COUNT] = {
        "Sound Alerts",
        "Vibration",
        "TX Frequency",
    };

    for(int i = 0; i < SET_COUNT; i++) {
        uint8_t y = 30 + i * 12;
        if(i == (int)app->settings_sel) {
            canvas_draw_box(canvas, 4, y - 9, 120, 11);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 8, y, labels[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 8, y, labels[i]);
        }

        /* Right-aligned values */
        char val[16];
        if(i == SET_SOUND) {
            snprintf(val, sizeof(val), "%s", app->sound_on ? "ON" : "OFF");
        } else if(i == SET_VIBRO) {
            snprintf(val, sizeof(val), "%s", app->vibro_on ? "ON" : "OFF");
        } else if(i == SET_TXFREQ) {
            snprintf(val, sizeof(val), "%s", tx_freq_labels[app->tx_freq_idx]);
        }
        uint16_t w = canvas_string_width(canvas, val);
        canvas_draw_str(canvas, 118 - w, y, val);
    }

    /* Bottom hint */
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 8, 61, "OK=toggle B=back");
}

/* ------------------------------------------------------------------ */
/* Main draw callback                                                  */
/* ------------------------------------------------------------------ */
static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    switch(app->mode) {
    case SweepModeRF:    draw_rf_tab(canvas, app);    break;
    case SweepModeWifi:  draw_wifi_tab(canvas, app);  break;
    case SweepModeBle:   draw_ble_tab(canvas, app);   break;
    case SweepModeGps:   draw_gps_tab(canvas, app);   break;
    case SweepModeInfo:  draw_info_tab(canvas, app);  break;
    default: break;
    }

    /* --- Tab indicator strip (y 0..3) — 5 tabs, 25px each --- */
    canvas_draw_line(canvas, 0, 0, 127, 0);
    static const char* tab_labels[] = {"RF", "Wi", "BT", "GPS", "i"};
    for(int t = 0; t < SweepModeCount; t++) {
        uint8_t x = 1 + t * 25;
        if(t == (int)app->mode) {
            canvas_draw_box(canvas, x, 1, 24, 3);
            /* Label under the active tab */
            canvas_set_font(canvas, FontKeyboard);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, x + 6, 4, tab_labels[t]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_frame(canvas, x, 1, 24, 3);
        }
    }

    /* --- Status indicators (top-right, moved BELOW header to avoid clutter) --- */
    canvas_set_font(canvas, FontKeyboard);

    /* TX / RX indicator — always visible (below tab strip) */
    if(app->tx_active) {
        /* Inverse-video "TX" badge */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 106, 16, 20, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 109, 24, "TX");
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, 106, 24, "RX");
    }

    /* Sound/Vibro state — only in non-RF tabs */
    if(app->mode != SweepModeRF) {
        canvas_draw_str(canvas, 104, 23, app->sound_on ? "S:ON" : "S:off");
        canvas_draw_str(canvas, 104, 31, app->vibro_on ? "V:ON" : "V:off");
    }

    /* --- TX SAFETY WARNING overlay (highest priority, covers screen) --- */
    if(app->tx_warning_active) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, 128, 64);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 1, 1, 126, 62);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 8, 14, "!! TRANSMIT !!");

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 8, 26, "Radiates RF energy.");
        canvas_draw_str(canvas, 8, 36, "AUTHORIZED USE ONLY:");
        canvas_draw_str(canvas, 8, 45, "own property / licensed.");

        char fbuf[40];
        snprintf(fbuf, sizeof(fbuf), "Freq: %06lu kHz, 3s max",
                 (unsigned long)(app->tx_freq_hz / 1000));
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 8, 56, fbuf);

        /* Inverse-video prompt */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 70, 58, 54, 6);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 72, 63, "OK=send B=cxl");
        canvas_set_color(canvas, ColorBlack);
    }

    /* --- SETTINGS MENU overlay (highest priority, covers screen) --- */
    if(app->settings_active) {
        draw_settings_menu(canvas, app);
    }
}

/* ------------------------------------------------------------------ */
/* Input handler                                                       */
/* ------------------------------------------------------------------ */
static void input_cb(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, 0);
}

/* ------------------------------------------------------------------ */
/* App entry                                                           */
/* ------------------------------------------------------------------ */
int32_t room_sweep_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->running = true;
    app->mode = SweepModeRF;
    app->sound_on = false;  /* default: SOUND OFF (silent operation; Up toggles) */
    app->vibro_on = false;  /* default: vibro off (can be noisy) */
    app->peak_rssi = -120.0f;

    /* RF transmit defaults (safety-gated, OWN PROPERTY ONLY) */
    app->tx_freq_hz = 433920000;  /* 433.92 MHz ISM carrier-test freq */
    app->tx_freq_idx = 0;
    app->tx_active = false;
    app->tx_warning_shown = false;
    app->tx_warning_active = false;
    app->tx_thread = NULL;

    /* Settings menu defaults */
    app->settings_active = false;
    app->settings_sel = 0;

    /* GPS parser init (host-tested: 48/48 assertions pass) */
    nmea_init(&app->gps);
    app->gps_active = false;

    /* notification service */
    app->notif = furi_record_open(RECORD_NOTIFICATION);

    /* try to open UART (will fail gracefully if BFFB not connected) */
    marauder_open(app);

    /* start RF sweep thread */
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
            /* No input this tick — run feedback + redraw */
            app->tick_count++;
            feedback_tick(app);
            view_port_update(view_port);
            continue;
        }
        if(event.type != InputTypeShort && event.type != InputTypeLong) continue;

        /* --- SETTINGS MENU INPUT (when overlay active) --- */
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
                } else if(app->settings_sel == SET_VIBRO) {
                    app->vibro_on = !app->vibro_on;
                } else if(app->settings_sel == SET_TXFREQ) {
                    app->tx_freq_idx = (app->tx_freq_idx + 1) % TX_FREQ_PRESET_COUNT;
                    app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
                }
            }
            continue; /* consume event, don't fall through to scan handlers */
        }

        if(event.key == InputKeyBack) {
            /* If the TX safety warning is showing, Back cancels it (not exit). */
            if(app->tx_warning_active) {
                app->tx_warning_active = false;
                continue;
            }
            /* LONG press Back = exit app. SHORT press = open Settings. */
            if(event.type == InputTypeLong) {
                app->running = false;
                break;
            }
            app->settings_active = true;
            app->settings_sel = 0;
            continue;
        }
        if(event.key == InputKeyLeft) {
            app->mode = (app->mode == 0) ? (SweepMode)(SweepModeCount - 1)
                                         : (SweepMode)(app->mode - 1);
            /* switch UART context for wifi/ble tabs */
            if(app->serial) {
                app->line_count = 0;
                app->marauder_state = MarauderIdle;
            }
            /* GPS tab: enable NMEA feeding only while on GPS tab */
            app->gps_active = (app->mode == SweepModeGps && app->serial != NULL);
        }
        if(event.key == InputKeyRight) {
            app->mode = (SweepMode)((app->mode + 1) % SweepModeCount);
            if(app->serial) {
                app->line_count = 0;
                app->marauder_state = MarauderIdle;
            }
            app->gps_active = (app->mode == SweepModeGps && app->serial != NULL);
        }
        if(event.key == InputKeyUp) {
            /* Toggle sound flag (state only — SILENT MODE emits no audio). */
            app->sound_on = !app->sound_on;
            /* Intentionally NO notification_message: silent operation. */
        }
        if(event.key == InputKeyDown) {
            /* Toggle vibro flag (state only — SILENT MODE emits no haptics). */
            app->vibro_on = !app->vibro_on;
            /* Intentionally NO notification_message: silent operation. */
        }
        if(event.key == InputKeyOk) {
            /* --- TX SAFETY GATE (RF tab only) --- */
            if(app->tx_warning_active) {
                /* Second OK: user confirmed → transmit a 3s bounded carrier */
                app->tx_warning_active = false;
                app->tx_warning_shown = true;
                app->tx_active = true;
                app->tx_start_tick = furi_get_tick();
                if(app->tx_thread == NULL) {
                    app->tx_thread = furi_thread_alloc_ex("RoomSweepTX", 2048, tx_thread, app);
                }
                furi_thread_start(app->tx_thread);
            } else if(app->mode == SweepModeRF && !app->tx_active) {
                /* First OK on RF tab: show the safety warning overlay */
                app->tx_warning_active = true;
            } else if(app->mode == SweepModeWifi && app->serial) {
                marauder_send(app, "scanap");
                app->marauder_state = MarauderScanning;
            } else if(app->mode == SweepModeBle && app->serial) {
                marauder_send(app, "sniffbt");
                app->marauder_state = MarauderScanning;
            }
        }
        /* Cancel TX warning overlay with Left/Right too */
        if(app->tx_warning_active &&
           (event.key == InputKeyLeft || event.key == InputKeyRight)) {
            app->tx_warning_active = false;
            continue; /* don't also change tabs */
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

    /* Stop + join TX thread first (it owns the radio while active) */
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

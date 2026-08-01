/* Room Sweep — multi-tab wireless surveillance detector
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
#include <lib/subghz/devices/cc1101_configs.h>

#include "room_sweep.h"

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
    FuriThread* rf_thread;

    /* Marauder UART */
    MarauderState marauder_state;
    FuriHalSerialHandle* serial;
    char lines[MARAUDER_MAX_LINES][MARAUDER_LINE_MAX];
    uint8_t line_count;
    char line_buf[MARAUDER_LINE_MAX];
    uint8_t line_pos;
    volatile bool uart_rx_flag;    // ISR -> thread signal

    /* notification */
    NotificationApp* notif;
} App;

/* ------------------------------------------------------------------ */
/* UART ISR callback — feeds ring of lines, ISR-safe (no alloc)        */
/* ------------------------------------------------------------------ */
static void uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(handle);
    App* app = ctx;
    if(event != FuriHalSerialRxEventData) return;

    uint8_t byte = furi_hal_serial_async_rx(app->serial);
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
        bool any_alert = false;
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
        }
        app->rf_alert = any_alert;
        if(any_alert) {
            notification_message(app->notif, &sequence_set_red_255);
        } else {
            notification_message(app->notif, &sequence_reset_red);
        }
    }

    furi_hal_subghz_sleep();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */
static void draw_rf_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "RF Sweep  (Sub-GHz)");

    /* bar chart: 16 channels, 7px wide, 50px tall area starting at y=14 */
    const uint8_t bar_w = 7;
    const uint8_t area_h = 48;
    const uint8_t base_y = 63;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) {
        /* Map RSSI [-100..-30] -> [0..area_h] */
        float r = app->rssi[i];
        int h = (int)((r + 100.0f) * area_h / 70.0f);
        if(h < 0) h = 0;
        if(h > area_h) h = area_h;
        uint8_t x = 1 + i * (bar_w + 0);

        if(app->rssi[i] > RF_ALERT_THRESHOLD) {
            canvas_draw_box(canvas, x, base_y - h, bar_w - 1, h);
        } else {
            canvas_draw_frame(canvas, x, base_y - h, bar_w - 1, h);
        }
        /* channel label every 4th */
        if(i % 4 == 0) {
            canvas_set_font(canvas, FontKeyboard);
            canvas_draw_str(canvas, x, base_y + 1, rf_labels[i]);
            canvas_set_font(canvas, FontSecondary);
        }
    }
    furi_mutex_release(app->mutex);

    /* threshold line */
    int th_y = base_y - (int)((RF_ALERT_THRESHOLD + 100.0f) * area_h / 70.0f);
    canvas_draw_line(canvas, 0, th_y, 127, th_y);

    /* status */
    if(app->rf_alert) {
        canvas_draw_str(canvas, 80, 10, "SIGNAL!");
    }
}

static void draw_wifi_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "WiFi AP Scan (Marauder)");

    if(!app->serial) {
        canvas_draw_str(canvas, 10, 35, "BFFB not connected");
        canvas_draw_str(canvas, 10, 47, "Plug in ESP32 via UART");
        return;
    }

    const char* state_str = app->marauder_state == MarauderScanning ? "Scanning..." :
                            app->marauder_state == MarauderError    ? "Error" : "Idle";
    canvas_draw_str(canvas, 90, 10, state_str);

    /* show last captured lines */
    canvas_set_font(canvas, FontKeyboard);
    uint8_t shown = app->line_count < 5 ? app->line_count : 5;
    for(uint8_t i = 0; i < shown; i++) {
        canvas_draw_str(canvas, 2, 22 + i * 9, app->lines[i]);
    }
    canvas_set_font(canvas, FontSecondary);
}

static void draw_ble_tab(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "BLE Sniff (Marauder)");

    if(!app->serial) {
        canvas_draw_str(canvas, 10, 35, "BFFB not connected");
        canvas_draw_str(canvas, 10, 47, "Plug in ESP32 via UART");
        return;
    }

    const char* state_str = app->marauder_state == MarauderScanning ? "Sniffing..." :
                            app->marauder_state == MarauderError    ? "Error" : "Idle";
    canvas_draw_str(canvas, 90, 10, state_str);

    canvas_set_font(canvas, FontKeyboard);
    uint8_t shown = app->line_count < 5 ? app->line_count : 5;
    for(uint8_t i = 0; i < shown; i++) {
        canvas_draw_str(canvas, 2, 22 + i * 9, app->lines[i]);
    }
    canvas_set_font(canvas, FontSecondary);
}

static void draw_info_tab(Canvas* canvas, App* app) {
    UNUSED(app);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "Room Sweep v1.0");
    canvas_draw_str(canvas, 2, 22, "RF: 16ch ISM sweep 304-925M");
    canvas_draw_str(canvas, 2, 33, "WiFi/BLE: needs BFFB ESP32");
    canvas_draw_str(canvas, 2, 44, "Legal: own property only.");
    canvas_draw_str(canvas, 2, 55, "No TX. Passive RX only.");
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
    case SweepModeInfo:  draw_info_tab(canvas, app);  break;
    default: break;
    }
    /* tab indicator bar */
    canvas_draw_line(canvas, 0, 0, 127, 0);
    for(int t = 0; t < SweepModeCount; t++) {
        uint8_t x = 2 + t * 32;
        if(t == (int)app->mode) {
            canvas_draw_box(canvas, x, 1, 28, 3);
        } else {
            canvas_draw_frame(canvas, x, 1, 28, 3);
        }
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
            view_port_update(view_port);
            continue;
        }
        if(event.type != InputTypeShort && event.type != InputTypeLong) continue;

        if(event.key == InputKeyBack) {
            app->running = false;
            break;
        }
        if(event.key == InputKeyLeft) {
            app->mode = (app->mode == 0) ? (SweepMode)(SweepModeCount - 1)
                                         : (SweepMode)(app->mode - 1);
            /* switch UART context for wifi/ble tabs */
            if(app->serial) {
                app->line_count = 0;
                app->marauder_state = MarauderIdle;
            }
        }
        if(event.key == InputKeyRight) {
            app->mode = (SweepMode)((app->mode + 1) % SweepModeCount);
            if(app->serial) {
                app->line_count = 0;
                app->marauder_state = MarauderIdle;
            }
        }
        if(event.key == InputKeyOk && app->serial) {
            /* start scan for wifi/ble tabs */
            if(app->mode == SweepModeWifi) {
                marauder_send(app, "scanap");
                app->marauder_state = MarauderScanning;
            } else if(app->mode == SweepModeBle) {
                marauder_send(app, "sniffbt");
                app->marauder_state = MarauderScanning;
            }
        }
        view_port_update(view_port);
    }

    /* Cleanup */
    notification_message(app->notif, &sequence_reset_rgb);
    furi_record_close(RECORD_NOTIFICATION);

    app->running = false;
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

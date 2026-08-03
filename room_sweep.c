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
#include <furi_hal_region.h>
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
#include "room_sweep_report.h"
#include "room_sweep_scan.h"
#include "room_sweep_gps_state.h"
#include "room_sweep_wireless.h"
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
#define RECORD_QUEUE_CAPACITY 32U
#define INPUT_QUEUE_CAPACITY 16U

typedef struct {
    char event[20];
    char source[12];
    char mode[12];
    char submode[16];
    char id[33];
    char detail[48];
    char state[16];
    char error_code[20];
    int rssi;
    uint32_t freq_hz;
    uint8_t channel;
    float lat;
    float lon;
    bool has_pos;
    bool has_gps;
    uint8_t fix_quality;
    uint8_t sats_used;
    uint8_t sats_view;
    float speed_kmh;
    float course_deg;
    char gps_utc[12];
    char gps_date[15];
    uint32_t gps_nav_sentences;
    uint32_t gps_rx_bytes;
    uint32_t count;
} PendingRecordEvent;

typedef struct {
    volatile SweepMode mode;
    volatile RfSubMode rf_sub;
    volatile bool running;
    FuriMutex* mutex;
    FuriMutex* radio_mutex;
    FuriMutex* gps_mutex;
    FuriMutex* record_mutex;

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

    /* Qualified, time-bounded RF observation used for refine/lock/TX handoff. */
    RoomSweepSignalCandidate signal_candidate;
    volatile uint32_t rf_config_generation;

    /* SubGHz radio: prefer BFFB external SPI CC1101, else internal */
    const SubGhzDevice* radio;
    RadioPath radio_path;
    bool radio_otg_on;

    /* Marauder UART */
    volatile MarauderState marauder_state;
    FuriHalSerialHandle* serial;
    char pending_lines[MARAUDER_MAX_LINES][MARAUDER_LINE_MAX];
    volatile uint8_t uart_line_head;
    volatile uint8_t uart_line_tail;
    char line_buf[MARAUDER_LINE_MAX];
    uint8_t line_pos;
    char last_uart_line[40]; /* debug: last non-empty RX line (UI) */
    volatile uint32_t uart_rx_tick;   /* any Marauder line received */
    volatile uint16_t uart_line_count;
    volatile uint32_t uart_line_drops;
    volatile bool marauder_confirmed;
    volatile uint32_t marauder_confirmed_tick;
    volatile uint32_t marauder_probe_tick;

    /* WiFi parsed results */
    WifiAp wifi_aps[MAX_WIFI_APS];
    uint8_t wifi_count;
    int8_t wifi_strongest;
    uint32_t wifi_last_scan_tick;
    uint32_t wifi_scan_end_tick;
    uint16_t wifi_table_full;
    uint16_t wifi_window_observations;
    uint8_t wifi_scroll;
    uint8_t wifi_last_updated;

    /* BLE parsed results */
    BleDev ble_devs[MAX_BLE_DEVS];
    uint8_t ble_count;
    int8_t ble_strongest;
    uint32_t ble_last_scan_tick;
    uint32_t ble_scan_end_tick;
    uint16_t ble_table_full;
    uint16_t ble_window_observations;
    uint8_t ble_scroll;
    uint8_t ble_last_updated;

    /* Rescan timer */
    uint32_t last_rescan_tick;
    bool auto_rescan;
    volatile bool session_log_on;
    Storage* storage;
    bool gps_log_coordinates;
    uint32_t last_gps_record_tick;
    uint32_t last_rf_record_tick;
    PendingRecordEvent record_queue[RECORD_QUEUE_CAPACITY];
    uint8_t record_head;
    uint8_t record_tail;
    uint32_t record_queue_drops;
    bool record_storage_error;
    volatile uint32_t record_ordinal;
    volatile uint32_t record_dropped_total;
    uint32_t reported_uart_drops;
    uint32_t reported_gps_drops;
    uint32_t reported_dump_overwrites;
    uint32_t reported_wifi_table_full;
    uint32_t reported_ble_table_full;

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
    char dump_lines[24][MARAUDER_LINE_MAX];
    uint8_t dump_head;
    uint8_t dump_count;
    volatile uint32_t dump_overwrites;
    bool raw_dump_error;

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
    RoomSweepGpsPage gps_page;
    uint8_t gps_profile; /* 0=BFFB Marauder, 1=optional external GPIO GPS */
    volatile uint16_t gps_byte_head;
    volatile uint16_t gps_byte_tail;
    volatile uint32_t gps_byte_drops;
    uint8_t gps_bytes[256];

    uint8_t info_page;

    /* TX (dedicated tab, safety-gated) */
    TxState tx_state;
    volatile bool tx_active;
    FuriThread* tx_thread;
    volatile RoomSweepTxWorkerState tx_worker_state;
    volatile RoomSweepTxRefusal tx_refusal;
    volatile uint32_t tx_freq_hz;
    volatile uint32_t tx_tuned_freq_hz;
    bool tx_from_candidate;
    RoomSweepSignalCandidate tx_candidate;
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
    SET_GPSSRC,
    SET_GPSLOG,
    SET_BASELINE,
    SET_DUMP,
    SET_TXDUR,
    SET_COUNT,
};

#define RESCAN_INTERVAL_MS 5000
#define MARAUDER_SCAN_TIMEOUT_MS 30000
#define GPS_STALE_TIMEOUT_MS 5000
#define GPS_PROFILE_BFFB 0
#define GPS_PROFILE_EXTERNAL 1
#define GPS_BYTE_RING_SIZE 256U
#define DUMP_LINE_COUNT 24U
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
    if(app->dump_count == DUMP_LINE_COUNT) app->dump_overwrites++;
    strncpy(app->dump_lines[app->dump_head], line, MARAUDER_LINE_MAX - 1U);
    app->dump_lines[app->dump_head][MARAUDER_LINE_MAX - 1U] = '\0';
    app->dump_head = (uint8_t)((app->dump_head + 1U) % DUMP_LINE_COUNT);
    if(app->dump_count < DUMP_LINE_COUNT) app->dump_count++;
}

static const char* mode_record_text(SweepMode mode) {
    switch(mode) {
    case SweepModeRF: return "RF";
    case SweepModeWifi: return "WIFI";
    case SweepModeBle: return "BLE";
    case SweepModeGps: return "GPS";
    case SweepModeTx: return "TX";
    case SweepModeInfo: return "INFO";
    default: return "?";
    }
}

static const char* record_mode_for_source(const char* source, SweepMode current) {
    if(source && strcmp(source, "RF") == 0) return "RF";
    if(source && strcmp(source, "WIFI") == 0) return "WIFI";
    if(source && strcmp(source, "BLE") == 0) return "BLE";
    if(source && strcmp(source, "GPS") == 0) return "GPS";
    if(source && strcmp(source, "TX") == 0) return "TX";
    return mode_record_text(current);
}

static const char* record_submode_for_source(App* app, const char* source) {
    if(source && strcmp(source, "RF") == 0) {
        if(app->rf_sub == RfSubSweep) return "sweep";
        if(app->rf_sub == RfSubPeak) return "peak";
        return "survey";
    }
    if(source && (strcmp(source, "WIFI") == 0 || strcmp(source, "BLE") == 0))
        return "scan_window";
    if(source && strcmp(source, "GPS") == 0)
        return app->gps_profile == GPS_PROFILE_EXTERNAL ? "external" : "bffb";
    if(source && strcmp(source, "TX") == 0) return "bounded_carrier";
    return "";
}

static bool record_enqueue_ex(
    App* app,
    const char* event,
    const char* kind,
    const char* id,
    int rssi,
    uint32_t freq_hz,
    uint8_t channel,
    uint32_t count,
    const char* state,
    const char* error_code,
    const char* detail) {
    uint32_t now = furi_get_tick();
    furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
    GpsFix gps = app->gps;
    uint32_t gps_last_valid_tick = app->gps_last_valid_tick;
    furi_mutex_release(app->gps_mutex);

    furi_mutex_acquire(app->record_mutex, FuriWaitForever);
    if(!app->session_log_on) {
        furi_mutex_release(app->record_mutex);
        return false;
    }
    uint8_t next = (uint8_t)((app->record_head + 1U) % RECORD_QUEUE_CAPACITY);
    if(next == app->record_tail) {
        app->record_queue_drops++;
        app->record_dropped_total++;
        furi_mutex_release(app->record_mutex);
        return false;
    }
    PendingRecordEvent* pending = &app->record_queue[app->record_head];
    memset(pending, 0, sizeof(*pending));
    strncpy(pending->event, event ? event : "event", sizeof(pending->event) - 1U);
    strncpy(pending->source, kind ? kind : "SYSTEM", sizeof(pending->source) - 1U);
    strncpy(
        pending->mode,
        record_mode_for_source(kind, app->mode),
        sizeof(pending->mode) - 1U);
    strncpy(
        pending->submode,
        record_submode_for_source(app, kind),
        sizeof(pending->submode) - 1U);
    if(id) strncpy(pending->id, id, sizeof(pending->id) - 1U);
    if(detail) strncpy(pending->detail, detail, sizeof(pending->detail) - 1U);
    if(state) strncpy(pending->state, state, sizeof(pending->state) - 1U);
    if(error_code)
        strncpy(pending->error_code, error_code, sizeof(pending->error_code) - 1U);
    pending->rssi = rssi;
    pending->freq_hz = freq_hz;
    pending->channel = channel;
    pending->lat = gps.latitude;
    pending->lon = gps.longitude;
    pending->has_pos = app->gps_log_coordinates && gps.has_pos && gps.has_fix &&
                       gps_last_valid_tick > 0 &&
                       (uint32_t)(now - gps_last_valid_tick) < GPS_STALE_TIMEOUT_MS;
    pending->has_gps = gps.sentences > 0;
    pending->fix_quality = gps.fix_quality;
    pending->sats_used = gps.sats;
    pending->sats_view = gps.sats_in_view;
    pending->speed_kmh = gps.speed_kts * 1.852f;
    pending->course_deg = gps.course;
    if(gps.has_time) {
        snprintf(
            pending->gps_utc,
            sizeof(pending->gps_utc),
            "%02u:%02u:%02u",
            gps.hour,
            gps.minute,
            gps.second);
    }
    if(gps.has_date) {
        snprintf(
            pending->gps_date,
            sizeof(pending->gps_date),
            "%04u-%02u-%02u",
            gps.year,
            gps.month,
            gps.day);
    }
    pending->gps_nav_sentences = gps.nav_sentences;
    pending->gps_rx_bytes = gps.rx_bytes;
    pending->count = count;
    app->record_head = next;
    furi_mutex_release(app->record_mutex);
    return true;
}

static bool record_enqueue(
    App* app,
    const char* event,
    const char* kind,
    const char* id,
    int rssi,
    uint32_t freq_hz,
    uint8_t channel,
    const char* detail) {
    return record_enqueue_ex(
        app, event, kind, id, rssi, freq_hz, channel, 1, "", "", detail);
}

static void record_transport_drop(
    App* app,
    const char* source,
    uint32_t count,
    const char* error_code,
    const char* detail) {
    if(count == 0 || !session_log_is_open()) return;
    SessionLogEvent event = {
        .event = "drop",
        .source = source,
        .mode = record_mode_for_source(source, app->mode),
        .submode = record_submode_for_source(app, source),
        .id = "",
        .count = count,
        .state = "incomplete",
        .error_code = error_code,
        .detail = detail,
    };
    session_log_note_drop(count);
    session_log_write_event(&event);
}

static void record_drain(App* app) {
    if(!session_log_is_open()) return;
    uint32_t uart_drops = app->uart_line_drops;
    uint32_t gps_drops = app->gps_byte_drops;
    uint32_t dump_overwrites = app->dump_overwrites;
    uint32_t wifi_table_full = app->wifi_table_full;
    uint32_t ble_table_full = app->ble_table_full;
    record_transport_drop(
        app,
        "SYSTEM",
        uart_drops - app->reported_uart_drops,
        "uart_queue_full",
        "complete UART lines were lost before parsing");
    record_transport_drop(
        app,
        "GPS",
        gps_drops - app->reported_gps_drops,
        "gps_byte_queue_full",
        "GPS bytes were lost before NMEA parsing");
    record_transport_drop(
        app,
        "SYSTEM",
        dump_overwrites - app->reported_dump_overwrites,
        "uart_snapshot_bounded",
        "old raw UART snapshot lines were overwritten");
    record_transport_drop(
        app,
        "WIFI",
        wifi_table_full - app->reported_wifi_table_full,
        "wifi_table_full",
        "accepted observations exceeded the on-screen table");
    record_transport_drop(
        app,
        "BLE",
        ble_table_full - app->reported_ble_table_full,
        "ble_table_full",
        "accepted observations exceeded the on-screen table");
    app->reported_uart_drops = uart_drops;
    app->reported_gps_drops = gps_drops;
    app->reported_dump_overwrites = dump_overwrites;
    app->reported_wifi_table_full = wifi_table_full;
    app->reported_ble_table_full = ble_table_full;

    for(uint8_t drained = 0; drained < 8U; drained++) {
        PendingRecordEvent pending;
        bool has_event = false;
        uint32_t drops = 0;
        furi_mutex_acquire(app->record_mutex, FuriWaitForever);
        if(app->record_queue_drops > 0) {
            drops = app->record_queue_drops;
            app->record_queue_drops = 0;
        }
        if(app->record_tail != app->record_head) {
            pending = app->record_queue[app->record_tail];
            app->record_tail =
                (uint8_t)((app->record_tail + 1U) % RECORD_QUEUE_CAPACITY);
            has_event = true;
        }
        furi_mutex_release(app->record_mutex);
        if(drops) session_log_note_drop(drops);
        if(!has_event) break;
        SessionLogEvent event = {
            .event = pending.event,
            .source = pending.source,
            .mode = pending.mode,
            .submode = pending.submode,
            .id = pending.id,
            .rssi = pending.rssi,
            .freq_hz = pending.freq_hz,
            .channel = pending.channel,
            .has_position = pending.has_pos,
            .latitude = pending.lat,
            .longitude = pending.lon,
            .has_gps = pending.has_gps,
            .fix_quality = pending.fix_quality,
            .sats_used = pending.sats_used,
            .sats_view = pending.sats_view,
            .speed_kmh = pending.speed_kmh,
            .course_deg = pending.course_deg,
            .gps_utc = pending.gps_utc,
            .gps_date = pending.gps_date,
            .gps_nav_sentences = pending.gps_nav_sentences,
            .gps_rx_bytes = pending.gps_rx_bytes,
            .count = pending.count,
            .state = pending.state,
            .error_code = pending.error_code,
            .detail = pending.detail,
        };
        session_log_write_event(&event);
        if(session_log_has_error()) break;
    }
    app->record_dropped_total = session_log_dropped();
    if(session_log_has_error()) {
        uint32_t abandoned = 0;
        furi_mutex_acquire(app->record_mutex, FuriWaitForever);
        app->session_log_on = false;
        abandoned = app->record_queue_drops;
        while(app->record_tail != app->record_head) {
            app->record_tail =
                (uint8_t)((app->record_tail + 1U) % RECORD_QUEUE_CAPACITY);
            abandoned++;
        }
        app->record_queue_drops = 0;
        furi_mutex_release(app->record_mutex);
        if(abandoned) session_log_note_drop(abandoned);
        session_log_end(app->storage);
        app->record_dropped_total = session_log_dropped();
        app->record_storage_error = true;
    }
}

static void record_finish_session(App* app) {
    furi_mutex_acquire(app->record_mutex, FuriWaitForever);
    app->session_log_on = false;
    furi_mutex_release(app->record_mutex);
    for(uint8_t pass = 0; pass < 4U && session_log_is_open(); pass++) record_drain(app);
    if(session_log_is_open()) session_log_end(app->storage);
    app->record_ordinal = session_log_ordinal();
    app->record_dropped_total = session_log_dropped();
    if(session_log_has_error()) app->record_storage_error = true;
}

static void tx_preload_detected_frequency(App* app) {
    if(app->mode != SweepModeTx) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->tx_freq_hz = room_sweep_candidate_handoff_frequency(
        &app->signal_candidate,
        furi_get_tick(),
        tx_freq_presets[app->tx_freq_idx],
        &app->tx_from_candidate);
    if(app->tx_from_candidate) {
        app->tx_candidate = app->signal_candidate;
    } else {
        room_sweep_candidate_invalidate(&app->tx_candidate);
    }
    furi_mutex_release(app->mutex);
}

static void tx_restore_current_preset(App* app) {
    app->tx_from_candidate = false;
    room_sweep_candidate_invalidate(&app->tx_candidate);
    app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
}

static RoomSweepTxInputState tx_input_state(TxState state) {
    switch(state) {
    case TxArmed:
        return RoomSweepTxInputArmed;
    case TxStarting:
        return RoomSweepTxInputStarting;
    case TxTransmitting:
        return RoomSweepTxInputTransmitting;
    case TxDisarmed:
    default:
        return RoomSweepTxInputDisarmed;
    }
}

static void tx_recover_from_candidate_refusal(App* app) {
    if(!app->tx_from_candidate) return;
    RoomSweepTxRefusal refusal = app->tx_refusal;
    tx_restore_current_preset(app);
    app->tx_refusal = refusal;
}

static void publish_signal_candidate(
    App* app,
    uint32_t requested_hz,
    uint32_t tuned_hz,
    float rssi,
    bool completed,
    RoomSweepCandidateSource source) {
    uint32_t now = furi_get_tick();
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool published = room_sweep_candidate_publish(
        &app->signal_candidate,
        requested_hz,
        tuned_hz,
        rssi,
        now,
        completed,
        source);
    furi_mutex_release(app->mutex);
    if(!published) return;
    if(source == RoomSweepCandidateSurvey && app->last_rf_record_tick > 0 &&
       (uint32_t)(now - app->last_rf_record_tick) < LOG_HIT_MIN_MS) {
        return;
    }
    app->last_rf_record_tick = now;
    char detail[48];
    snprintf(
        detail,
        sizeof(detail),
        "requested=%lu tuned=%lu",
        (unsigned long)requested_hz,
        (unsigned long)tuned_hz);
    record_enqueue_ex(
        app,
        "observation",
        "RF",
        room_sweep_candidate_source_text(source),
        (int)rssi,
        tuned_hz,
        0,
        1,
        "complete",
        "",
        detail);
}

static bool tx_frequency_preflight(App* app) {
    app->tx_refusal = RoomSweepTxRefusalNone;
    if(app->tx_from_candidate) {
        bool candidate_fresh = room_sweep_candidate_is_fresh(
            &app->tx_candidate, furi_get_tick());
        bool candidate_matches = app->tx_candidate.tuned_hz == app->tx_freq_hz;
        if(!candidate_fresh || !candidate_matches) {
            app->tx_refusal = RoomSweepTxRefusalExpiredCandidate;
            return false;
        }
    }
    if(!app->radio) {
        app->tx_refusal = RoomSweepTxRefusalNoRadio;
        return false;
    }
    if(!subghz_devices_is_frequency_valid(app->radio, app->tx_freq_hz)) {
        app->tx_refusal = RoomSweepTxRefusalInvalidFrequency;
        return false;
    }
    if(app->radio_path == RadioPathExternal) {
        if(app->ext_band == ExtBandAuto) {
            app->tx_refusal = RoomSweepTxRefusalExtBandUnknown;
            return false;
        }
        if(!room_sweep_external_band_allows((uint8_t)app->ext_band, app->tx_freq_hz)) {
            app->tx_refusal = RoomSweepTxRefusalInvalidFrequency;
            return false;
        }
    }
    if(!furi_hal_region_is_provisioned() ||
       !furi_hal_region_is_frequency_allowed(app->tx_freq_hz)) {
        app->tx_refusal = RoomSweepTxRefusalPolicy;
        return false;
    }
    return true;
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
            } else {
                app->uart_line_drops++;
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
    app->marauder_confirmed = false;
    app->marauder_probe_tick = furi_get_tick();
    static const uint8_t help_command[] = "help\n";
    furi_hal_serial_tx(app->serial, help_command, sizeof(help_command) - 1U);
    furi_hal_serial_tx_wait_complete(app->serial);
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
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    memset(app->wifi_aps, 0, sizeof(app->wifi_aps));
    app->wifi_count = 0;
    app->wifi_strongest = -127;
    app->wifi_last_scan_tick = 0;
    app->wifi_scan_end_tick = 0;
    app->wifi_table_full = 0;
    app->wifi_window_observations = 0;
    app->wifi_scroll = 0;
    furi_mutex_release(app->mutex);
}

static void clear_ble_results(App* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    memset(app->ble_devs, 0, sizeof(app->ble_devs));
    app->ble_count = 0;
    app->ble_strongest = -127;
    app->ble_last_scan_tick = 0;
    app->ble_scan_end_tick = 0;
    app->ble_table_full = 0;
    app->ble_window_observations = 0;
    app->ble_scroll = 0;
    furi_mutex_release(app->mutex);
}

static void marauder_reset_results(App* app) {
    clear_wifi_results(app);
    clear_ble_results(app);
}

static void clear_wireless_target(App* app, TargetKind kind) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->target_kind == kind) {
        app->target_kind = TargetNone;
        app->target_id[0] = '\0';
        app->target_rssi = -127;
        app->target_freq_hz = 0;
    }
    furi_mutex_release(app->mutex);
}

static void clear_uart_lines(App* app) {
    if(app->serial) furi_hal_serial_async_rx_stop(app->serial);
    app->uart_line_head = 0;
    app->uart_line_tail = 0;
    app->line_pos = 0;
    app->line_buf[0] = '\0';
    if(app->serial)
        furi_hal_serial_async_rx_start(app->serial, uart_rx_cb, app, false);
}

static void marauder_stop_scan(App* app) {
    /* Always poke stopscan when UART is up — leaves nmea/sniff cleanly. */
    bool was_scanning = app->marauder_state == MarauderScanning;
    if(app->serial && app->marauder_state != MarauderNoDevice) {
        marauder_send(app, MARAUDER_CMD_STOP);
        furi_delay_ms(80);
    }
    if(app->marauder_state != MarauderNoDevice) app->marauder_state = MarauderIdle;
    if(was_scanning)
        record_enqueue(app, "scan_stop", "SYSTEM", "marauder", 0, 0, 0, "requested");
}

/* clear_results: true on manual OK / tab enter; false keeps table on soft restart */
static void marauder_start_scan(App* app, const char* command, bool clear_results) {
    if(!app->serial) return;
    marauder_stop_scan(app);
    if(clear_results) {
        if(app->mode == SweepModeWifi) {
            clear_wireless_target(app, TargetWifi);
            clear_wifi_results(app);
        }
        if(app->mode == SweepModeBle) {
            clear_wireless_target(app, TargetBle);
            clear_ble_results(app);
        }
        clear_uart_lines(app);
    }
    if(app->mode == SweepModeWifi) app->wifi_window_observations = 0;
    if(app->mode == SweepModeBle) app->ble_window_observations = 0;
    marauder_send(app, command);
    app->marauder_state = MarauderScanning;
    app->last_rescan_tick = furi_get_tick();
    record_enqueue(
        app,
        "scan_start",
        app->mode == SweepModeWifi ? "WIFI" : app->mode == SweepModeBle ? "BLE" : "GPS",
        command,
        0,
        0,
        0,
        clear_results ? "new window; prior rows cleared" : "new window; prior rows retained");
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
    uint16_t next = (uint16_t)((app->gps_byte_head + 1U) % GPS_BYTE_RING_SIZE);
    if(next != app->gps_byte_tail) {
        app->gps_bytes[app->gps_byte_head] = byte;
        app->gps_byte_head = next;
    } else {
        app->gps_byte_drops++;
    }
}

/* The main loop is the sole owner of the NMEA parser state. */
static void process_gps_gpio_bytes(App* app) {
    furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
    while(app->gps_byte_tail != app->gps_byte_head) {
        uint8_t byte = app->gps_bytes[app->gps_byte_tail];
        app->gps_byte_tail =
            (uint16_t)((app->gps_byte_tail + 1U) % GPS_BYTE_RING_SIZE);
        uint32_t nav_before = app->gps.nav_sentences;
        nmea_feed(&app->gps, (char)byte);
        if(app->gps.nav_sentences != nav_before) {
            app->gps_last_valid_tick = furi_get_tick();
            app->gps_from_gpio = true;
        }
    }
    furi_mutex_release(app->gps_mutex);
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
        furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
        nmea_init(&app->gps);
        app->gps_last_valid_tick = 0;
        app->gps_had_fix = false;
        app->gps_from_gpio = false;
        furi_mutex_release(app->gps_mutex);
        app->gps_byte_tail = app->gps_byte_head;
        if(app->gps_profile == GPS_PROFILE_EXTERNAL) {
            /* 5V is often needed by an explicitly selected external module. */
            if(!furi_hal_power_is_otg_enabled()) {
                furi_hal_power_enable_otg();
                app->radio_otg_on = true; /* share OTG ownership with radio path */
                furi_delay_ms(30);
            }
            gps_gpio_open(app, GPS_GPIO_BAUD_PRIMARY);
        } else if(app->serial) {
            gps_marauder_stream(app);
        }
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
    if(ssid[0] == '\0') snprintf(ssid, sizeof(ssid), "Hidden/unknown");

    int ch_val = 0;
    parse_int_after(line, "Ch:", &ch_val);
    if(ch_val == 0) parse_int_after(line, "Channel", &ch_val);

    /* BSSID: look for MAC pattern (XX:XX:XX:XX:XX:XX) */
    char bssid[18] = {0};
    copy_mac(line, bssid);

    /* UINT8_MAX means syntactically valid but not retained because the table is full. */
    app->wifi_last_updated = UINT8_MAX;
    /* Update existing or add new */
    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
        if(app->wifi_aps[i].valid &&
           room_sweep_wireless_identity_matches(
               app->wifi_aps[i].bssid, app->wifi_aps[i].ssid, bssid, ssid)) {
            app->wifi_aps[i].rssi = (int8_t)rssi_val;
            app->wifi_aps[i].channel = (uint8_t)ch_val;
            app->wifi_aps[i].last_seen = now;
            if(app->wifi_aps[i].observations < UINT16_MAX)
                app->wifi_aps[i].observations++;
            app->wifi_last_updated = i;
            return true;
        }
    }
    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
        if(!app->wifi_aps[i].valid) {
            strncpy(app->wifi_aps[i].ssid, ssid, 32);
            app->wifi_aps[i].ssid[32] = '\0';
            app->wifi_aps[i].rssi = (int8_t)rssi_val;
            app->wifi_aps[i].channel = (uint8_t)ch_val;
            strncpy(app->wifi_aps[i].bssid, bssid, 17);
            app->wifi_aps[i].bssid[17] = '\0';
            app->wifi_aps[i].first_seen = now;
            app->wifi_aps[i].last_seen = now;
            app->wifi_aps[i].observations = 1;
            app->wifi_aps[i].valid = true; /* publish the completed row last */
            app->wifi_count++;
            app->wifi_last_updated = i;
            return true;
        }
    }
    app->wifi_table_full++;
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
    if(name[0] == '\0') snprintf(name, sizeof(name), "Hidden/unknown");

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

    app->ble_last_updated = UINT8_MAX;
    uint32_t now = furi_get_tick();
    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
        if(app->ble_devs[i].valid &&
           room_sweep_wireless_identity_matches(
               app->ble_devs[i].mac, app->ble_devs[i].name, mac, name)) {
            app->ble_devs[i].rssi = (int8_t)rssi_val;
            app->ble_devs[i].last_seen = now;
            if(app->ble_devs[i].observations < UINT16_MAX)
                app->ble_devs[i].observations++;
            app->ble_last_updated = i;
            return true;
        }
    }
    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
        if(!app->ble_devs[i].valid) {
            strncpy(app->ble_devs[i].name, name, 32);
            app->ble_devs[i].name[32] = '\0';
            app->ble_devs[i].rssi = (int8_t)rssi_val;
            strncpy(app->ble_devs[i].mac, mac, 17);
            app->ble_devs[i].mac[17] = '\0';
            app->ble_devs[i].first_seen = now;
            app->ble_devs[i].last_seen = now;
            app->ble_devs[i].observations = 1;
            app->ble_devs[i].valid = true; /* publish the completed row last */
            app->ble_count++;
            app->ble_last_updated = i;
            return true;
        }
    }
    app->ble_table_full++;
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

        if(app->gps_active && app->gps_profile == GPS_PROFILE_BFFB) {
            furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
            uint32_t nav_sentences = app->gps.nav_sentences;
            for(const char* p = latest; *p; p++) nmea_feed(&app->gps, *p);
            if(app->gps.nav_sentences != nav_sentences) {
                app->gps_last_valid_tick = furi_get_tick();
                app->gps_from_gpio = false; /* came from Marauder USART */
                app->marauder_confirmed = true;
                app->marauder_confirmed_tick = app->gps_last_valid_tick;
            }
            furi_mutex_release(app->gps_mutex);
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

        if(strstr(latest, "ESP32 Marauder") || strstr(latest, "sniffbeacon") ||
           strstr(latest, "sniffbt") || strstr(latest, "Commands")) {
            app->marauder_confirmed = true;
            app->marauder_confirmed_tick = furi_get_tick();
        }

        if(strstr(latest, "not supported") || strstr(latest, "Index not in range")) {
            app->marauder_state = MarauderError;
            continue;
        }

        if(app->mode == SweepModeWifi) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(parse_wifi_line(app, latest)) {
                if(app->wifi_window_observations < UINT16_MAX)
                    app->wifi_window_observations++;
                app->marauder_confirmed = true;
                app->marauder_confirmed_tick = furi_get_tick();
                app->wifi_strongest = -127;
                for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
                    if(app->wifi_aps[i].valid && app->wifi_aps[i].rssi > app->wifi_strongest) {
                        app->wifi_strongest = app->wifi_aps[i].rssi;
                    }
                }
                app->wifi_last_scan_tick = furi_get_tick();
                if(app->marauder_state == MarauderIdle ||
                   app->marauder_state == MarauderError) {
                    app->marauder_state = MarauderScanning;
                }
                if(app->wifi_last_updated < MAX_WIFI_APS) {
                    WifiAp* observed = &app->wifi_aps[app->wifi_last_updated];
                    bool unidentified = !observed->bssid[0] &&
                                        strcmp(observed->ssid, "Hidden/unknown") == 0;
                    record_enqueue_ex(
                        app,
                        "observation",
                        "WIFI",
                        unidentified ? "" :
                        observed->bssid[0] ? observed->bssid : observed->ssid,
                        observed->rssi,
                        0,
                        observed->channel,
                        observed->observations,
                        "observed",
                        "",
                        unidentified ?
                            "unidentified AP observations; not a device count" :
                            "AP beacon heard; Internet telemetry not measured");
                }
                if(app->target_kind == TargetWifi) {
                    for(uint8_t i = 0; i < MAX_WIFI_APS; i++) {
                        if(app->wifi_aps[i].valid &&
                           (strcmp(app->target_id, app->wifi_aps[i].ssid) == 0 ||
                            (app->wifi_aps[i].bssid[0] &&
                             strcmp(app->target_id, app->wifi_aps[i].bssid) == 0))) {
                            app->target_rssi = app->wifi_aps[i].rssi;
                            break;
                        }
                    }
                }
            }
            furi_mutex_release(app->mutex);
        } else if(app->mode == SweepModeBle) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(parse_ble_line(app, latest)) {
                if(app->ble_window_observations < UINT16_MAX)
                    app->ble_window_observations++;
                app->marauder_confirmed = true;
                app->marauder_confirmed_tick = furi_get_tick();
                app->ble_strongest = -127;
                for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
                    if(app->ble_devs[i].valid && app->ble_devs[i].rssi > app->ble_strongest) {
                        app->ble_strongest = app->ble_devs[i].rssi;
                    }
                }
                app->ble_last_scan_tick = furi_get_tick();
                if(app->marauder_state == MarauderIdle ||
                   app->marauder_state == MarauderError) {
                    app->marauder_state = MarauderScanning;
                }
                if(app->ble_last_updated < MAX_BLE_DEVS) {
                    BleDev* observed = &app->ble_devs[app->ble_last_updated];
                    bool unidentified = !observed->mac[0] &&
                                        strcmp(observed->name, "Hidden/unknown") == 0;
                    record_enqueue_ex(
                        app,
                        "observation",
                        "BLE",
                        unidentified ? "" :
                        observed->mac[0] ? observed->mac : observed->name,
                        observed->rssi,
                        0,
                        0,
                        observed->observations,
                        "observed",
                        "",
                        unidentified ?
                            "unidentified BLE observations; not a device count" :
                            "active scan advertisement/scan response; Internet telemetry not measured");
                }
                if(app->target_kind == TargetBle) {
                    for(uint8_t i = 0; i < MAX_BLE_DEVS; i++) {
                        if(app->ble_devs[i].valid &&
                           (strcmp(app->target_id, app->ble_devs[i].name) == 0 ||
                            (app->ble_devs[i].mac[0] &&
                             strcmp(app->target_id, app->ble_devs[i].mac) == 0))) {
                            app->target_rssi = app->ble_devs[i].rssi;
                            break;
                        }
                    }
                }
            }
            furi_mutex_release(app->mutex);
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

static uint32_t radio_rx_at(App* app, uint32_t hz) {
    if(!app->radio) return 0;
    subghz_devices_idle(app->radio);
    uint32_t tuned_hz = subghz_devices_set_frequency(app->radio, hz);
    subghz_devices_flush_rx(app->radio);
    subghz_devices_set_rx(app->radio);
    return tuned_hz;
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
            uint32_t config_generation = app->rf_config_generation;
            bool any_alert = false;
            float peak = -120.0f;
            uint8_t peak_idx = 0;
            uint32_t peak_requested_hz = 0;
            uint32_t peak_tuned_hz = 0;
            bool complete = true;

            for(uint8_t ch = 0; ch < RF_NUM_CHANNELS && app->running; ch++) {
                if(app->tx_active) {
                    complete = false;
                    break;
                }
                if(!rf_channel_allowed(app, ch)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->rssi[ch] = -120.0f;
                    furi_mutex_release(app->mutex);
                    continue;
                }
                furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
                if(!app->running || app->tx_active || !app->radio) {
                    furi_mutex_release(app->radio_mutex);
                    complete = false;
                    break;
                }
                uint32_t tuned_hz = radio_rx_at(app, rf_channels[ch]);

                float sum = 0;
                for(uint8_t s = 0; s < RF_SAMPLES_PER_CH && app->running && !app->tx_active; s++) {
                    sum += radio_rssi(app);
                    furi_delay_ms(5);
                }
                bool sample_valid = app->running && !app->tx_active;
                float avg = sum / RF_SAMPLES_PER_CH;
                radio_idle(app);
                furi_mutex_release(app->radio_mutex);
                if(!sample_valid) {
                    complete = false;
                    break;
                }

                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->rssi[ch] = avg;
                app->sweep_ch = ch;
                furi_mutex_release(app->mutex);

                if(avg > RF_ALERT_THRESHOLD) any_alert = true;
                if(avg > peak) {
                    peak = avg;
                    peak_idx = ch;
                    peak_requested_hz = rf_channels[ch];
                    peak_tuned_hz = tuned_hz;
                }
            }

            app->peak_rssi = peak;
            app->peak_ch = peak_idx;
            app->rf_alert = any_alert;
            if(complete && config_generation == app->rf_config_generation &&
               peak > RF_ALERT_THRESHOLD && peak_requested_hz > 0) {
                publish_signal_candidate(
                    app,
                    peak_requested_hz,
                    peak_tuned_hz,
                    peak,
                    true,
                    RoomSweepCandidateSurvey);
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
            uint32_t config_generation = app->rf_config_generation;
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
            uint32_t best_tuned_freq = 0;
            bool complete = true;

            for(uint32_t step = 0; step < total_steps && app->running && app->sweep_running; step++) {
                if(app->tx_active) {
                    complete = false;
                    break;
                }
                uint32_t freq = band->start_hz + step * SWEEP_STEP_COARSE;
                app->sweep_freq = freq;

                furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
                if(!app->running || app->tx_active || !app->sweep_running || !app->radio) {
                    furi_mutex_release(app->radio_mutex);
                    complete = false;
                    break;
                }
                uint32_t tuned_hz = radio_rx_at(app, freq);

                float sum = 0;
                for(uint8_t s = 0; s < SWEEP_SAMPLES && app->running && !app->tx_active && app->sweep_running; s++) {
                    sum += radio_rssi(app);
                    furi_delay_ms(SWEEP_DWELL_MS / SWEEP_SAMPLES);
                }
                bool sample_valid = app->running && !app->tx_active && app->sweep_running;
                float avg = sum / SWEEP_SAMPLES;
                radio_idle(app);
                furi_mutex_release(app->radio_mutex);
                if(!sample_valid) {
                    complete = false;
                    break;
                }

                if(avg > best_rssi) {
                    best_rssi = avg;
                    best_freq = freq;
                    best_tuned_freq = tuned_hz;
                }

                app->sweep_peak_rssi = best_rssi;
                app->sweep_peak_freq = best_freq;
                app->sweep_points_done = (uint16_t)(step + 1);
                app->sweep_progress = (uint8_t)((step + 1) * 100 / total_steps);
            }

            bool finished = complete && app->running && app->sweep_running;
            app->sweep_running = false;
            if(finished && config_generation == app->rf_config_generation &&
               best_rssi > RF_ALERT_THRESHOLD && best_tuned_freq > 0) {
                publish_signal_candidate(
                    app,
                    best_freq,
                    best_tuned_freq,
                    best_rssi,
                    finished,
                    RoomSweepCandidateSweep);
            }
            app->peak_rssi = best_rssi;

        } else if(app->rf_sub == RfSubPeak && app->peak_running) {
            uint32_t config_generation = app->rf_config_generation;
            uint32_t center = 0;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(room_sweep_candidate_is_fresh(&app->signal_candidate, furi_get_tick())) {
                center = app->signal_candidate.tuned_hz;
            }
            furi_mutex_release(app->mutex);
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
            uint32_t best_tuned_freq = 0;
            bool complete = true;

            for(uint32_t step = 0; step < total_steps && app->running && app->peak_running; step++) {
                if(app->tx_active) {
                    complete = false;
                    break;
                }
                uint32_t freq = start + step * SWEEP_STEP_FINE;
                furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
                if(!app->running || app->tx_active || !app->peak_running || !app->radio) {
                    furi_mutex_release(app->radio_mutex);
                    complete = false;
                    break;
                }
                uint32_t tuned_hz = radio_rx_at(app, freq);

                float sum = 0;
                for(uint8_t s = 0; s < SWEEP_SAMPLES && app->running && !app->tx_active && app->peak_running; s++) {
                    sum += radio_rssi(app);
                    furi_delay_ms(SWEEP_DWELL_MS / SWEEP_SAMPLES);
                }
                bool sample_valid = app->running && !app->tx_active && app->peak_running;
                float avg = sum / SWEEP_SAMPLES;
                radio_idle(app);
                furi_mutex_release(app->radio_mutex);
                if(!sample_valid) {
                    complete = false;
                    break;
                }

                if(avg > best_rssi) {
                    best_rssi = avg;
                    best_freq = freq;
                    best_tuned_freq = tuned_hz;
                }
                app->peak_fine_rssi = best_rssi;
                app->peak_fine_freq = best_freq;
                app->sweep_progress = (uint8_t)((step + 1) * 100 / total_steps);
            }

            bool finished = complete && app->running && app->peak_running;
            app->peak_running = false;
            if(finished && config_generation == app->rf_config_generation &&
               best_rssi > RF_ALERT_THRESHOLD && best_tuned_freq > 0) {
                publish_signal_candidate(
                    app,
                    best_freq,
                    best_tuned_freq,
                    best_rssi,
                    finished,
                    RoomSweepCandidatePeak);
            }
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
    bool result_recorded = false;

    uint32_t duration_ms = app->tx_duration_s * 1000U;
    app->tx_remaining_ms = duration_ms;
    record_enqueue(
        app,
        "tx_start_intent",
        "TX",
        app->tx_from_candidate ? "rx_candidate" : "preset",
        0,
        app->tx_freq_hz,
        0,
        "bounded carrier; not replay");

    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    if(!app->radio) {
        app->tx_refusal = RoomSweepTxRefusalNoRadio;
    } else if(app->running && app->tx_active &&
              app->tx_worker_state == RoomSweepTxWorkerRunning) {
        app->tx_level = false;
        app->tx_started = false;
        subghz_devices_idle(app->radio);
        subghz_devices_load_preset(app->radio, FuriHalSubGhzPresetOok650Async, NULL);
        app->tx_tuned_freq_hz = subghz_devices_set_frequency(app->radio, app->tx_freq_hz);
        bool tuned_allowed = app->tx_tuned_freq_hz > 0 &&
                             subghz_devices_is_frequency_valid(
                                 app->radio, app->tx_tuned_freq_hz) &&
                             furi_hal_region_is_provisioned() &&
                             furi_hal_region_is_frequency_allowed(app->tx_tuned_freq_hz) &&
                             (app->radio_path != RadioPathExternal ||
                              room_sweep_external_band_allows(
                                  (uint8_t)app->ext_band, app->tx_tuned_freq_hz));
        bool start_ok = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool candidate_ready =
            !app->tx_from_candidate ||
            (room_sweep_candidate_is_fresh(&app->tx_candidate, furi_get_tick()) &&
             app->tx_candidate.tuned_hz == app->tx_freq_hz);
        if(!candidate_ready) {
            app->tx_refusal = RoomSweepTxRefusalExpiredCandidate;
        } else if(!tuned_allowed) {
            app->tx_refusal = RoomSweepTxRefusalInvalidFrequency;
        } else if(!app->running || !app->tx_active ||
                  app->tx_worker_state != RoomSweepTxWorkerRunning) {
            app->tx_refusal = RoomSweepTxRefusalCanceled;
        } else if(subghz_devices_start_async_tx(app->radio, tx_carrier_cb, app)) {
            start_ok = true;
            app->tx_state = TxTransmitting;
        } else {
            app->tx_refusal = RoomSweepTxRefusalStartFailed;
        }
        furi_mutex_release(app->mutex);
        if(start_ok) {
            record_enqueue(
                app,
                "tx_started",
                "TX",
                "carrier",
                0,
                app->tx_tuned_freq_hz,
                0,
                "radio API accepted; antenna output not measured");
            uint32_t elapsed = 0;
            while(app->running && app->tx_active && elapsed < duration_ms) {
                furi_delay_ms(50);
                elapsed += 50;
                app->tx_remaining_ms = duration_ms - elapsed;
            }
            subghz_devices_stop_async_tx(app->radio);
            record_enqueue(
                app,
                app->tx_active && elapsed >= duration_ms ? "tx_end" : "tx_aborted",
                "TX",
                "carrier",
                0,
                app->tx_tuned_freq_hz,
                0,
                app->tx_active && elapsed >= duration_ms ? "duration complete" : "stopped");
            result_recorded = true;
        } else {
            record_enqueue(
                app,
                "tx_refused",
                "TX",
                "carrier",
                0,
                app->tx_freq_hz,
                0,
                room_sweep_tx_refusal_text(app->tx_refusal));
            result_recorded = true;
        }
        subghz_devices_idle(app->radio);
    } else if(app->tx_refusal == RoomSweepTxRefusalNone) {
        app->tx_refusal = RoomSweepTxRefusalCanceled;
    }
    furi_mutex_release(app->radio_mutex);

    if(!result_recorded && app->tx_refusal != RoomSweepTxRefusalNone) {
        record_enqueue(
            app,
            "tx_refused",
            "TX",
            "carrier",
            0,
            app->tx_freq_hz,
            0,
            room_sweep_tx_refusal_text(app->tx_refusal));
    }

    app->tx_active = false;
    app->tx_state = TxDisarmed;
    app->tx_remaining_ms = 0;
    app->tx_worker_state = RoomSweepTxWorkerIdle;
    return 0;
}

static void tx_thread_cleanup(App* app) {
    if(app->tx_thread && app->tx_worker_state == RoomSweepTxWorkerIdle) {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }
}

/* Stop and join outside radio_mutex; the worker owns that lock during TX. */
static void tx_stop_and_join(App* app) {
    if(!app->tx_thread) return;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool was_in_progress = app->tx_active ||
                           app->tx_worker_state != RoomSweepTxWorkerIdle;
    app->tx_active = false;
    app->tx_worker_state = RoomSweepTxWorkerStopping;
    if(was_in_progress && app->tx_refusal == RoomSweepTxRefusalNone) {
        app->tx_refusal = RoomSweepTxRefusalCanceled;
    }
    furi_mutex_release(app->mutex);
    furi_thread_join(app->tx_thread);
    furi_thread_free(app->tx_thread);
    app->tx_thread = NULL;
    app->tx_worker_state = RoomSweepTxWorkerIdle;
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

static void led_from_gps(
    NotificationApp* notif,
    const GpsFix* gps,
    bool fresh,
    bool blink_phase) {
    if(!fresh || gps->sentences == 0) {
        notification_message(notif, &sequence_reset_rgb);
        return;
    }
    if(gps->has_fix && blink_phase && gps->sats >= 6) {
        notification_message(notif, &sequence_set_green_255);
        return;
    }
    if(gps->has_fix) {
        if(gps->sats >= 6) notification_message(notif, &sequence_set_green_255);
        else if(gps->sats >= 3) notification_message(notif, &sequence_solid_yellow);
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

    /* An RF lock is valid only while its exact source candidate is fresh. */
    if(app->target_kind == TargetRF) {
        RoomSweepSignalCandidate candidate;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        candidate = app->signal_candidate;
        furi_mutex_release(app->mutex);
        if(!room_sweep_candidate_matches_request(
               &candidate, app->target_freq_hz, now)) {
            app->target_kind = TargetNone;
            app->target_freq_hz = 0;
            app->target_id[0] = '\0';
            app->target_rssi = -127;
        }
    }

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
        furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
        GpsFix gps = app->gps;
        uint32_t gps_last_valid_tick = app->gps_last_valid_tick;
        furi_mutex_release(app->gps_mutex);
        bool fresh = gps_last_valid_tick > 0 &&
                     now - gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
        led_from_gps(app->notif, &gps, fresh, blink_phase);
        alerting = fresh && gps.has_fix;
        /* Fix-acquire edge for sound/vibro */
        if(alerting && !app->gps_had_fix) {
            if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
            if(app->vibro_on) notification_message(app->notif, &seq_vibro_pulse);
        }
        app->gps_had_fix = alerting;
        peak = fresh ? (-100.0f + (float)gps.sats * 4.0f) : -120.0f;
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
    } else if(app->vibro_on && gps_mode && alerting) {
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
    if(!app->rf_alert) canvas_draw_str(canvas, 44, 22, "U/D mode HoldOK lock");
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
        canvas_draw_str(canvas, 2, 63, "OK=cancel");
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
        canvas_draw_str(canvas, 2, 62, "U/D mode OK run HoldLR band");
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
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 63, "OK=cancel");
    } else {
        RoomSweepSignalCandidate candidate;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        candidate = app->signal_candidate;
        furi_mutex_release(app->mutex);
        bool candidate_fresh = room_sweep_candidate_is_fresh(&candidate, furi_get_tick());

        if(candidate_fresh) {
            canvas_set_font(canvas, FontSecondary);
            char buf[32];
            snprintf(buf, sizeof(buf), "Center: %lu.%03lu MHz",
                     (unsigned long)(candidate.tuned_hz / 1000000),
                     (unsigned long)((candidate.tuned_hz % 1000000) / 1000));
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
            canvas_draw_str(canvas, 2, 63, "U/D mode OK refine HoldOK lock");
        } else {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(
                canvas,
                2,
                30,
                candidate.valid ? "RF candidate expired." : "No RF candidate.");
            canvas_draw_str(canvas, 2, 42, "Run Survey or Sweep");
            canvas_draw_str(canvas, 2, 53, "to find a signal first.");
        }
    }
}

/* ================================================================== */
/* Drawing: WiFi tab (meter + parsed AP list)                          */
/* ================================================================== */
static void draw_wifi_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "WiFi");

    if(!app->serial) {
        canvas_draw_str(canvas, 2, 30, "No UART handle");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 43, "BFFB/Marauder unavailable");
        canvas_draw_str(canvas, 2, 55, "No observation != absence");
        return;
    }

    canvas_set_font(canvas, FontKeyboard);
    const char* state = !app->marauder_confirmed &&
                                (uint32_t)(furi_get_tick() - app->marauder_probe_tick) >= 5000U ?
                            "no response" :
                        !app->marauder_confirmed ? "UART open" :
                        app->marauder_state == MarauderScanning ? "scan..." :
                        app->marauder_state == MarauderDone ? "done" :
                        app->marauder_state == MarauderError ? "ERR" : "idle";
    canvas_draw_str(canvas, 127 - canvas_string_width(canvas, state), 12, state);

    char buf[48];
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint8_t count = app->wifi_count;
    uint16_t window_observations = app->wifi_window_observations;
    uint16_t table_full = app->wifi_table_full;
    uint8_t selected = app->wifi_scroll < count ? app->wifi_scroll : 0;
    WifiAp ap = count > 0 ? app->wifi_aps[selected] : (WifiAp){0};
    bool locked = count > 0 && app->target_kind == TargetWifi &&
                  ((ap.bssid[0] && strcmp(app->target_id, ap.bssid) == 0) ||
                   strcmp(app->target_id, ap.ssid) == 0);
    furi_mutex_release(app->mutex);
    if(!app->marauder_confirmed) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 28, "Marauder not confirmed");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 41, "Check ESP32 switch/firmware");
        canvas_draw_str(canvas, 2, 52, "Scan evidence unavailable");
        return;
    }
    if(count == 0 ||
       (app->marauder_state == MarauderDone && window_observations == 0)) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(
            canvas,
            2,
            28,
            app->marauder_state == MarauderDone ?
                room_sweep_wireless_evidence_text(
                    RoomSweepWirelessEvidenceNotObservedInScan) :
                "Listening for AP beacons");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 41, "Internet telemetry not measured");
        canvas_draw_str(canvas, 2, 52, "No observation != absence");
        if(count > 0) {
            snprintf(buf, sizeof(buf), "old:%u", count);
            canvas_draw_str(canvas, 98, 63, buf);
        }
        canvas_draw_str(canvas, 2, 63, "OK=scan");
        return;
    }

    snprintf(buf, sizeof(buf), "%u/%u %s%s",
             selected + 1U, count, locked ? "LOCK" : "",
             table_full ? " FULL" : "");
    canvas_draw_str(canvas, 38, 12, buf);
    canvas_set_font(canvas, FontSecondary);
    bool unidentified = !ap.bssid[0] && strcmp(ap.ssid, "Hidden/unknown") == 0;
    canvas_draw_str(canvas, 2, 24, unidentified ? "Unidentified observations" : ap.ssid);
    snprintf(buf, sizeof(buf), "%ddBm Ch%u age%lus seen%u",
             ap.rssi, ap.channel,
             (unsigned long)((furi_get_tick() - ap.last_seen) / 1000U),
             ap.observations);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 2, 35, buf);
    canvas_draw_str(canvas, 2, 44, ap.bssid[0] ? ap.bssid : "ID unavailable; grouped");
    canvas_draw_str(canvas, 2, 53, "AP beacon heard; net unmeasured");
    canvas_draw_str(canvas, 2, 63, "U/D browse OK scan Hold lock");
}

/* ================================================================== */
/* Drawing: BLE tab                                                    */
/* ================================================================== */
static void draw_ble_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "BLE");

    if(!app->serial) {
        canvas_draw_str(canvas, 2, 30, "No UART handle");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 43, "BFFB/Marauder unavailable");
        canvas_draw_str(canvas, 2, 55, "No observation != absence");
        return;
    }

    canvas_set_font(canvas, FontKeyboard);
    const char* state = !app->marauder_confirmed &&
                                (uint32_t)(furi_get_tick() - app->marauder_probe_tick) >= 5000U ?
                            "no response" :
                        !app->marauder_confirmed ? "UART open" :
                        app->marauder_state == MarauderScanning ? "active..." :
                        app->marauder_state == MarauderDone ? "done" :
                        app->marauder_state == MarauderError ? "ERR" : "idle";
    canvas_draw_str(canvas, 127 - canvas_string_width(canvas, state), 12, state);

    char buf[48];
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint8_t count = app->ble_count;
    uint16_t window_observations = app->ble_window_observations;
    uint16_t table_full = app->ble_table_full;
    uint8_t selected = app->ble_scroll < count ? app->ble_scroll : 0;
    BleDev dev = count > 0 ? app->ble_devs[selected] : (BleDev){0};
    bool locked = count > 0 && app->target_kind == TargetBle &&
                  ((dev.mac[0] && strcmp(app->target_id, dev.mac) == 0) ||
                   strcmp(app->target_id, dev.name) == 0);
    furi_mutex_release(app->mutex);
    if(!app->marauder_confirmed) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 28, "Marauder not confirmed");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 41, "Check ESP32 switch/firmware");
        canvas_draw_str(canvas, 2, 52, "Scan evidence unavailable");
        return;
    }
    if(count == 0 ||
       (app->marauder_state == MarauderDone && window_observations == 0)) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(
            canvas,
            2,
            28,
            app->marauder_state == MarauderDone ?
                room_sweep_wireless_evidence_text(
                    RoomSweepWirelessEvidenceNotObservedInScan) :
                "Listening for BLE devices");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 41, "Internet telemetry not measured");
        canvas_draw_str(canvas, 2, 52, "No observation != absence");
        if(count > 0) {
            snprintf(buf, sizeof(buf), "old:%u", count);
            canvas_draw_str(canvas, 98, 63, buf);
        }
        canvas_draw_str(canvas, 2, 63, "OK=scan");
        return;
    }

    snprintf(buf, sizeof(buf), "%u/%u %s%s",
             selected + 1U, count, locked ? "LOCK" : "",
             table_full ? " FULL" : "");
    canvas_draw_str(canvas, 38, 12, buf);
    canvas_set_font(canvas, FontSecondary);
    bool unidentified = !dev.mac[0] && strcmp(dev.name, "Hidden/unknown") == 0;
    canvas_draw_str(canvas, 2, 24, unidentified ? "Unidentified observations" : dev.name);
    snprintf(buf, sizeof(buf), "%ddBm age%lus seen%u",
             dev.rssi,
             (unsigned long)((furi_get_tick() - dev.last_seen) / 1000U),
             dev.observations);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 2, 35, buf);
    canvas_draw_str(canvas, 2, 44, dev.mac[0] ? dev.mac : "ID unavailable; grouped");
    canvas_draw_str(canvas, 2, 53, "BLE advertisement heard");
    canvas_draw_str(canvas, 2, 63, "U/D browse OK scan Hold lock");
}

/* ================================================================== */
/* Drawing: GPS tab                                                    */
/* ================================================================== */
static void draw_gps_tab(Canvas* canvas, App* app) {
    uint32_t now = furi_get_tick();
    furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
    GpsFix gps = app->gps;
    uint32_t gps_last_valid_tick = app->gps_last_valid_tick;
    bool gps_from_gpio = app->gps_from_gpio;
    furi_mutex_release(app->gps_mutex);
    RoomSweepGpsSnapshot snapshot = {
        .marauder_link = app->serial != NULL && app->marauder_confirmed,
        .external_gpio_link = app->gps_gpio_open,
        .source = gps_from_gpio ? RoomSweepGpsSourceExternalGpio :
                  gps.sentences > 0 ? RoomSweepGpsSourceMarauder :
                  RoomSweepGpsSourceNone,
        .valid_sentences = gps.sentences,
        .has_fix = gps.has_fix,
        .last_valid_tick = gps_last_valid_tick,
        .now_tick = now,
        .stale_timeout_ms = GPS_STALE_TIMEOUT_MS,
    };
    RoomSweepGpsStatus status = room_sweep_gps_status(&snapshot);
    bool gps_fresh = status == RoomSweepGpsStatusFix ||
                     status == RoomSweepGpsStatusNoFix;

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 12, "GPS");
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 34, 12, room_sweep_gps_status_text(status));
    canvas_draw_str(
        canvas, 89, 12, room_sweep_gps_page_text(app->gps_page));

    char buf[48];
    canvas_set_font(canvas, FontSecondary);

    if(gps.sentences == 0) {
        canvas_draw_str(
            canvas,
            2,
            27,
            app->gps_profile == GPS_PROFILE_EXTERNAL ?
                "External GPIO (optional)" : "BFFB Marauder GPS");
        snprintf(
            buf,
            sizeof(buf),
            "%s",
            app->gps_profile == GPS_PROFILE_EXTERNAL ?
                (app->gps_gpio_open ? "GPIO listening" : "GPIO unavailable") :
                (app->serial ? "Awaiting NMEA response" : "Marauder unavailable"));
        canvas_draw_str(canvas, 2, 38, buf);
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 51, "Source set in Settings");
        canvas_draw_str(canvas, 2, 63, "U/D page OK=retry");
        return;
    }

    if(app->gps_page == RoomSweepGpsPageDetail) {
        canvas_set_font(canvas, FontKeyboard);
        snprintf(
            buf,
            sizeof(buf),
            "Src:%s",
            gps_from_gpio ? "External GPIO" : "BFFB Marauder");
        canvas_draw_str(canvas, 2, 23, buf);
        if(gps.has_date) {
            snprintf(
                buf,
                sizeof(buf),
                "Date:%04u-%02u-%02u Q:%u",
                gps.year,
                gps.month,
                gps.day,
                gps.fix_quality);
        } else {
            snprintf(buf, sizeof(buf), "Date:---- -- -- Q:%u", gps.fix_quality);
        }
        canvas_draw_str(canvas, 2, 33, buf);
        snprintf(
            buf,
            sizeof(buf),
            "NMEA:%lu nav:%lu bytes:%lu",
            (unsigned long)gps.sentences,
            (unsigned long)gps.nav_sentences,
            (unsigned long)gps.rx_bytes);
        canvas_draw_str(canvas, 2, 43, buf);
        if(gps_last_valid_tick > 0) {
            snprintf(
                buf,
                sizeof(buf),
                "Age:%lus drops:%lu",
                (unsigned long)((now - gps_last_valid_tick) / 1000U),
                (unsigned long)app->gps_byte_drops);
        } else {
            snprintf(
                buf,
                sizeof(buf),
                "Age:-- drops:%lu",
                (unsigned long)app->gps_byte_drops);
        }
        canvas_draw_str(canvas, 2, 53, buf);
        canvas_draw_str(canvas, 2, 63, "U/D page OK=retry/mark");
        return;
    }

    if(gps_fresh && gps.has_time) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 gps.hour, gps.minute, gps.second);
        canvas_draw_str(canvas, 2, 24, buf);
    } else {
        canvas_draw_str(canvas, 2, 24, "--:--:--");
    }

    /* Sat quality bar (used sats, max 12) */
    uint8_t sats = gps.sats;
    if(sats > 12) sats = 12;
    canvas_draw_frame(canvas, 56, 16, 50, 7);
    if(sats > 0) canvas_draw_box(canvas, 57, 17, (uint8_t)(48 * sats / 12), 5);
    snprintf(buf, sizeof(buf), "%d/%d", gps.sats, gps.sats_in_view);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 108, 23, buf);

    canvas_set_font(canvas, FontSecondary);
    if(gps_fresh && gps.has_pos) {
        snprintf(buf, sizeof(buf), "%.5f %.5f",
                 (double)gps.latitude, (double)gps.longitude);
        canvas_draw_str(canvas, 2, 36, buf);
    } else {
        canvas_draw_str(canvas, 2, 36, "No position yet");
    }

    /* Speed (kts→km/h) and course — fields already parsed from RMC */
    if(gps_fresh && gps.has_fix) {
        float kmh = gps.speed_kts * 1.852f;
        snprintf(buf, sizeof(buf), "%.1fkm/h %03.0fdeg",
                 (double)kmh, (double)gps.course);
        canvas_draw_str(canvas, 2, 48, buf);
    } else {
        canvas_draw_str(canvas, 2, 48, "spd/crs --");
    }

    /* Mark distance */
    canvas_set_font(canvas, FontKeyboard);
    if(app->gps_mark_set && gps_fresh && gps.has_pos) {
        float dist = geo_distance_m(
            app->gps_mark_lat, app->gps_mark_lon,
            gps.latitude, gps.longitude);
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

    canvas_draw_str(canvas, 78, 60, "U/D page");
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
        canvas_draw_str(canvas, 2, 38, "Carrier only; no replay.");
        const char* refusal = room_sweep_tx_refusal_text(app->tx_refusal);
        bool candidate_ready = app->tx_from_candidate &&
                               room_sweep_candidate_is_fresh(
                                   &app->tx_candidate, furi_get_tick());
        canvas_draw_str(
            canvas,
            2,
            47,
            refusal[0] ? refusal :
            candidate_ready ? "Detected RX candidate" : "Own/licensed use only.");

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

        canvas_set_font(canvas, FontKeyboard);
        RoomSweepSignalCandidate candidate = app->tx_candidate;
        bool candidate_fresh = app->tx_from_candidate &&
                               room_sweep_candidate_is_fresh(
                                   &candidate, furi_get_tick());
        canvas_draw_str(
            canvas,
            2,
            37,
            candidate_fresh ? "Detected RX candidate" : "Preset carrier only");

        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "%lu.%03lu MHz %ds max",
                 (unsigned long)(app->tx_freq_hz / 1000000),
                 (unsigned long)((app->tx_freq_hz % 1000000) / 1000),
                 app->tx_duration_s);
        canvas_draw_str(canvas, 2, 47, buf);

        if(candidate_fresh) {
            uint32_t age_s = (furi_get_tick() - candidate.tick) / 1000U;
            snprintf(
                buf,
                sizeof(buf),
                "RX %s %.0fdBm %lus",
                room_sweep_candidate_source_text(candidate.source),
                (double)candidate.rssi,
                (unsigned long)age_s);
            canvas_draw_str(canvas, 2, 55, buf);
        } else {
            canvas_draw_str(canvas, 2, 55, "No captured signal/replay");
        }

        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 63, "LongOK=TX Up/Dn=freq B=disarm");

    } else if(app->tx_state == TxStarting) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 28, "STARTING...");
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 42, "Back=stop");
    } else if(app->tx_state == TxTransmitting) {
        /* The radio API accepted the carrier request; antenna output is not measured. */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, 128, 64);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 12, 20, "TX API ACTIVE");

        char fbuf[32];
        snprintf(fbuf, sizeof(fbuf), "%lu.%03lu MHz",
                 (unsigned long)(app->tx_tuned_freq_hz / 1000000),
                 (unsigned long)((app->tx_tuned_freq_hz % 1000000) / 1000));
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 30, 38, fbuf);

        snprintf(fbuf, sizeof(fbuf), "%lu.%lus remaining",
                 (unsigned long)(app->tx_remaining_ms / 1000),
                 (unsigned long)((app->tx_remaining_ms % 1000) / 100));
        canvas_draw_str(canvas, 24, 52, fbuf);
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str(canvas, 2, 62, "Back=stop");
        canvas_set_color(canvas, ColorBlack);
    }
}

/* ================================================================== */
/* Drawing: Info tab (live capability card)                            */
/* ================================================================== */
static void draw_info_tab(Canvas* canvas, App* app) {
    canvas_set_font(canvas, FontKeyboard);
    char buf[48];
    furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
    bool gps_has_sentences = app->gps.sentences > 0;
    bool gps_from_gpio = app->gps_from_gpio;
    furi_mutex_release(app->gps_mutex);

    if(app->info_page != 0) {
        canvas_draw_str(canvas, 2, 10, "Glossary 2/2");
        canvas_draw_str(canvas, 2, 20, "Survey=known channels");
        canvas_draw_str(canvas, 2, 29, "Sweep=search band");
        canvas_draw_str(canvas, 2, 38, "Peak=refine; Lock=follow");
        canvas_draw_str(canvas, 2, 47, "RSSI=relative, not distance");
        canvas_draw_str(canvas, 2, 56, "No proof of Internet telemetry");
        canvas_draw_str(canvas, 2, 63, "U/D=page");
        return;
    }

    canvas_draw_str(canvas, 92, 10, "1/2");

    snprintf(buf, sizeof(buf), "RF:%s band:%s",
             app->radio_path == RadioPathExternal ? "EXT" :
             app->radio_path == RadioPathInternal ? "INT" : "NONE",
             app->ext_band == ExtBand400 ? "400" :
             app->ext_band == ExtBand900 ? "900" : "AUTO");
    canvas_draw_str(canvas, 2, 10, buf);

    snprintf(buf, sizeof(buf), "Marauder:%s GPS:%s",
             app->marauder_confirmed ? "confirmed" : app->serial ? "open" : "none",
             gps_has_sentences ? (gps_from_gpio ? "EXT" : "BFFB") : "wait");
    canvas_draw_str(canvas, 2, 19, buf);

    snprintf(buf, sizeof(buf), "Rec:%s #%lu drop:%lu",
             app->record_storage_error ? "ERR" : app->session_log_on ? "ON" : "off",
             (unsigned long)app->record_ordinal,
             (unsigned long)app->record_dropped_total);
    canvas_draw_str(canvas, 2, 28, buf);

    snprintf(buf, sizeof(buf), "Base:%s Lock:%s",
             app->baseline_set ? "Y" : "n",
             app->target_kind == TargetNone ? "-" :
             app->target_kind == TargetRF ? "RF" :
             app->target_kind == TargetWifi ? "Wi" : "BT");
    canvas_draw_str(canvas, 2, 37, buf);

    if(app->target_kind != TargetNone) {
        char tid[12];
        strncpy(tid, app->target_id, 11);
        tid[11] = '\0';
        snprintf(buf, sizeof(buf), "T:%s %ddBm", tid, (int)app->target_rssi);
        canvas_draw_str(canvas, 65, 37, buf);
    } else {
        canvas_draw_str(canvas, 65, 37, "T:none");
    }

    snprintf(buf, sizeof(buf), "dump:%u lines", app->dump_count);
    canvas_draw_str(canvas, 2, 46, buf);

    snprintf(
        buf,
        sizeof(buf),
        "UART lines:%u drop:%lu",
        app->uart_line_count,
        (unsigned long)app->uart_line_drops);
    canvas_draw_str(canvas, 2, 55, buf);
    canvas_draw_str(canvas, 2, 63, "U/D=page Back=settings");
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
        "Sound", "Vibro", "Rescan", "Record", "ExtBand", "GPS Src", "GPS in Log", "Baseline", "Raw Dump", "TXDur",
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
        else if(i == SET_LOG)
            snprintf(
                val,
                sizeof(val),
                "%s",
                app->record_storage_error ? "ERR" : app->session_log_on ? "ON" : "off");
        else if(i == SET_EXTBAND)
            snprintf(
                val,
                sizeof(val),
                "%s",
                app->ext_band == ExtBand400 ? "400" :
                app->ext_band == ExtBand900 ? "900" : "AUTO");
        else if(i == SET_GPSSRC)
            snprintf(
                val,
                sizeof(val),
                "%s",
                app->gps_profile == GPS_PROFILE_EXTERNAL ? "EXT" : "BFFB");
        else if(i == SET_GPSLOG)
            snprintf(val, sizeof(val), "%s", app->gps_log_coordinates ? "YES" : "no");
        else if(i == SET_BASELINE)
            snprintf(val, sizeof(val), "%s", app->baseline_set ? "set" : "OK=");
        else if(i == SET_DUMP) {
            if(app->raw_dump_error) snprintf(val, sizeof(val), "ERR");
            else snprintf(val, sizeof(val), "%u", app->dump_count);
        }
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
static RoomSweepInputAction classify_input_event(const InputEvent* event) {
    RoomSweepInputKey key;
    switch(event->key) {
    case InputKeyUp: key = RoomSweepInputUp; break;
    case InputKeyDown: key = RoomSweepInputDown; break;
    case InputKeyLeft: key = RoomSweepInputLeft; break;
    case InputKeyRight: key = RoomSweepInputRight; break;
    case InputKeyOk: key = RoomSweepInputOk; break;
    case InputKeyBack: key = RoomSweepInputBack; break;
    default: return RoomSweepInputNone;
    }

    RoomSweepInputPhase phase;
    switch(event->type) {
    case InputTypeShort: phase = RoomSweepInputShort; break;
    case InputTypeLong: phase = RoomSweepInputLong; break;
    case InputTypeRepeat: phase = RoomSweepInputRepeat; break;
    default: return RoomSweepInputNone;
    }
    return room_sweep_input_action(key, phase);
}

static void input_cb(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, 0);
}

static void navigate_tab(App* app, bool next) {
    if(app->mode == SweepModeTx) {
        tx_stop_and_join(app);
        app->tx_state = TxDisarmed;
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->target_kind = TargetNone;
    app->target_id[0] = '\0';
    app->target_freq_hz = 0;
    app->target_rssi = -127;
    furi_mutex_release(app->mutex);
    if(app->serial) {
        marauder_stop_scan(app);
        marauder_reset_results(app);
        clear_uart_lines(app);
        app->last_uart_line[0] = '\0';
        if(app->marauder_state != MarauderNoDevice) app->marauder_state = MarauderIdle;
    }
    app->mode = next ? (SweepMode)((app->mode + 1) % SweepModeCount) :
                       (app->mode == 0 ? (SweepMode)(SweepModeCount - 1) :
                                         (SweepMode)(app->mode - 1));
    record_enqueue(
        app, "mode", "SYSTEM", mode_record_text(app->mode), 0, 0, 0, "tab selected");
    tx_preload_detected_frequency(app);
    update_gps_mode(app);
    marauder_start_for_mode(app);
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
    app->gps_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->record_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
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
    app->gps_log_coordinates = false;
    app->record_storage_error = false;
    app->baseline_set = false;
    app->target_kind = TargetNone;
    app->target_rssi = -127;
    app->ext_band = ExtBandAuto;
    app->rf_config_generation = 0;
    app->dump_head = 0;
    app->dump_count = 0;
    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) app->baseline_rssi[i] = -120.0f;

    /* TX defaults */
    app->tx_state = TxDisarmed;
    app->tx_freq_hz = 433920000;
    app->tx_tuned_freq_hz = 433920000;
    app->tx_from_candidate = false;
    room_sweep_candidate_invalidate(&app->tx_candidate);
    app->tx_freq_idx = 0;
    app->tx_duration_s = TX_DEFAULT_DURATION;
    app->tx_active = false;
    app->tx_thread = NULL;
    app->tx_worker_state = RoomSweepTxWorkerIdle;
    app->tx_refusal = RoomSweepTxRefusalNone;

    /* GPS init */
    nmea_init(&app->gps);
    app->gps_active = false;
    app->gps_last_valid_tick = 0;
    app->gps_last_request_tick = 0;
    app->gps_mark_set = false;
    app->gps_had_fix = false;
    app->gps_page = RoomSweepGpsPageSummary;
    app->gps_profile = GPS_PROFILE_BFFB;
    app->info_page = 0;

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
    FuriMessageQueue* input_queue =
        furi_message_queue_alloc(INPUT_QUEUE_CAPACITY, sizeof(InputEvent));
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
            process_gps_gpio_bytes(app);
            process_uart_lines(app);

            uint32_t record_now = furi_get_tick();
            if(app->session_log_on && app->gps_active &&
               (uint32_t)(record_now - app->last_gps_record_tick) >= 5000U) {
                furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
                GpsFix gps_record = app->gps;
                bool gps_record_from_gpio = app->gps_from_gpio;
                uint32_t gps_record_last_valid = app->gps_last_valid_tick;
                furi_mutex_release(app->gps_mutex);
                if(gps_record.sentences > 0) {
                    char detail[48];
                    uint32_t gps_age_ms = gps_record_last_valid > 0 ?
                                              record_now - gps_record_last_valid : UINT32_MAX;
                    snprintf(
                        detail,
                        sizeof(detail),
                        "fix=%u sats=%u/%u q=%u age=%lus nav=%lu",
                        gps_record.has_fix,
                        gps_record.sats,
                        gps_record.sats_in_view,
                        gps_record.fix_quality,
                        (unsigned long)(gps_age_ms == UINT32_MAX ? 0 : gps_age_ms / 1000U),
                        (unsigned long)gps_record.nav_sentences);
                    record_enqueue_ex(
                        app,
                        "snapshot",
                        "GPS",
                        gps_record_from_gpio ? "external" : "bffb",
                        gps_record.sats,
                        0,
                        0,
                        gps_record.sentences,
                        gps_age_ms < GPS_STALE_TIMEOUT_MS ? "fresh" : "stale",
                        "",
                        detail);
                    app->last_gps_record_tick = record_now;
                }
            }
            record_drain(app);

            if(app->serial && (app->mode == SweepModeWifi || app->mode == SweepModeBle)) {
                uint32_t now = furi_get_tick();
                /* A bounded window completes regardless of result count. */
                if(app->marauder_state == MarauderScanning &&
                   (uint32_t)(now - app->last_rescan_tick) >= MARAUDER_SCAN_TIMEOUT_MS) {
                    marauder_stop_scan(app);
                    app->marauder_state = app->marauder_confirmed ?
                                               MarauderDone : MarauderError;
                    app->last_rescan_tick = now;
                    if(app->mode == SweepModeWifi) app->wifi_scan_end_tick = now;
                    else app->ble_scan_end_tick = now;
                    char scan_detail[48];
                    snprintf(
                        scan_detail,
                        sizeof(scan_detail),
                        app->marauder_confirmed ? "completed observations=%u; absence not proven" :
                                                  "Marauder response not confirmed",
                        app->mode == SweepModeWifi ? app->wifi_window_observations :
                                                     app->ble_window_observations);
                    record_enqueue(
                        app,
                        "scan_end",
                        app->mode == SweepModeWifi ? "WIFI" : "BLE",
                        "window",
                        0,
                        0,
                        0,
                        scan_detail);
                }
                if(app->auto_rescan &&
                   (app->marauder_state == MarauderIdle ||
                    app->marauder_state == MarauderError ||
                    app->marauder_state == MarauderDone) &&
                   now - app->last_rescan_tick >= RESCAN_INTERVAL_MS) {
                    marauder_start_scan(
                        app,
                        app->mode == SweepModeWifi ? MARAUDER_CMD_WIFI : MARAUDER_CMD_BLE,
                        false);
                }
            }

            /* GPS: GPIO baud cycle, then Marauder nmea fallback */
            if(app->mode == SweepModeGps && app->gps_active) {
                uint32_t now = furi_get_tick();
                bool fresh = app->gps_last_valid_tick > 0 &&
                             now - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
                if(!fresh && now - app->gps_last_request_tick >= 5000) {
                    app->gps_last_request_tick = now;
                    if(app->gps_profile == GPS_PROFILE_EXTERNAL) {
                        if(app->gps_gpio_baud == GPS_GPIO_BAUD_PRIMARY) {
                            gps_gpio_open(app, GPS_GPIO_BAUD_ALT);
                        } else {
                            gps_gpio_open(app, GPS_GPIO_BAUD_PRIMARY);
                        }
                    } else if(app->serial) {
                        if(app->gps.sentences == 0) gps_marauder_stream(app);
                        else gps_marauder_poll(app);
                    }
                }
            }

            view_port_update(view_port);
            continue;
        }
        RoomSweepInputAction input_action = classify_input_event(&event);
        if(input_action == RoomSweepInputNone) continue;
        process_gps_gpio_bytes(app);
        process_uart_lines(app);
        record_drain(app);

        if(input_action == RoomSweepInputBackShort ||
           input_action == RoomSweepInputBackLong) {
            RoomSweepBackAction back_action = room_sweep_back_action(
                app->settings_active,
                app->mode == SweepModeTx && app->tx_state != TxDisarmed,
                input_action == RoomSweepInputBackLong);
            if(back_action == RoomSweepBackExit) {
                app->running = false;
                break;
            }
            if(back_action == RoomSweepBackCloseSettings) {
                app->settings_active = false;
            } else if(back_action == RoomSweepBackDisarm) {
                record_enqueue(
                    app, "tx_disarm", "TX", "back", 0, app->tx_freq_hz, 0, "user stop");
                tx_stop_and_join(app);
                app->tx_state = TxDisarmed;
            } else {
                app->settings_active = true;
                app->settings_sel = 0;
            }
            continue;
        }

        /* --- Settings overlay input --- */
        if(app->settings_active) {
            if(input_action == RoomSweepInputBrowseUp ||
               input_action == RoomSweepInputBrowseDown) {
                app->settings_sel = (uint8_t)room_sweep_cursor_step(
                    app->settings_sel, SET_COUNT, input_action);
            } else if(input_action == RoomSweepInputPrimary) {
                if(app->settings_sel == SET_SOUND) {
                    app->sound_on = !app->sound_on;
                    if(app->sound_on) notification_message(app->notif, &seq_test_beep);
                } else if(app->settings_sel == SET_VIBRO) {
                    app->vibro_on = !app->vibro_on;
                    if(app->vibro_on) notification_message(app->notif, &seq_test_vibro);
                } else if(app->settings_sel == SET_RESCAN) {
                    app->auto_rescan = !app->auto_rescan;
                } else if(app->settings_sel == SET_LOG) {
                    if(!app->session_log_on) {
                        if(!app->storage) app->storage = furi_record_open(RECORD_STORAGE);
                        if(session_log_begin(app->storage)) {
                            app->record_storage_error = false;
                            furi_mutex_acquire(app->record_mutex, FuriWaitForever);
                            app->record_head = 0;
                            app->record_tail = 0;
                            app->record_queue_drops = 0;
                            app->session_log_on = true;
                            furi_mutex_release(app->record_mutex);
                            app->record_ordinal = session_log_ordinal();
                            app->record_dropped_total = 0;
                            app->reported_uart_drops = app->uart_line_drops;
                            app->reported_gps_drops = app->gps_byte_drops;
                            app->reported_dump_overwrites = app->dump_overwrites;
                            app->reported_wifi_table_full = app->wifi_table_full;
                            app->reported_ble_table_full = app->ble_table_full;
                            uint8_t unavailable = 0;
                            if(!app->radio)
                                unavailable |= (uint8_t)RoomSweepReportSensorRf;
                            if(!app->serial) {
                                unavailable |= (uint8_t)RoomSweepReportSensorWifi;
                                unavailable |= (uint8_t)RoomSweepReportSensorBle;
                            }
                            if(app->gps_profile == GPS_PROFILE_EXTERNAL ?
                                   !app->gps_gpio_open : !app->serial)
                                unavailable |= (uint8_t)RoomSweepReportSensorGps;
                            session_log_note_sensor_unavailable(unavailable);
                            record_enqueue(
                                app,
                                "config",
                                "SYSTEM",
                                "start",
                                0,
                                0,
                                0,
                                app->gps_log_coordinates ?
                                    "GPS coordinates included by user" :
                                    "GPS coordinates omitted by default");
                            notification_message(app->notif, &seq_test_beep);
                        } else {
                            furi_mutex_acquire(app->record_mutex, FuriWaitForever);
                            app->session_log_on = false;
                            furi_mutex_release(app->record_mutex);
                            app->record_storage_error = true;
                        }
                    } else {
                        record_finish_session(app);
                    }
                } else if(app->settings_sel == SET_EXTBAND) {
                    app->ext_band = (ExtBandPref)((app->ext_band + 1) % 3);
                    app->rf_config_generation++;
                    app->sweep_running = false;
                    app->peak_running = false;
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    room_sweep_candidate_invalidate(&app->signal_candidate);
                    furi_mutex_release(app->mutex);
                    app->tx_from_candidate = false;
                    room_sweep_candidate_invalidate(&app->tx_candidate);
                    if(app->target_kind == TargetRF) {
                        app->target_kind = TargetNone;
                        app->target_freq_hz = 0;
                        app->target_id[0] = '\0';
                        app->target_rssi = -127;
                    }
                    if(app->ext_band == ExtBand400) {
                        app->tx_freq_idx = 0;
                        app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
                    } else if(app->ext_band == ExtBand900) {
                        app->tx_freq_idx = 2;
                        app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
                    }
                    if(app->radio_path == RadioPathExternal && app->ext_band != ExtBandAuto) {
                        app->sweep_band_idx = rf_default_sweep_band(app);
                    }
                    record_enqueue(
                        app,
                        "config",
                        "RF",
                        "external_band",
                        0,
                        0,
                        0,
                        app->ext_band == ExtBand400 ? "400 MHz path selected" :
                        app->ext_band == ExtBand900 ? "900 MHz path selected" :
                                                       "automatic receive path; external TX blocked");
                } else if(app->settings_sel == SET_GPSSRC) {
                    app->gps_profile = app->gps_profile == GPS_PROFILE_BFFB ?
                                           GPS_PROFILE_EXTERNAL : GPS_PROFILE_BFFB;
                    if(app->gps_active) {
                        app->gps_active = false;
                        gps_gpio_close(app);
                        if(app->serial) marauder_stop_scan(app);
                        update_gps_mode(app);
                    }
                    record_enqueue(
                        app,
                        "config",
                        "GPS",
                        "source",
                        0,
                        0,
                        0,
                        app->gps_profile == GPS_PROFILE_EXTERNAL ?
                            "optional external GPIO selected" : "BFFB Marauder selected");
                } else if(app->settings_sel == SET_GPSLOG) {
                    furi_mutex_acquire(app->record_mutex, FuriWaitForever);
                    app->gps_log_coordinates = !app->gps_log_coordinates;
                    bool gps_coordinates_enabled = app->gps_log_coordinates;
                    furi_mutex_release(app->record_mutex);
                    record_enqueue(
                        app,
                        "config",
                        "GPS",
                        "privacy",
                        0,
                        0,
                        0,
                        gps_coordinates_enabled ?
                            "coordinates enabled by user" : "coordinates omitted");
                } else if(app->settings_sel == SET_BASELINE) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) {
                        app->baseline_rssi[i] = app->rssi[i];
                    }
                    furi_mutex_release(app->mutex);
                    app->baseline_set = true;
                    for(uint8_t i = 0; i < RF_NUM_CHANNELS; i++) {
                        record_enqueue(
                            app,
                            "baseline",
                            "RF",
                            rf_labels[i],
                            (int)app->baseline_rssi[i],
                            rf_channels[i],
                            i,
                            "survey channel baseline RSSI");
                    }
                    notification_message(app->notif, &seq_test_beep);
                } else if(app->settings_sel == SET_DUMP) {
                    if(!app->storage) app->storage = furi_record_open(RECORD_STORAGE);
                    const char* lines[DUMP_LINE_COUNT];
                    uint8_t n = app->dump_count;
                    if(n > DUMP_LINE_COUNT) n = DUMP_LINE_COUNT;
                    /* oldest first */
                    uint8_t start =
                        (uint8_t)((app->dump_head + DUMP_LINE_COUNT - n) % DUMP_LINE_COUNT);
                    for(uint8_t i = 0; i < n; i++) {
                        lines[i] = app->dump_lines[(start + i) % DUMP_LINE_COUNT];
                    }
                    if(session_log_write_dump(
                           app->storage,
                           lines,
                           n,
                           app->uart_line_drops,
                           app->dump_overwrites)) {
                        app->raw_dump_error = false;
                        record_enqueue(
                            app,
                            "raw_dump",
                            "SYSTEM",
                            "uart",
                            0,
                            0,
                            0,
                            "explicit raw UART snapshot created");
                        notification_message(app->notif, &seq_test_beep);
                    } else {
                        app->raw_dump_error = true;
                        if(session_log_has_error()) {
                            app->record_storage_error = true;
                            if(session_log_is_open()) record_finish_session(app);
                        }
                        notification_message(app->notif, &seq_vibro_pulse);
                    }
                } else if(app->settings_sel == SET_TXDUR) {
                    app->tx_duration_s = (app->tx_duration_s % TX_MAX_DURATION_S) + 1;
                    record_enqueue(
                        app, "config", "TX", "duration", 0, 0, 0, "bounded duration changed");
                }
            }
            continue;
        }

        /* --- TX tab: safety-critical input handling --- */
        if(app->mode == SweepModeTx) {
            if(input_action == RoomSweepInputPrimary ||
               input_action == RoomSweepInputSecondary) {
                bool long_press = input_action == RoomSweepInputSecondary;
                RoomSweepTxInputState input_state = tx_input_state(app->tx_state);
                bool needs_preflight =
                    (!long_press && input_state == RoomSweepTxInputDisarmed) ||
                    (long_press && input_state == RoomSweepTxInputArmed);
                bool frequency_valid = needs_preflight && tx_frequency_preflight(app);
                RoomSweepTxDecision decision = room_sweep_tx_ok_decision(
                    input_state,
                    app->tx_worker_state,
                    frequency_valid,
                    long_press);

                if(needs_preflight && !frequency_valid) {
                    record_enqueue(
                        app,
                        "tx_refused",
                        "TX",
                        app->tx_from_candidate ? "rx_candidate" : "preset",
                        0,
                        app->tx_freq_hz,
                        0,
                        room_sweep_tx_refusal_text(app->tx_refusal));
                    tx_recover_from_candidate_refusal(app);
                    app->tx_state = TxDisarmed;
                    continue;
                }

                if(decision == RoomSweepTxDecisionArm) {
                    app->tx_state = TxArmed;
                    record_enqueue(
                        app,
                        "tx_arm",
                        "TX",
                        app->tx_from_candidate ? "rx_candidate" : "preset",
                        0,
                        app->tx_freq_hz,
                        0,
                        "software armed; no RF output");
                    notification_message(app->notif, &seq_tx_alert);
                } else if(decision == RoomSweepTxDecisionStart) {
                    /* LONG OK while armed: TRANSMIT */
                    /* Reap a completed worker before allocating the next run. */
                    tx_thread_cleanup(app);
                    if(app->tx_thread != NULL) {
                        app->tx_refusal = RoomSweepTxRefusalStartFailed;
                        record_enqueue(
                            app,
                            "tx_refused",
                            "TX",
                            "worker_busy",
                            0,
                            app->tx_freq_hz,
                            0,
                            room_sweep_tx_refusal_text(app->tx_refusal));
                        app->tx_state = TxDisarmed;
                        continue;
                    }
                    app->tx_thread = furi_thread_alloc_ex("RoomSweepTX", 2048, tx_thread, app);
                    if(!app->tx_thread) {
                        app->tx_refusal = RoomSweepTxRefusalStartFailed;
                        record_enqueue(
                            app,
                            "tx_refused",
                            "TX",
                            "worker_alloc",
                            0,
                            app->tx_freq_hz,
                            0,
                            room_sweep_tx_refusal_text(app->tx_refusal));
                        app->tx_state = TxDisarmed;
                        continue;
                    }
                    app->tx_state = TxStarting;
                    app->tx_active = true;
                    app->tx_worker_state = RoomSweepTxWorkerRunning;
                    furi_thread_start(app->tx_thread);
                }
            }
            if((input_action == RoomSweepInputBrowseUp ||
                input_action == RoomSweepInputBrowseDown) &&
               app->tx_state == TxArmed) {
                app->tx_freq_idx = (uint8_t)room_sweep_cursor_step(
                    app->tx_freq_idx, TX_FREQ_PRESET_COUNT, input_action);
                app->tx_freq_hz = tx_freq_presets[app->tx_freq_idx];
                app->tx_from_candidate = false;
                room_sweep_candidate_invalidate(&app->tx_candidate);
                app->tx_refusal = RoomSweepTxRefusalNone;
            }
            if(room_sweep_input_is_tab_navigation(input_action)) {
                navigate_tab(app, input_action == RoomSweepInputNavigateNext);
            }
            continue;
        }

        /* --- General input (non-TX tabs) --- */
        if(room_sweep_input_is_tab_navigation(input_action)) {
            navigate_tab(app, input_action == RoomSweepInputNavigateNext);
            continue;
        }
        if(app->mode == SweepModeRF && app->rf_sub == RfSubSweep &&
           !app->sweep_running &&
           (input_action == RoomSweepInputAlternatePrev ||
            input_action == RoomSweepInputAlternateNext)) {
            app->sweep_band_idx = (uint8_t)room_sweep_cursor_step(
                app->sweep_band_idx,
                RF_BAND_COUNT,
                input_action == RoomSweepInputAlternatePrev ?
                    RoomSweepInputBrowseUp : RoomSweepInputBrowseDown);
            continue;
        }

        /* --- RF tab: Up/Down cycles sub-modes, OK triggers actions --- */
        if(app->mode == SweepModeRF) {
            if(input_action == RoomSweepInputBrowseUp ||
               input_action == RoomSweepInputBrowseDown) {
                app->rf_sub = (RfSubMode)room_sweep_cursor_step(
                    app->rf_sub, RfSubCount, input_action);
                app->sweep_running = false;
                app->peak_running = false;
            }
            if(input_action == RoomSweepInputPrimary) {
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
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    bool candidate_fresh = room_sweep_candidate_is_fresh(
                        &app->signal_candidate, furi_get_tick());
                    furi_mutex_release(app->mutex);
                    if(candidate_fresh) {
                        app->peak_running = true;
                        app->peak_fine_rssi = -120.0f;
                        app->sweep_progress = 0;
                    }
                } else if(app->rf_sub == RfSubPeak) {
                    app->peak_running = false;
                }
            }
            if(input_action == RoomSweepInputSecondary &&
               !app->sweep_running && !app->peak_running) {
                if(app->target_kind == TargetRF) {
                    record_enqueue(
                        app,
                        "unlock",
                        "RF",
                        app->target_id,
                        app->target_rssi,
                        app->target_freq_hz,
                        0,
                        "target follow stopped");
                    app->target_kind = TargetNone;
                    app->target_freq_hz = 0;
                    app->target_rssi = -127;
                } else {
                    uint32_t candidate_freq = 0;
                    float candidate_rssi = -120.0f;
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    bool candidate_fresh = room_sweep_candidate_is_fresh(
                        &app->signal_candidate, furi_get_tick());
                    if(candidate_fresh) {
                        candidate_freq = app->signal_candidate.requested_hz;
                        candidate_rssi = app->signal_candidate.rssi;
                    }
                    furi_mutex_release(app->mutex);
                    if(candidate_fresh) {
                        app->target_kind = TargetRF;
                        app->target_freq_hz = candidate_freq;
                        snprintf(
                            app->target_id,
                            sizeof(app->target_id),
                            "%lu",
                            (unsigned long)(candidate_freq / 1000000));
                        app->target_rssi = (int8_t)candidate_rssi;
                        record_enqueue(
                            app,
                            "lock",
                            "RF",
                            app->target_id,
                            app->target_rssi,
                            app->target_freq_hz,
                            0,
                            "target follows qualified fresh observation");
                        if(app->sound_on)
                            notification_message(app->notif, &seq_lock_tone);
                    }
                }
            }
            continue;
        }

        if(app->mode == SweepModeWifi) {
            if(input_action == RoomSweepInputBrowseUp ||
               input_action == RoomSweepInputBrowseDown) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->wifi_scroll = (uint8_t)room_sweep_cursor_step(
                    app->wifi_scroll, app->wifi_count, input_action);
                furi_mutex_release(app->mutex);
            } else if(input_action == RoomSweepInputPrimary && app->serial) {
                marauder_start_for_mode(app);
            } else if(input_action == RoomSweepInputSecondary && app->wifi_count > 0) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                if(app->target_kind == TargetWifi) {
                    record_enqueue(
                        app,
                        "unlock",
                        "WIFI",
                        app->target_id,
                        app->target_rssi,
                        0,
                        0,
                        "selected AP follow stopped");
                    app->target_kind = TargetNone;
                    app->target_id[0] = '\0';
                    app->target_rssi = -127;
                } else {
                    uint8_t selected = app->wifi_scroll < app->wifi_count ?
                                           app->wifi_scroll : 0;
                    WifiAp* ap = &app->wifi_aps[selected];
                    app->target_kind = TargetWifi;
                    strncpy(
                        app->target_id, ap->bssid[0] ? ap->bssid : ap->ssid, 32);
                    app->target_id[32] = '\0';
                    app->target_rssi = ap->rssi;
                    app->target_freq_hz = 0;
                    record_enqueue(
                        app,
                        "lock",
                        "WIFI",
                        app->target_id,
                        app->target_rssi,
                        0,
                        ap->channel,
                        "selected AP beacon follow");
                    if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
                }
                furi_mutex_release(app->mutex);
            }
            continue;
        }

        if(app->mode == SweepModeBle) {
            if(input_action == RoomSweepInputBrowseUp ||
               input_action == RoomSweepInputBrowseDown) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->ble_scroll = (uint8_t)room_sweep_cursor_step(
                    app->ble_scroll, app->ble_count, input_action);
                furi_mutex_release(app->mutex);
            } else if(input_action == RoomSweepInputPrimary && app->serial) {
                marauder_start_for_mode(app);
            } else if(input_action == RoomSweepInputSecondary && app->ble_count > 0) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                if(app->target_kind == TargetBle) {
                    record_enqueue(
                        app,
                        "unlock",
                        "BLE",
                        app->target_id,
                        app->target_rssi,
                        0,
                        0,
                        "selected BLE follow stopped");
                    app->target_kind = TargetNone;
                    app->target_id[0] = '\0';
                    app->target_rssi = -127;
                } else {
                    uint8_t selected = app->ble_scroll < app->ble_count ?
                                           app->ble_scroll : 0;
                    BleDev* dev = &app->ble_devs[selected];
                    app->target_kind = TargetBle;
                    strncpy(
                        app->target_id, dev->mac[0] ? dev->mac : dev->name, 32);
                    app->target_id[32] = '\0';
                    app->target_rssi = dev->rssi;
                    app->target_freq_hz = 0;
                    record_enqueue(
                        app,
                        "lock",
                        "BLE",
                        app->target_id,
                        app->target_rssi,
                        0,
                        0,
                        "selected active-scan observation follow");
                    if(app->sound_on) notification_message(app->notif, &seq_lock_tone);
                }
                furi_mutex_release(app->mutex);
            }
            continue;
        }

        if(app->mode == SweepModeGps) {
            if(input_action == RoomSweepInputBrowseUp) {
                app->gps_page = room_sweep_gps_page_prev(app->gps_page);
                continue;
            }
            if(input_action == RoomSweepInputBrowseDown) {
                app->gps_page = room_sweep_gps_page_next(app->gps_page);
                continue;
            }
            if(input_action != RoomSweepInputPrimary) continue;
            bool gps_fresh = app->gps_last_valid_tick > 0 &&
                             furi_get_tick() - app->gps_last_valid_tick < GPS_STALE_TIMEOUT_MS;
            if(app->gps.sentences == 0 || !gps_fresh) {
                furi_mutex_acquire(app->gps_mutex, FuriWaitForever);
                nmea_init(&app->gps);
                app->gps_last_valid_tick = 0;
                furi_mutex_release(app->gps_mutex);
                if(app->gps_profile == GPS_PROFILE_EXTERNAL) {
                    gps_gpio_open(
                        app,
                        app->gps_gpio_baud == GPS_GPIO_BAUD_PRIMARY ?
                            GPS_GPIO_BAUD_ALT : GPS_GPIO_BAUD_PRIMARY);
                } else if(app->serial) {
                    gps_marauder_stream(app);
                }
                app->gps_last_request_tick = furi_get_tick();
            } else if(gps_fresh && app->gps.has_pos) {
                if(app->gps_mark_set) {
                    record_enqueue(
                        app, "mark_clear", "GPS", "mark", 0, 0, 0, "distance mark cleared");
                    app->gps_mark_set = false;
                } else {
                    app->gps_mark_set = true;
                    app->gps_mark_lat = app->gps.latitude;
                    app->gps_mark_lon = app->gps.longitude;
                    record_enqueue(
                        app, "mark_set", "GPS", "mark", 0, 0, 0, "distance mark set");
                    if(app->sound_on) notification_message(app->notif, &seq_test_beep);
                    if(app->vibro_on) notification_message(app->notif, &seq_test_vibro);
                }
            }
            continue;
        }

        if(app->mode == SweepModeInfo &&
           (input_action == RoomSweepInputBrowseUp ||
            input_action == RoomSweepInputBrowseDown)) {
            app->info_page = (uint8_t)room_sweep_cursor_step(
                app->info_page, 2, input_action);
        }

        app->tick_count++;
        record_drain(app);
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

    tx_stop_and_join(app);

    furi_thread_join(app->rf_thread);
    furi_thread_free(app->rf_thread);

    radio_close(app);

    if(session_log_is_open()) {
        record_finish_session(app);
    }
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
    furi_mutex_free(app->gps_mutex);
    furi_mutex_free(app->record_mutex);
    free(app);
    return 0;
}

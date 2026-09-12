#include "session_log.h"

#include "room_sweep_record_state.h"
#include "room_sweep_report.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

#define LOG_DIR APP_DATA_PATH("")
#define SESSION_PATH_MAX 96U
#define REPORT_BUFFER_SIZE 2048U
#define MIN_FREE_SPACE (1024U * 1024U)
#define SYNC_INTERVAL_MS 5000U
#define SYNC_INTERVAL_BYTES 4096U
#define END_RECORD_RESERVE_BYTES 256U

typedef struct {
    Storage* storage;
    File* session_file;
    uint32_t ordinal;
    uint32_t sequence;
    uint32_t last_sync_tick;
    uint32_t bytes_since_sync;
    bool budget_exhausted;
    RoomSweepRecordState record;
    RoomSweepRecordIdentifierMap identifiers;
    RoomSweepReportState report;
    uint32_t observation_count[5]; /* RF, WIFI, BLE, GPS, NRF24 */
    uint32_t scan_windows[3]; /* WIFI, BLE, NRF24 */
    int strongest_rssi[4]; /* RF, WIFI, BLE, NRF24 */
    uint32_t strongest_freq[4];
    char strongest_id[4][16];
    uint32_t nrf_active_channels;
    uint8_t nrf_top_channel;
    uint32_t nrf_total_hits; /* summed RPD energy hits across passes */
    bool full_sweep_completed;
    char session_path[SESSION_PATH_MAX];
    char uart_path[SESSION_PATH_MAX];
    char report_path[SESSION_PATH_MAX];
} SessionLogContext;

static SessionLogContext s_log;

static void note_storage_failure(void) {
    room_sweep_record_state_note_storage_failure(&s_log.record);
    room_sweep_report_note_storage_error(&s_log.report);
}

static void close_and_free(File** file) {
    if(!file || !*file) return;
    storage_file_close(*file);
    storage_file_free(*file);
    *file = NULL;
}

static bool ensure_log_directory(Storage* storage) {
    /* The loader resolves /data to this FAP's existing app-data directory. */
    return storage != NULL;
}

static bool make_path(
    RoomSweepRecordFileKind kind,
    uint32_t ordinal,
    char* out,
    size_t out_size) {
    char name[ROOM_SWEEP_RECORD_NAME_MAX];
    if(!room_sweep_record_filename(kind, ordinal, name, sizeof(name))) return false;
    int written = snprintf(out, out_size, "%s%s", LOG_DIR, name);
    return written > 0 && (size_t)written < out_size;
}

static bool choose_ordinal(Storage* storage, uint32_t* ordinal) {
    char path[SESSION_PATH_MAX];
    for(uint32_t candidate = 1; candidate < 10000U; candidate++) {
        bool collision = false;
        for(RoomSweepRecordFileKind kind = RoomSweepRecordFileSession;
            kind < RoomSweepRecordFileCount;
            kind++) {
            if(!make_path(kind, candidate, path, sizeof(path))) return false;
            if(storage_file_exists(storage, path)) {
                collision = true;
                break;
            }
        }
        if(!collision) {
            *ordinal = candidate;
            return true;
        }
    }
    return false;
}

static bool checked_write(File* file, const char* data, size_t size) {
    if(!file || !data || size == 0 || s_log.record.storage_failed) return false;
    bool end_reserved = s_log.record.records_written < s_log.record.max_records &&
                        s_log.record.max_records - s_log.record.records_written > 1U &&
                        s_log.record.bytes_written <= s_log.record.max_bytes &&
                        END_RECORD_RESERVE_BYTES <=
                            s_log.record.max_bytes - s_log.record.bytes_written &&
                        size <= s_log.record.max_bytes - s_log.record.bytes_written -
                                    END_RECORD_RESERVE_BYTES;
    if(!end_reserved || !room_sweep_record_state_can_append(&s_log.record, size)) {
        s_log.record.dropped_records++;
        room_sweep_report_note_drop(&s_log.report, 1);
        s_log.budget_exhausted = true;
        return false;
    }
    if(storage_file_write(file, data, size) != size) {
        note_storage_failure();
        return false;
    }
    if(!room_sweep_record_state_commit_append(&s_log.record, size)) {
        note_storage_failure();
        return false;
    }
    s_log.bytes_since_sync += (uint32_t)size;
    return true;
}

static bool checked_write_end(File* file, const char* data, size_t size) {
    if(!file || !data || size == 0 || s_log.record.storage_failed ||
       !room_sweep_record_state_can_append(&s_log.record, size)) {
        return false;
    }
    if(storage_file_write(file, data, size) != size) {
        note_storage_failure();
        return false;
    }
    if(!room_sweep_record_state_commit_append(&s_log.record, size)) {
        note_storage_failure();
        return false;
    }
    return true;
}

static bool sync_session_if_due(bool force) {
    if(!session_log_is_open() || session_log_has_error()) return false;
    uint32_t now = furi_get_tick();
    if(!force && s_log.bytes_since_sync < SYNC_INTERVAL_BYTES &&
       (uint32_t)(now - s_log.last_sync_tick) < SYNC_INTERVAL_MS) {
        return true;
    }
    if(!storage_file_sync(s_log.session_file)) {
        note_storage_failure();
        return false;
    }
    s_log.last_sync_tick = now;
    s_log.bytes_since_sync = 0;
    return true;
}

static void clean_csv(const char* input, char* output, size_t output_size) {
    if(!output || output_size == 0) return;
    size_t used = 0;
    if(input) {
        for(size_t i = 0; input[i] && used + 1U < output_size; i++) {
            char value = input[i];
            if(value == ',' || value == '\n' || value == '\r') value = '_';
            output[used++] = value;
        }
    }
    output[used] = '\0';
}

static RoomSweepRecordIdKind id_kind_for_event(const char* source) {
    if(source && strcmp(source, "WIFI") == 0) return RoomSweepRecordIdAccessPoint;
    if(source && strcmp(source, "BLE") == 0) return RoomSweepRecordIdBle;
    if(source && strcmp(source, "RF") == 0) return RoomSweepRecordIdRf;
    if(source && strcmp(source, "GPS") == 0) return RoomSweepRecordIdGps;
    return RoomSweepRecordIdOther;
}

static void report_sensor_for_source(const char* source) {
    if(!source) return;
    if(strcmp(source, "RF") == 0)
        room_sweep_report_sensor_confirmed(&s_log.report, RoomSweepReportSensorRf);
    else if(strcmp(source, "WIFI") == 0)
        room_sweep_report_sensor_confirmed(&s_log.report, RoomSweepReportSensorWifi);
    else if(strcmp(source, "BLE") == 0)
        room_sweep_report_sensor_confirmed(&s_log.report, RoomSweepReportSensorBle);
    else if(strcmp(source, "GPS") == 0)
        room_sweep_report_sensor_confirmed(&s_log.report, RoomSweepReportSensorGps);
    else if(strcmp(source, "NRF24") == 0)
        room_sweep_report_sensor_confirmed(&s_log.report, RoomSweepReportSensorNrf24);
}

static int sensor_index(const char* source) {
    if(!source) return -1;
    if(strcmp(source, "RF") == 0) return 0;
    if(strcmp(source, "WIFI") == 0) return 1;
    if(strcmp(source, "BLE") == 0) return 2;
    if(strcmp(source, "GPS") == 0) return 3;
    if(strcmp(source, "NRF24") == 0) return 4;
    return -1;
}

bool session_log_is_open(void) {
    return s_log.session_file && storage_file_is_open(s_log.session_file);
}

bool session_log_has_error(void) {
    return s_log.record.storage_failed || s_log.budget_exhausted;
}

uint32_t session_log_dropped(void) {
    return s_log.record.dropped_records;
}

uint32_t session_log_ordinal(void) {
    return s_log.ordinal;
}

const char* session_log_session_path(void) {
    return s_log.session_path;
}

const char* session_log_uart_path(void) {
    return s_log.uart_path;
}

const char* session_log_report_path(void) {
    return s_log.report_path;
}

bool session_log_begin(Storage* storage) {
    if(!storage || session_log_is_open()) return session_log_is_open();
    memset(&s_log, 0, sizeof(s_log));
    s_log.storage = storage;
    if(!ensure_log_directory(storage)) return false;
    if(!choose_ordinal(storage, &s_log.ordinal) ||
       !make_path(
           RoomSweepRecordFileSession,
           s_log.ordinal,
           s_log.session_path,
           sizeof(s_log.session_path)) ||
       !make_path(
           RoomSweepRecordFileUart,
           s_log.ordinal,
           s_log.uart_path,
           sizeof(s_log.uart_path)) ||
       !make_path(
           RoomSweepRecordFileReport,
           s_log.ordinal,
           s_log.report_path,
           sizeof(s_log.report_path))) {
        return false;
    }

    uint64_t total_space = 0;
    uint64_t free_space = 0;
    /* /data is the FAP alias for this app's directory on the external SD card. */
    if(storage_common_fs_info(storage, STORAGE_EXT_PATH_PREFIX, &total_space, &free_space) !=
           FSE_OK ||
       free_space < MIN_FREE_SPACE) {
        return false;
    }
    UNUSED(total_space);
    uint32_t byte_budget = ROOM_SWEEP_RECORD_DEFAULT_MAX_BYTES;
    if(free_space / 16U < byte_budget) byte_budget = (uint32_t)(free_space / 16U);
    room_sweep_record_state_init(
        &s_log.record, ROOM_SWEEP_RECORD_DEFAULT_MAX_RECORDS, byte_budget);
    room_sweep_record_identifier_map_init(&s_log.identifiers);
    room_sweep_report_init(&s_log.report);
    room_sweep_report_set_gps_omitted(&s_log.report);
    for(size_t i = 0; i < 4; i++) s_log.strongest_rssi[i] = -127;
    s_log.nrf_active_channels = 0;
    s_log.nrf_top_channel = 0;
    s_log.full_sweep_completed = false;
    if(!room_sweep_record_state_begin(&s_log.record)) return false;

    s_log.session_file = storage_file_alloc(storage);
    if(!s_log.session_file || !storage_file_open(
           s_log.session_file,
           s_log.session_path,
           FSAM_WRITE,
           FSOM_CREATE_NEW)) {
        close_and_free(&s_log.session_file);
        note_storage_failure();
        return false;
    }
    const char* header =
        "schema,seq,tick_ms,event,source,mode,submode,id,rssi,freq_hz,channel,has_pos,lat,lon,has_gps,fix_quality,sats_used,sats_view,speed_kmh,course_deg,gps_utc,gps_date,gps_nav_sentences,gps_rx_bytes,count,state,error_code,detail\n"
        "3,0,0,begin,SYSTEM,APP,start,-,0,0,0,0,,,0,0,0,0,,,,,0,0,1,active,,session started\n";
    if(!checked_write(s_log.session_file, header, strlen(header))) {
        session_log_end(storage);
        return false;
    }
    if(!sync_session_if_due(true)) {
        session_log_end(storage);
        return false;
    }
    s_log.sequence = 1;
    return true;
}

bool session_log_write_event(const SessionLogEvent* event) {
    if(!event || !session_log_is_open() || session_log_has_error()) return false;
    char event_clean[20];
    char source_clean[12];
    char mode_clean[12];
    char submode_clean[16];
    char detail_clean[64];
    char state_clean[16];
    char error_clean[20];
    char id_ref[16];
    clean_csv(event->event, event_clean, sizeof(event_clean));
    clean_csv(event->source, source_clean, sizeof(source_clean));
    clean_csv(event->mode, mode_clean, sizeof(mode_clean));
    clean_csv(event->submode, submode_clean, sizeof(submode_clean));
    clean_csv(event->detail, detail_clean, sizeof(detail_clean));
    clean_csv(event->state, state_clean, sizeof(state_clean));
    clean_csv(event->error_code, error_clean, sizeof(error_clean));
    if(!room_sweep_record_identifier_ref(
           &s_log.identifiers,
           id_kind_for_event(source_clean),
           event->id,
           id_ref,
           sizeof(id_ref))) {
        snprintf(id_ref, sizeof(id_ref), "REDACTED");
        session_log_note_drop(1);
    }

    char latitude[20] = "";
    char longitude[20] = "";
    char fix_quality[8] = "";
    char sats_used[8] = "";
    char sats_view[8] = "";
    char speed_kmh[16] = "";
    char course_deg[16] = "";
    if(event->has_position) {
        snprintf(latitude, sizeof(latitude), "%.6f", (double)event->latitude);
        snprintf(longitude, sizeof(longitude), "%.6f", (double)event->longitude);
    }
    if(event->has_gps) {
        snprintf(fix_quality, sizeof(fix_quality), "%u", event->fix_quality);
        snprintf(sats_used, sizeof(sats_used), "%u", event->sats_used);
        snprintf(sats_view, sizeof(sats_view), "%u", event->sats_view);
        snprintf(speed_kmh, sizeof(speed_kmh), "%.2f", (double)event->speed_kmh);
        snprintf(course_deg, sizeof(course_deg), "%.2f", (double)event->course_deg);
    }

    char line[384];
    int written = snprintf(
        line,
        sizeof(line),
        "3,%lu,%lu,%s,%s,%s,%s,%s,%d,%lu,%u,%u,%s,%s,%u,%s,%s,%s,%s,%s,%s,%s,%lu,%lu,%lu,%s,%s,%s\n",
        (unsigned long)s_log.sequence,
        (unsigned long)furi_get_tick(),
        event_clean,
        source_clean,
        mode_clean,
        submode_clean,
        id_ref,
        event->rssi,
        (unsigned long)event->freq_hz,
        event->channel,
        event->has_position ? 1U : 0U,
        latitude,
        longitude,
        event->has_gps ? 1U : 0U,
        fix_quality,
        sats_used,
        sats_view,
        speed_kmh,
        course_deg,
        event->gps_utc ? event->gps_utc : "",
        event->gps_date ? event->gps_date : "",
        (unsigned long)event->gps_nav_sentences,
        (unsigned long)event->gps_rx_bytes,
        (unsigned long)event->count,
        state_clean,
        error_clean,
        detail_clean);
    if(written <= 0 || written >= (int)sizeof(line)) {
        session_log_note_drop(1);
        return false;
    }
    if(!checked_write(s_log.session_file, line, (size_t)written)) return false;
    if(event->has_position) room_sweep_report_set_gps_included(&s_log.report);
    s_log.sequence++;
    int index = sensor_index(source_clean);
    if((strcmp(event_clean, "observation") == 0 ||
        strcmp(event_clean, "snapshot") == 0) &&
       index >= 0) {
        s_log.observation_count[index]++;
        /* RF=0 WIFI=1 BLE=2 NRF24=4 map to strongest slots 0..3 */
        int strong_i = (index == 4) ? 3 : (index < 3 ? index : -1);
        if(strong_i >= 0 && event->rssi > s_log.strongest_rssi[strong_i]) {
            s_log.strongest_rssi[strong_i] = event->rssi;
            s_log.strongest_freq[strong_i] = event->freq_hz;
            strncpy(
                s_log.strongest_id[strong_i],
                id_ref,
                sizeof(s_log.strongest_id[strong_i]) - 1U);
        }
        if(index == 4) {
            s_log.nrf_total_hits += event->nrf_total_hits;
            if(event->channel > 0 || event->count > 0) {
                if(event->count > s_log.nrf_active_channels)
                    s_log.nrf_active_channels = event->count;
                if(event->channel != 0) s_log.nrf_top_channel = event->channel;
            }
        }
    }
    if(strcmp(event_clean, "scan_start") == 0 && index == 1)
        s_log.scan_windows[0]++;
    else if(strcmp(event_clean, "scan_start") == 0 && index == 2)
        s_log.scan_windows[1]++;
    else if(strcmp(event_clean, "scan_start") == 0 && index == 4)
        s_log.scan_windows[2]++;
    if(strcmp(event_clean, "full_sweep_done") == 0) s_log.full_sweep_completed = true;
    if(strcmp(event_clean, "observation") == 0 || strcmp(event_clean, "snapshot") == 0)
        report_sensor_for_source(source_clean);
    if(strcmp(source_clean, "TX") == 0) {
        if(strcmp(event_clean, "tx_arm") == 0)
            room_sweep_report_set_tx(&s_log.report, RoomSweepReportTxArmed);
        else if(strcmp(event_clean, "tx_started") == 0)
            room_sweep_report_set_tx(&s_log.report, RoomSweepReportTxStarted);
        else if(strcmp(event_clean, "tx_end") == 0)
            room_sweep_report_set_tx(&s_log.report, RoomSweepReportTxCompleted);
        else if(strcmp(event_clean, "tx_refused") == 0)
            room_sweep_report_set_tx(&s_log.report, RoomSweepReportTxRefused);
        else if(strcmp(event_clean, "tx_aborted") == 0)
            room_sweep_report_set_tx(&s_log.report, RoomSweepReportTxAborted);
    }
    return sync_session_if_due(false);
}

void session_log_note_drop(uint32_t count) {
    if(count == 0) return;
    s_log.record.dropped_records += count;
    room_sweep_report_note_drop(&s_log.report, count);
}

void session_log_note_storage_failure(void) {
    note_storage_failure();
}

void session_log_note_sensor_unavailable(uint8_t sensor_mask) {
    static const RoomSweepReportSensor sensors[] = {
        RoomSweepReportSensorRf,
        RoomSweepReportSensorWifi,
        RoomSweepReportSensorBle,
        RoomSweepReportSensorGps,
        RoomSweepReportSensorNrf24,
    };
    for(size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++) {
        if(sensor_mask & (uint8_t)sensors[i])
            room_sweep_report_sensor_unavailable(&s_log.report, sensors[i]);
    }
}

static bool write_report(void) {
    if(!s_log.storage || !s_log.report_path[0]) return false;
    char report[REPORT_BUFFER_SIZE];
    size_t used = room_sweep_report_format(&s_log.report, report, sizeof(report));
    RoomSweepReportFindings findings;
    room_sweep_report_findings_init(&findings);
    findings.rf_observations = s_log.observation_count[0];
    findings.rf_strongest_rssi = s_log.strongest_rssi[0];
    findings.rf_strongest_hz = s_log.strongest_freq[0];
    findings.wifi_observations = s_log.observation_count[1];
    findings.wifi_windows = s_log.scan_windows[0];
    findings.wifi_strongest_rssi = s_log.strongest_rssi[1];
    findings.ble_observations = s_log.observation_count[2];
    findings.ble_windows = s_log.scan_windows[1];
    findings.ble_strongest_rssi = s_log.strongest_rssi[2];
    findings.nrf_observations = s_log.observation_count[4];
    findings.nrf_active_channels = s_log.nrf_active_channels;
    findings.nrf_top_channel = s_log.nrf_top_channel;
    findings.nrf_total_hits = s_log.nrf_total_hits;
    findings.gps_snapshots = s_log.observation_count[3];
    findings.full_sweep_completed = s_log.full_sweep_completed;
    used = room_sweep_report_append_findings(&findings, report, sizeof(report), used);
    bool uart_exists = storage_file_exists(s_log.storage, s_log.uart_path);
    room_sweep_report_append(
        report,
        sizeof(report),
        &used,
        "Files: session=%s; report=%s; UART snapshot=%s%s\nRecords: %lu; bytes: %lu; dropped: %lu\n",
        s_log.session_path,
        s_log.report_path,
        s_log.uart_path,
        uart_exists ? "" : " (not created)",
        (unsigned long)s_log.record.records_written,
        (unsigned long)s_log.record.bytes_written,
        (unsigned long)s_log.record.dropped_records);
    room_sweep_report_append(
        report,
        sizeof(report),
        &used,
        "Detail CSV holds per-event fields. This Room Report is the plain summary.\n");
    File* report_file = storage_file_alloc(s_log.storage);
    if(!report_file) return false;
    bool opened = storage_file_open(
        report_file, s_log.report_path, FSAM_WRITE, FSOM_CREATE_NEW);
    bool ok = opened && storage_file_write(report_file, report, used) == used &&
              storage_file_sync(report_file);
    if(!storage_file_close(report_file)) ok = false;
    storage_file_free(report_file);
    return ok;
}

void session_log_end(Storage* storage) {
    UNUSED(storage);
    if(!s_log.session_file) return;
    bool clean = !session_log_has_error() && s_log.record.dropped_records == 0;
    if(session_log_is_open()) {
        char end_line[256];
        uint32_t final_records = s_log.record.records_written + 1U;
        int written = snprintf(
            end_line,
            sizeof(end_line),
            "3,%lu,%lu,end,SYSTEM,APP,stop,-,0,0,0,0,,,0,0,0,0,,,,,0,0,1,%s,,records=%lu drops=%lu\n",
            (unsigned long)s_log.sequence,
            (unsigned long)furi_get_tick(),
            clean ? "complete" : "incomplete",
            (unsigned long)final_records,
            (unsigned long)s_log.record.dropped_records);
        bool end_ok = written > 0 && written < (int)sizeof(end_line);
        if(end_ok)
            end_ok = checked_write_end(s_log.session_file, end_line, (size_t)written) &&
                     storage_file_sync(s_log.session_file);
        if(!end_ok) {
            clean = false;
            note_storage_failure();
        }
        if(!storage_file_close(s_log.session_file)) {
            clean = false;
            note_storage_failure();
        }
    }
    room_sweep_record_state_finish(&s_log.record, clean);
    s_log.report.complete =
        s_log.report.complete && room_sweep_record_state_is_complete(&s_log.record);
    if(!write_report()) {
        note_storage_failure();
    }
    storage_file_free(s_log.session_file);
    s_log.session_file = NULL;
}

bool session_log_write_dump(
    Storage* storage,
    const char* const* lines,
    uint8_t count,
    uint32_t dropped_lines,
    uint32_t overwritten_lines) {
    if(!storage || !lines) return false;
    if(!ensure_log_directory(storage)) {
        session_log_note_storage_failure();
        return false;
    }

    bool active_session = session_log_is_open();
    uint32_t ordinal = s_log.ordinal;
    char path[SESSION_PATH_MAX];
    if(!active_session || ordinal == 0) {
        if(!choose_ordinal(storage, &ordinal)) {
            session_log_note_storage_failure();
            return false;
        }
    }
    if(!make_path(RoomSweepRecordFileUart, ordinal, path, sizeof(path))) {
        session_log_note_storage_failure();
        return false;
    }
    if(storage_file_exists(storage, path)) {
        if(active_session) return false;
        if(!choose_ordinal(storage, &ordinal) ||
           !make_path(RoomSweepRecordFileUart, ordinal, path, sizeof(path))) {
            session_log_note_storage_failure();
            return false;
        }
    }
    if(!active_session) {
        s_log.ordinal = ordinal;
        s_log.session_path[0] = '\0';
        s_log.report_path[0] = '\0';
        strncpy(s_log.uart_path, path, sizeof(s_log.uart_path) - 1U);
        s_log.uart_path[sizeof(s_log.uart_path) - 1U] = '\0';
    }

    File* file = storage_file_alloc(storage);
    if(!file || !storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW)) {
        close_and_free(&file);
        session_log_note_storage_failure();
        return false;
    }
    char header[224];
    int header_size = snprintf(
        header,
        sizeof(header),
        "# room_sweep raw UART snapshot; explicit user action\n"
        "# WARNING: raw identifiers and GPS coordinates may be present\n"
        "# newest bounded lines only; uart_queue_drops=%lu ring_overwrites=%lu\n",
        (unsigned long)dropped_lines,
        (unsigned long)overwritten_lines);
    bool ok = header_size > 0 && header_size < (int)sizeof(header) &&
              storage_file_write(file, header, (size_t)header_size) == (size_t)header_size;
    for(uint8_t i = 0; ok && i < count; i++) {
        if(!lines[i] || !lines[i][0]) continue;
        size_t size = strlen(lines[i]);
        ok = storage_file_write(file, lines[i], size) == size &&
             storage_file_write(file, "\n", 1) == 1;
    }
    if(ok) ok = storage_file_sync(file);
    if(!storage_file_close(file)) ok = false;
    storage_file_free(file);
    if(!ok) session_log_note_storage_failure();
    return ok;
}

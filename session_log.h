#pragma once

#include <storage/storage.h>
#include <stdbool.h>
#include <stdint.h>

/* Unique artifacts live under /ext/apps_data/room_sweep/. */
bool session_log_begin(Storage* storage);
void session_log_end(Storage* storage);
bool session_log_is_open(void);
bool session_log_has_error(void);
uint32_t session_log_dropped(void);
uint32_t session_log_ordinal(void);
const char* session_log_session_path(void);
const char* session_log_uart_path(void);
const char* session_log_report_path(void);

/* Main-loop-only event writer. Persistent identifiers are session ordinals. */
typedef struct {
    const char* event;
    const char* source;
    const char* mode;
    const char* submode;
    const char* id;
    int rssi;
    uint32_t freq_hz;
    uint8_t channel;
    bool has_position;
    float latitude;
    float longitude;
    bool has_gps;
    uint8_t fix_quality;
    uint8_t sats_used;
    uint8_t sats_view;
    float speed_kmh;
    float course_deg;
    const char* gps_utc;
    const char* gps_date;
    uint32_t gps_nav_sentences;
    uint32_t gps_rx_bytes;
    uint32_t count;
    const char* state;
    const char* error_code;
    const char* detail;
} SessionLogEvent;

bool session_log_write_event(const SessionLogEvent* event);

void session_log_note_drop(uint32_t count);
void session_log_note_storage_failure(void);
void session_log_note_sensor_unavailable(uint8_t sensor_mask);

/* Explicit raw UART snapshot; never overwrites an earlier dump. */
bool session_log_write_dump(
    Storage* storage,
    const char* const* lines,
    uint8_t count,
    uint32_t dropped_lines,
    uint32_t overwritten_lines);

#pragma once

#include <storage/storage.h>
#include <stdint.h>
#include <stdbool.h>

/* Append-only CSV session log under /ext/apps_data/room_sweep/ */

bool session_log_begin(Storage* storage);
void session_log_end(Storage* storage);
bool session_log_is_open(void);

/* One hit line. id truncated; lat/lon used only if has_pos. */
void session_log_hit(
    const char* kind,
    const char* id,
    int rssi,
    uint32_t freq_hz,
    float lat,
    float lon,
    bool has_pos);

/* Dump raw BFFB/UART line capture to a new file. Returns false on I/O error. */
bool session_log_write_dump(
    Storage* storage,
    const char* const* lines,
    uint8_t count);

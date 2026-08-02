#include "session_log.h"
#include <furi.h>
#include <stdio.h>
#include <string.h>

#define LOG_DIR  APP_DATA_PATH("room_sweep")
#define LOG_PATH APP_DATA_PATH("room_sweep/session.csv")
#define DUMP_PATH APP_DATA_PATH("room_sweep/bffb_dump.txt")

static File* s_log;
static uint32_t s_hits;

bool session_log_is_open(void) {
    return s_log != NULL && storage_file_is_open(s_log);
}

bool session_log_begin(Storage* storage) {
    if(!storage) return false;
    if(session_log_is_open()) return true;

    storage_common_mkdir(storage, APP_DATA_PATH(""));
    storage_common_mkdir(storage, LOG_DIR);

    s_log = storage_file_alloc(storage);
    bool exists = storage_file_exists(storage, LOG_PATH);
    if(!storage_file_open(s_log, LOG_PATH, FSAM_WRITE, FSOM_OPEN_ALWAYS)) {
        storage_file_free(s_log);
        s_log = NULL;
        return false;
    }
    /* Append: seek end */
    storage_file_seek(s_log, (uint32_t)storage_file_size(s_log), true);
    if(!exists || storage_file_size(s_log) == 0) {
        const char* hdr = "tick_ms,lat,lon,kind,id,rssi,freq_hz\n";
        storage_file_write(s_log, hdr, strlen(hdr));
    }
    s_hits = 0;
    return true;
}

void session_log_end(Storage* storage) {
    UNUSED(storage);
    if(!s_log) return;
    if(storage_file_is_open(s_log)) {
        storage_file_sync(s_log);
        storage_file_close(s_log);
    }
    storage_file_free(s_log);
    s_log = NULL;
}

void session_log_hit(
    const char* kind,
    const char* id,
    int rssi,
    uint32_t freq_hz,
    float lat,
    float lon,
    bool has_pos) {
    if(!session_log_is_open()) return;

    char id_clean[28];
    size_t j = 0;
    if(id) {
        for(size_t i = 0; id[i] && j < sizeof(id_clean) - 1; i++) {
            char c = id[i];
            if(c == ',' || c == '\n' || c == '\r') c = '_';
            id_clean[j++] = c;
        }
    }
    id_clean[j] = '\0';
    if(j == 0) {
        id_clean[0] = '-';
        id_clean[1] = '\0';
    }

    char line[160];
    int n;
    if(has_pos) {
        n = snprintf(
            line,
            sizeof(line),
            "%lu,%.6f,%.6f,%s,%s,%d,%lu\n",
            (unsigned long)furi_get_tick(),
            (double)lat,
            (double)lon,
            kind ? kind : "?",
            id_clean,
            rssi,
            (unsigned long)freq_hz);
    } else {
        n = snprintf(
            line,
            sizeof(line),
            "%lu,,,%s,%s,%d,%lu\n",
            (unsigned long)furi_get_tick(),
            kind ? kind : "?",
            id_clean,
            rssi,
            (unsigned long)freq_hz);
    }
    if(n > 0 && n < (int)sizeof(line)) {
        storage_file_write(s_log, line, (size_t)n);
        s_hits++;
        if((s_hits % 8u) == 0u) storage_file_sync(s_log);
    }
}

bool session_log_write_dump(Storage* storage, const char* const* lines, uint8_t count) {
    if(!storage || !lines) return false;
    storage_common_mkdir(storage, APP_DATA_PATH(""));
    storage_common_mkdir(storage, LOG_DIR);

    File* f = storage_file_alloc(storage);
    if(!storage_file_open(f, DUMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(f);
        return false;
    }
    const char* hdr = "# room_sweep BFFB UART dump\n";
    storage_file_write(f, hdr, strlen(hdr));
    for(uint8_t i = 0; i < count; i++) {
        if(!lines[i] || !lines[i][0]) continue;
        storage_file_write(f, lines[i], strlen(lines[i]));
        storage_file_write(f, "\n", 1);
    }
    storage_file_sync(f);
    storage_file_close(f);
    storage_file_free(f);
    return true;
}

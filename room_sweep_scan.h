#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* Pure helpers for Marauder scan timeout + GPS distance.
 * Host-testable (no Flipper deps). */

/* True when a scan should enter MarauderError.
 * - scanning: current state is MarauderScanning
 * - now / scan_start_tick / timeout_ms: wall-clock window since start
 * - result_count: parsed APs or BLE devices this scan
 *
 * Rules:
 * 1. Never time out solely because last_data_tick is 0.
 * 2. If result_count > 0, silence is normal (Marauder BLE dedups prints).
 * 3. ERR only when zero results for the full timeout since scan_start.
 */
static inline bool marauder_scan_should_error(
    bool scanning,
    uint32_t now,
    uint32_t scan_start_tick,
    uint32_t timeout_ms,
    uint8_t result_count) {
    if(!scanning) return false;
    if(result_count > 0) return false;
    if(timeout_ms == 0) return false;
    return (now - scan_start_tick) >= timeout_ms;
}

/*
 * Captured BFFB Marauder BLE (headless Dev Board Pro) looks like:
 *   ">  RSSI: -37 Device: f4:0c:6d:d1:90:07 RSSI: -50 Device: 4e:81:…"
 * Wiki form "-60 Device: name" may also appear. No reliable '\n' between
 * records; '#stopscan' can abut the last MAC. Room Sweep frames and splits.
 */
#define ROOM_SWEEP_UART_LINE_MAX 128

/* Strip Marauder prompt / whitespace. Returns pointer into s (or near). */
static inline const char* room_sweep_uart_strip_prompt(const char* s) {
    if(!s) return s;
    while(*s == '>' || *s == ' ' || *s == '\t') s++;
    return s;
}

/* True if s (already stripped) looks like one BLE observation record. */
static inline bool room_sweep_uart_is_ble_result_line(const char* s) {
    s = room_sweep_uart_strip_prompt(s);
    if(s == NULL || s[0] == '\0') return false;
    /* Live BFFB: "RSSI: -37 Device: …" */
    if(s[0] == 'R' && s[1] == 'S' && s[2] == 'S' && s[3] == 'I') {
        const char* d = s;
        while(*d && !(*d == 'D' && d[1] == 'e' && d[2] == 'v' && d[3] == 'i' && d[4] == 'c' &&
                      d[5] == 'e' && d[6] == ':')) {
            d++;
        }
        return *d != '\0';
    }
    /* Legacy: "-60 Device: …" */
    if(s[0] == '-' && s[1] >= '0' && s[1] <= '9') {
        const char* p = s + 2;
        while(*p >= '0' && *p <= '9') p++;
        if(*p != ' ' && *p != '\0') return false;
        while(*p == ' ') p++;
        return p[0] == 'D' && p[1] == 'e' && p[2] == 'v' && p[3] == 'i' && p[4] == 'c' &&
               p[5] == 'e' && p[6] == ':';
    }
    return false;
}

/*
 * Find start of a BLE record inside text. Returns NULL if none.
 * Accepts "RSSI:" or bare "-NN Device:".
 */
static inline const char* room_sweep_uart_find_ble_record(const char* s) {
    if(!s) return NULL;
    for(const char* p = s; *p; p++) {
        if(p[0] == 'R' && p[1] == 'S' && p[2] == 'S' && p[3] == 'I' &&
           (p[4] == ':' || p[4] == ' ')) {
            /* Require Device: in this record (not WiFi "RSSI: … Ch: … ESSID"). */
            const char* dev = p + 4;
            while(*dev && !(dev[0] == 'R' && dev[1] == 'S' && dev[2] == 'S' && dev[3] == 'I')) {
                if(dev[0] == 'D' && dev[1] == 'e' && dev[2] == 'v' && dev[3] == 'i' &&
                   dev[4] == 'c' && dev[5] == 'e' && dev[6] == ':') {
                    return p;
                }
                dev++;
            }
            continue;
        }
        if(p[0] == '-' && p[1] >= '0' && p[1] <= '9') {
            /* Skip "RSSI: -37" value field (colon before optional spaces). */
            const char* pre = p;
            while(pre > s && pre[-1] == ' ') pre--;
            if(pre > s && pre[-1] == ':') continue;
            const char* q = p + 2;
            while(*q >= '0' && *q <= '9') q++;
            while(*q == ' ') q++;
            if(q[0] == 'D' && q[1] == 'e' && q[2] == 'v' && q[3] == 'i' && q[4] == 'c' &&
               q[5] == 'e' && q[6] == ':') {
                return p;
            }
        }
    }
    return NULL;
}

/* End of this record = start of next record, '#', or NUL. */
static inline const char* room_sweep_uart_ble_record_end(const char* rec) {
    if(!rec || !*rec) return rec;
    const char* next = room_sweep_uart_find_ble_record(rec + 1);
    const char* hash = rec;
    while(*hash && *hash != '#') hash++;
    if(next && (!*hash || next < hash)) return next;
    if(*hash) return hash;
    while(*rec) rec++;
    return rec;
}

typedef struct {
    char buf[ROOM_SWEEP_UART_LINE_MAX];
    uint8_t pos;
} RoomSweepUartLineAccum;

static inline void room_sweep_uart_line_reset(RoomSweepUartLineAccum* a) {
    if(!a) return;
    a->pos = 0;
    a->buf[0] = '\0';
}

/* Copy completed line to out (NUL-terminated). Returns true if a line was emitted. */
static inline bool room_sweep_uart_line_commit(
    RoomSweepUartLineAccum* a,
    char* out,
    size_t out_sz) {
    if(!a || a->pos == 0 || !out || out_sz == 0) return false;
    a->buf[a->pos] = '\0';
    size_t n = (size_t)a->pos;
    if(n >= out_sz) n = out_sz - 1U;
    for(size_t i = 0; i < n; i++) out[i] = a->buf[i];
    out[n] = '\0';
    a->pos = 0;
    a->buf[0] = '\0';
    return true;
}

/* If buffer holds 2+ BLE records, emit the first into out and keep the rest. */
static inline bool room_sweep_uart_try_emit_ble_record(
    RoomSweepUartLineAccum* a,
    char* out,
    size_t out_sz) {
    if(!a || a->pos == 0 || !out || out_sz == 0) return false;
    a->buf[a->pos] = '\0';
    const char* first = room_sweep_uart_find_ble_record(a->buf);
    if(!first) return false;
    const char* end = room_sweep_uart_ble_record_end(first);
    const char* second = room_sweep_uart_find_ble_record(first + 1);
    /* Emit when a following record exists, or buffer ends at '#'. */
    bool should = (second != NULL) || (*end == '#');
    if(!should) return false;

    size_t len = (size_t)(end - first);
    if(len == 0) return false;
    if(len >= out_sz) len = out_sz - 1U;
    for(size_t i = 0; i < len; i++) out[i] = first[i];
    out[len] = '\0';

    /* Shift remainder to front. */
    const char* rest = end;
    size_t rlen = 0;
    while(rest[rlen]) rlen++;
    if(rlen >= ROOM_SWEEP_UART_LINE_MAX) rlen = ROOM_SWEEP_UART_LINE_MAX - 1U;
    for(size_t i = 0; i < rlen; i++) a->buf[i] = rest[i];
    a->pos = (uint8_t)rlen;
    a->buf[a->pos] = '\0';
    return true;
}

/*
 * Feed one UART byte. Emits on '\n'/'\r', on a second BLE record boundary, or
 * when '#' abuts a BLE MAC (stopscan echo).
 */
static inline bool room_sweep_uart_feed_byte(
    RoomSweepUartLineAccum* a,
    char byte,
    char* out,
    size_t out_sz) {
    if(!a || !out || out_sz == 0) return false;

    if(byte == '\n' || byte == '\r') {
        return room_sweep_uart_line_commit(a, out, out_sz);
    }

    if(a->pos < ROOM_SWEEP_UART_LINE_MAX - 1U) {
        a->buf[a->pos++] = byte;
        a->buf[a->pos] = '\0';
    } else {
        /* Overflow: force-emit anything that looks like BLE, then restart. */
        a->buf[a->pos] = '\0';
        if(room_sweep_uart_find_ble_record(a->buf)) {
            bool emitted = room_sweep_uart_line_commit(a, out, out_sz);
            if(a->pos < ROOM_SWEEP_UART_LINE_MAX - 1U) {
                a->buf[a->pos++] = byte;
                a->buf[a->pos] = '\0';
            }
            return emitted;
        }
        a->pos = 0;
        a->buf[0] = byte;
        a->pos = 1;
        a->buf[1] = '\0';
        return false;
    }

    if(room_sweep_uart_try_emit_ble_record(a, out, out_sz)) return true;
    return false;
}

/* Idle / end-of-window: emit trailing BLE text (may contain several records). */
static inline bool room_sweep_uart_flush_ble_idle(
    RoomSweepUartLineAccum* a,
    char* out,
    size_t out_sz) {
    if(!a || a->pos == 0) return false;
    a->buf[a->pos] = '\0';
    if(!room_sweep_uart_find_ble_record(a->buf)) return false;
    return room_sweep_uart_line_commit(a, out, out_sz);
}

/* Haversine distance in meters. Lat/lon in decimal degrees. */
static inline float geo_distance_m(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371000.0f;
    const float DEG2RAD = 0.017453292519943295f;
    float p1 = lat1 * DEG2RAD;
    float p2 = lat2 * DEG2RAD;
    float dp = (lat2 - lat1) * DEG2RAD;
    float dl = (lon2 - lon1) * DEG2RAD;
    float sdp = sinf(dp * 0.5f);
    float sdl = sinf(dl * 0.5f);
    float a = sdp * sdp + cosf(p1) * cosf(p2) * sdl * sdl;
    if(a < 0.0f) a = 0.0f;
    if(a > 1.0f) a = 1.0f;
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return R * c;
}

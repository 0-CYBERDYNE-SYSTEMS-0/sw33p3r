/* nmea.c — minimal NMEA-0183 parser. Pure C, no Flipper deps, host-testable. */
#include "nmea.h"
#include <string.h>
#include <stdlib.h>

#define SENT_BUF_MAX 96

/* Per-parser line assembly buffer. Kept as file-static state so nmea_feed
 * stays allocation-free and callable with just (GpsFix*, char). One active
 * parser per app instance is the intended usage. */
static char s_buf[SENT_BUF_MAX];
static uint8_t s_pos;
static bool s_in_sentence;

static uint8_t hex2nib(char c) {
    if(c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if(c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if(c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

uint8_t nmea_checksum(const char* body) {
    uint8_t cs = 0;
    for(const char* p = body; *p && *p != '*'; p++) cs ^= (uint8_t)*p;
    return cs;
}

void nmea_init(GpsFix* fix) {
    memset(fix, 0, sizeof(*fix));
    s_pos = 0;
    s_in_sentence = false;
    memset(s_buf, 0, sizeof(s_buf));
}

/* Split sentence into up to max_fields comma-separated fields (in place).
 * Returns field count. Destructive on `s`. */
static int split_fields(char* s, char** fields, int max_fields) {
    int n = 0;
    char* p = s;
    fields[n++] = p;
    while(*p && n < max_fields) {
        if(*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

/* Parse NMEA latitude "ddmm.mmmm" + hemisphere into decimal degrees. */
static float parse_lat(const char* v, const char* hemi) {
    if(!v[0]) return 0.0f;
    float raw = strtof(v, NULL);
    int deg = (int)(raw / 100.0f);
    float min = raw - deg * 100.0f;
    float dd = deg + min / 60.0f;
    if(hemi[0] == 'S' || hemi[0] == 's') dd = -dd;
    return dd;
}

/* Parse NMEA longitude "dddmm.mmmm" + hemisphere into decimal degrees. */
static float parse_lon(const char* v, const char* hemi) {
    if(!v[0]) return 0.0f;
    float raw = strtof(v, NULL);
    int deg = (int)(raw / 100.0f);
    float min = raw - deg * 100.0f;
    float dd = deg + min / 60.0f;
    if(hemi[0] == 'W' || hemi[0] == 'w') dd = -dd;
    return dd;
}

/* Process one complete, checksum-verified sentence (no trailing CR/LF). */
static void process_sentence(GpsFix* fix, char* sent) {
    /* sent is like "GPGGA,123519,4807.038,N,..." (body between $ and *) */
    char* fields[20];
    int nf = split_fields(sent, fields, 20);
    if(nf < 1) return;

    /* Sentence type = last 3 chars of the talker+type field.
     * Handles GP (GPS), GN (multi-GNSS), BD (BeiDou), GL (GLONASS), etc. */
    const char* talker = fields[0];
    size_t tlen = strlen(talker);
    const char* type = talker + (tlen >= 3 ? tlen - 3 : 0);

    if(strcmp(type, "GGA") == 0) {
        /* $--GGA,time,lat,N,lon,E,quality,nsats,hdop,alt,M,geoid,M,,*cs */
        if(nf >= 10) {
            const char* t = fields[1];
            if(strlen(t) >= 6) {
                fix->hour = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
                fix->minute = (uint8_t)((t[2] - '0') * 10 + (t[3] - '0'));
                fix->second = (uint8_t)((t[4] - '0') * 10 + (t[5] - '0'));
                fix->has_time = true;
            }
            uint8_t q = (uint8_t)atoi(fields[6]);
            fix->fix_quality = q;
            fix->sats = (uint8_t)atoi(fields[7]);
            if(q > 0 && fields[2][0] && fields[4][0]) {
                fix->latitude = parse_lat(fields[2], fields[3]);
                fix->longitude = parse_lon(fields[4], fields[5]);
                fix->has_pos = true;
                fix->has_fix = true;
            } else {
                fix->has_fix = false;
                fix->has_pos = false;
            }
        }
    } else if(strcmp(type, "RMC") == 0) {
        /* $--RMC,time,status,lat,N,lon,E,speed,course,date,...*cs */
        if(nf >= 9) {
            const char* t = fields[1];
            if(strlen(t) >= 6) {
                fix->hour = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
                fix->minute = (uint8_t)((t[2] - '0') * 10 + (t[3] - '0'));
                fix->second = (uint8_t)((t[4] - '0') * 10 + (t[5] - '0'));
                fix->has_time = true;
            }
            bool active = (fields[2][0] == 'A');
            if(active) {
                fix->has_fix = true;
            } else {
                fix->has_fix = false;
                fix->has_pos = false;
            }
            if(active && fields[3][0] && fields[5][0]) {
                fix->latitude = parse_lat(fields[3], fields[4]);
                fix->longitude = parse_lon(fields[5], fields[6]);
                fix->has_pos = true;
            }
            if(fields[7][0]) fix->speed_kts = strtof(fields[7], NULL);
            if(fields[8][0]) fix->course = strtof(fields[8], NULL);
            /* RMC date: ddmmyy */
            const char* d = fields[9];
            if(strlen(d) >= 6) {
                fix->day = (uint8_t)((d[0] - '0') * 10 + (d[1] - '0'));
                fix->month = (uint8_t)((d[2] - '0') * 10 + (d[3] - '0'));
                fix->year = (uint16_t)(2000 + (d[4] - '0') * 10 + (d[5] - '0'));
                fix->has_date = true;
            }
        }
    } else if(strcmp(type, "GLL") == 0) {
        /* $--GLL,lat,N,lon,E,time,status,mode*cs
         * Broadcast every second even with no fix — gives us time. */
        if(nf >= 7) {
            const char* t = fields[5];
            if(strlen(t) >= 6) {
                fix->hour = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
                fix->minute = (uint8_t)((t[2] - '0') * 10 + (t[3] - '0'));
                fix->second = (uint8_t)((t[4] - '0') * 10 + (t[5] - '0'));
                fix->has_time = true;
            }
            bool active = (fields[6][0] == 'A');
            if(active && fields[1][0] && fields[3][0]) {
                fix->latitude = parse_lat(fields[1], fields[2]);
                fix->longitude = parse_lon(fields[3], fields[4]);
                fix->has_pos = true;
                fix->has_fix = true;
            } else {
                fix->has_fix = false;
                fix->has_pos = false;
            }
        }
    } else if(strcmp(type, "ZDA") == 0) {
        /* $--ZDA,time,day,month,year,local_h,local_m*cs */
        if(nf >= 5) {
            const char* t = fields[1];
            if(strlen(t) >= 6) {
                fix->hour = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
                fix->minute = (uint8_t)((t[2] - '0') * 10 + (t[3] - '0'));
                fix->second = (uint8_t)((t[4] - '0') * 10 + (t[5] - '0'));
                fix->has_time = true;
            }
            if(fields[2][0] && fields[3][0] && fields[4][0]) {
                fix->day = (uint8_t)atoi(fields[2]);
                fix->month = (uint8_t)atoi(fields[3]);
                fix->year = (uint16_t)atoi(fields[4]);
                fix->has_date = true;
            }
        }
    } else if(strcmp(type, "GSV") == 0) {
        /* $--GSV,total_msgs,msg_num,sats_in_view,...*cs
         * First message carries the total satellites-in-view count. */
        if(nf >= 4 && atoi(fields[2]) == 1) {
            fix->sats_in_view = (uint8_t)atoi(fields[3]);
        }
    }
}

void nmea_feed(GpsFix* fix, char c) {
    fix->rx_bytes++;

    if(c == '$') {
        s_pos = 0;
        s_in_sentence = true;
        s_buf[0] = '\0';
        return;
    }

    if(!s_in_sentence) return;

    if(c == '\r' || c == '\n') {
        /* end of sentence without checksum — discard */
        s_in_sentence = false;
        return;
    }

    if(c == '*') {
        /* checksum delimiter: next two chars are hex; we capture but the
         * terminating CR/LF will trigger validation. Mark end of body. */
        if(s_pos < SENT_BUF_MAX - 1) s_buf[s_pos++] = '*';
        s_buf[s_pos] = '\0';
        return;
    }

    if(s_pos < SENT_BUF_MAX - 1) {
        s_buf[s_pos++] = c;
        s_buf[s_pos] = '\0';
    } else {
        /* overflow — abort sentence */
        s_in_sentence = false;
    }

    /* Detect completion: body present, has '*', and we've accumulated the
     * two hex checksum chars. We treat "GPGGA,...*47" as ready. */
    char* star = strchr(s_buf, '*');
    if(star && (star - s_buf) >= 1 && strlen(star) == 3) {
        /* validate checksum */
        *star = '\0'; /* body now NUL-terminated */
        uint8_t calc = nmea_checksum(s_buf);
        uint8_t given = (uint8_t)((hex2nib(star[1]) << 4) | hex2nib(star[2]));
        if(calc == given) {
            fix->sentences++;
            process_sentence(fix, s_buf);
        }
        s_in_sentence = false;
        s_pos = 0;
        s_buf[0] = '\0';
    }
}

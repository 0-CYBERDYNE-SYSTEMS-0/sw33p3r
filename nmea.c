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

static int hex2nib(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
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

static bool parse_uint_field(const char* value, uint16_t min, uint16_t max, uint16_t* out) {
    if(!value[0]) return false;
    uint32_t parsed = 0;
    for(const char* p = value; *p; p++) {
        if(*p < '0' || *p > '9') return false;
        parsed = parsed * 10U + (uint32_t)(*p - '0');
        if(parsed > max) return false;
    }
    if(parsed < min) return false;
    *out = (uint16_t)parsed;
    return true;
}

static bool parse_fixed_uint(
    const char* value,
    size_t length,
    uint16_t min,
    uint16_t max,
    uint16_t* out) {
    uint32_t parsed = 0;
    for(size_t i = 0; i < length; i++) {
        if(value[i] < '0' || value[i] > '9') return false;
        parsed = parsed * 10U + (uint32_t)(value[i] - '0');
    }
    if(parsed < min || parsed > max) return false;
    *out = (uint16_t)parsed;
    return true;
}

static bool parse_time(const char* value, uint8_t* hour, uint8_t* minute, uint8_t* second) {
    size_t length = strlen(value);
    if(length < 6) return false;
    if(length > 6) {
        if(value[6] != '.' || length == 7) return false;
        for(size_t i = 7; i < length; i++) {
            if(value[i] < '0' || value[i] > '9') return false;
        }
    }
    uint16_t h;
    uint16_t m;
    uint16_t s;
    if(!parse_fixed_uint(value, 2, 0, 23, &h) ||
       !parse_fixed_uint(value + 2, 2, 0, 59, &m) ||
       !parse_fixed_uint(value + 4, 2, 0, 60, &s)) {
        return false;
    }
    *hour = (uint8_t)h;
    *minute = (uint8_t)m;
    *second = (uint8_t)s;
    return true;
}

static bool valid_date(uint16_t year, uint16_t month, uint16_t day) {
    if(month < 1 || month > 12) return false;
    uint16_t days = 31;
    if(month == 4 || month == 6 || month == 9 || month == 11) days = 30;
    if(month == 2) {
        bool leap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
        days = leap ? 29 : 28;
    }
    return day >= 1 && day <= days;
}

static bool parse_rmc_date(const char* value, uint16_t* year, uint8_t* month, uint8_t* day) {
    if(strlen(value) != 6) return false;
    uint16_t yy;
    uint16_t mm;
    uint16_t dd;
    if(!parse_fixed_uint(value, 2, 1, 31, &dd) ||
       !parse_fixed_uint(value + 2, 2, 1, 12, &mm) ||
       !parse_fixed_uint(value + 4, 2, 0, 99, &yy)) {
        return false;
    }
    uint16_t full_year = (uint16_t)(2000 + yy);
    if(!valid_date(full_year, mm, dd)) return false;
    *year = full_year;
    *month = (uint8_t)mm;
    *day = (uint8_t)dd;
    return true;
}

static bool parse_float_field(const char* value, float min, float max, float* out) {
    if(!value[0]) {
        *out = 0.0f;
        return true;
    }
    char* end;
    float parsed = strtof(value, &end);
    if(end == value || *end != '\0' || parsed != parsed || parsed < min || parsed > max) {
        return false;
    }
    *out = parsed;
    return true;
}

static bool parse_coordinate(const char* v, const char* hemi, bool longitude, float* out) {
    if(!v[0] || !hemi[0]) return false;
    char* end;
    float raw = strtof(v, &end);
    float max_raw = longitude ? 18000.0f : 9000.0f;
    if(end == v || *end != '\0' || raw != raw || raw < 0.0f || raw > max_raw) return false;

    int deg = (int)(raw / 100.0f);
    float min = raw - deg * 100.0f;
    int max_deg = longitude ? 180 : 90;
    if(min >= 60.0f || (deg == max_deg && min > 0.0f)) return false;
    if(longitude) {
        if(hemi[0] != 'E' && hemi[0] != 'e' && hemi[0] != 'W' && hemi[0] != 'w') return false;
    } else if(hemi[0] != 'N' && hemi[0] != 'n' && hemi[0] != 'S' && hemi[0] != 's') {
        return false;
    }

    *out = deg + min / 60.0f;
    if((longitude && (hemi[0] == 'W' || hemi[0] == 'w')) ||
       (!longitude && (hemi[0] == 'S' || hemi[0] == 's'))) {
        *out = -*out;
    }
    return true;
}

static bool parse_lat(const char* v, const char* hemi, float* out) {
    return parse_coordinate(v, hemi, false, out);
}

static bool parse_lon(const char* v, const char* hemi, float* out) {
    return parse_coordinate(v, hemi, true, out);
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
            uint16_t q;
            uint16_t sats;
            if(!parse_uint_field(fields[6], 0, 8, &q) ||
               !parse_uint_field(fields[7], 0, 99, &sats)) {
                return;
            }
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
            bool has_time = !fields[1][0] || parse_time(fields[1], &hour, &minute, &second);
            if(!has_time) return;
            fix->nav_sentences++;
            if(fields[1][0]) {
                fix->hour = hour;
                fix->minute = minute;
                fix->second = second;
                fix->has_time = true;
            }
            fix->fix_quality = (uint8_t)q;
            fix->sats = (uint8_t)sats;
            float latitude;
            float longitude;
            if(q > 0 && parse_lat(fields[2], fields[3], &latitude) &&
               parse_lon(fields[4], fields[5], &longitude)) {
                fix->latitude = latitude;
                fix->longitude = longitude;
                fix->has_pos = true;
                fix->has_fix = true;
            } else {
                fix->has_fix = q > 0;
                fix->has_pos = false;
            }
        }
    } else if(strcmp(type, "RMC") == 0) {
        /* $--RMC,time,status,lat,N,lon,E,speed,course,date,...*cs */
        if(nf >= 10) {
            if(fields[2][0] != 'A' && fields[2][0] != 'V') return;
            float speed;
            float course;
            if(!parse_float_field(fields[7], 0.0f, 1000.0f, &speed) ||
               !parse_float_field(fields[8], 0.0f, 360.0f, &course)) {
                return;
            }
            uint16_t date_year = 0;
            uint8_t date_month = 0;
            uint8_t date_day = 0;
            bool has_date = false;
            if(fields[9][0]) {
                if(!parse_rmc_date(fields[9], &date_year, &date_month, &date_day)) return;
                has_date = true;
            }
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
            bool has_time = !fields[1][0] || parse_time(fields[1], &hour, &minute, &second);
            if(!has_time) return;
            fix->nav_sentences++;
            if(fields[1][0]) {
                fix->hour = hour;
                fix->minute = minute;
                fix->second = second;
                fix->has_time = true;
            }
            bool active = (fields[2][0] == 'A');
            if(active) {
                fix->has_fix = true;
                fix->has_pos = false;
            } else {
                fix->has_fix = false;
                fix->has_pos = false;
            }
            float latitude;
            float longitude;
            if(active && parse_lat(fields[3], fields[4], &latitude) &&
               parse_lon(fields[5], fields[6], &longitude)) {
                fix->latitude = latitude;
                fix->longitude = longitude;
                fix->has_pos = true;
            }
            fix->speed_kts = speed;
            fix->course = course;
            if(has_date) {
                fix->day = date_day;
                fix->month = date_month;
                fix->year = date_year;
                fix->has_date = true;
            }
        }
    } else if(strcmp(type, "GLL") == 0) {
        /* $--GLL,lat,N,lon,E,time,status,mode*cs
         * Broadcast every second even with no fix — gives us time. */
        if(nf >= 7) {
            if(fields[6][0] != 'A' && fields[6][0] != 'V') return;
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
            bool has_time = !fields[5][0] || parse_time(fields[5], &hour, &minute, &second);
            if(!has_time) return;
            fix->nav_sentences++;
            if(fields[5][0]) {
                fix->hour = hour;
                fix->minute = minute;
                fix->second = second;
                fix->has_time = true;
            }
            bool active = (fields[6][0] == 'A');
            float latitude;
            float longitude;
            if(active && parse_lat(fields[1], fields[2], &latitude) &&
               parse_lon(fields[3], fields[4], &longitude)) {
                fix->latitude = latitude;
                fix->longitude = longitude;
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
            uint8_t hour;
            uint8_t minute;
            uint8_t second;
            bool has_time = !fields[1][0] || parse_time(fields[1], &hour, &minute, &second);
            if(!has_time) return;
            if(fields[1][0]) {
                fix->hour = hour;
                fix->minute = minute;
                fix->second = second;
                fix->has_time = true;
            }
            bool has_date_fields = fields[2][0] || fields[3][0] || fields[4][0];
            if(has_date_fields) {
                uint16_t day;
                uint16_t month;
                uint16_t year;
                if(!fields[2][0] || !fields[3][0] || !fields[4][0] ||
                   !parse_uint_field(fields[2], 1, 31, &day) ||
                   !parse_uint_field(fields[3], 1, 12, &month) ||
                   !parse_uint_field(fields[4], 1, 9999, &year) ||
                   !valid_date(year, month, day)) return;
                fix->day = (uint8_t)day;
                fix->month = (uint8_t)month;
                fix->year = year;
                fix->has_date = true;
            }
        }
    } else if(strcmp(type, "GSV") == 0) {
        /* $--GSV,total_msgs,msg_num,sats_in_view,...*cs
         * First message carries the total satellites-in-view count. */
        uint16_t message;
        uint16_t sats;
        if(nf >= 4 && parse_uint_field(fields[2], 1, 99, &message) &&
           parse_uint_field(fields[3], 0, 99, &sats) && message == 1) {
            fix->sats_in_view = (uint8_t)sats;
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
        int high = hex2nib(star[1]);
        int low = hex2nib(star[2]);
        if(high >= 0 && low >= 0) {
            uint8_t calc = nmea_checksum(s_buf);
            uint8_t given = (uint8_t)((high << 4) | low);
            if(calc == given) {
                fix->sentences++;
                process_sentence(fix, s_buf);
            }
        }
        s_in_sentence = false;
        s_pos = 0;
        s_buf[0] = '\0';
    }
}

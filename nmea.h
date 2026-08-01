#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Minimal, dependency-free NMEA-0183 parser for GPS position/time.
 * No allocation. Feed it one char at a time via nmea_feed(); it updates
 * the GpsFix struct in place. Designed to be host-testable (pure C, no
 * Flipper includes) so it can be verified before deploying to the device.
 * ------------------------------------------------------------------------- */

#define NMEA_FIELD_MAX 24

typedef struct {
    /* validity */
    bool has_time;
    bool has_fix;        // GGA fix quality > 0 or RMC status 'A'
    bool has_pos;
    bool has_date;       // ZDA or RMC date parsed

    /* time (UTC) */
    uint8_t hour, minute, second;

    /* date (from ZDA or RMC) */
    uint16_t year;
    uint8_t month, day;

    /* position */
    float latitude;     // decimal degrees, N positive / S negative
    float longitude;    // decimal degrees, E positive / W negative

    /* quality / motion */
    uint8_t fix_quality; // GGA quality: 0=invalid,1=GPS,2=DGPS,...
    uint8_t sats;        // satellites used (GGA)
    uint8_t sats_in_view;// satellites in view (GSV total)
    float speed_kts;    // RMC speed over ground
    float course;       // RMC course over ground

    /* bookkeeping */
    uint32_t sentences;  // valid checksummed sentences seen
    uint32_t rx_bytes;   // total bytes fed in
} GpsFix;

/* Reset fix to a clean no-data state. */
void nmea_init(GpsFix* fix);

/* Feed one received byte. Cheap; safe to call from a thread/ISR context
 * as long as the same GpsFix isn't fed from two contexts at once. */
void nmea_feed(GpsFix* fix, char c);

/* Compute NMEA checksum over the chars between '$' and '*'. Exposed for
 * unit testing. Returns 8-bit XOR checksum. */
uint8_t nmea_checksum(const char* sentence_without_dollar);

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

typedef enum {
    RoomSweepReportSensorRf = 1U << 0,
    RoomSweepReportSensorWifi = 1U << 1,
    RoomSweepReportSensorBle = 1U << 2,
    RoomSweepReportSensorGps = 1U << 3,
    RoomSweepReportSensorNrf24 = 1U << 4,
} RoomSweepReportSensor;

#define ROOM_SWEEP_REPORT_ALL_SENSORS \
    ((uint8_t)(RoomSweepReportSensorRf | RoomSweepReportSensorWifi | \
               RoomSweepReportSensorBle | RoomSweepReportSensorGps | \
               RoomSweepReportSensorNrf24))

typedef enum {
    RoomSweepReportTxNone = 0,
    RoomSweepReportTxArmed,
    RoomSweepReportTxStarted,
    RoomSweepReportTxCompleted,
    RoomSweepReportTxRefused,
    RoomSweepReportTxAborted,
} RoomSweepReportTxState;

typedef enum {
    RoomSweepReportActivityQuiet = 0,
    RoomSweepReportActivitySome,
    RoomSweepReportActivityBusy,
} RoomSweepReportActivity;

typedef struct {
    bool complete;
    uint8_t sensors_confirmed;
    uint8_t sensors_unavailable;
    RoomSweepReportTxState tx_state;
    bool tx_started;
    uint32_t dropped_events;
    bool storage_error;
    bool gps_included;
    bool gps_omitted;
} RoomSweepReportState;

/* Counts used to write a plain-English Room Report body. */
typedef struct {
    uint32_t rf_observations;
    int rf_strongest_rssi;
    uint32_t rf_strongest_hz;
    uint32_t wifi_observations;
    uint32_t wifi_windows;
    int wifi_strongest_rssi;
    uint32_t ble_observations;
    uint32_t ble_windows;
    int ble_strongest_rssi;
    uint32_t nrf_observations;
    uint32_t nrf_active_channels;
    uint8_t nrf_top_channel;
    uint32_t gps_snapshots;
    bool full_sweep_completed;
} RoomSweepReportFindings;

static inline void room_sweep_report_init(RoomSweepReportState* state) {
    if(!state) return;
    *state = (RoomSweepReportState){
        .complete = true,
        .tx_state = RoomSweepReportTxNone,
    };
}

static inline void room_sweep_report_findings_init(RoomSweepReportFindings* f) {
    if(!f) return;
    *f = (RoomSweepReportFindings){
        .rf_strongest_rssi = -127,
        .wifi_strongest_rssi = -127,
        .ble_strongest_rssi = -127,
    };
}

static inline void room_sweep_report_mark_incomplete(RoomSweepReportState* state) {
    if(state) state->complete = false;
}

static inline void room_sweep_report_sensor_confirmed(
    RoomSweepReportState* state,
    RoomSweepReportSensor sensor) {
    if(!state) return;
    state->sensors_confirmed |= (uint8_t)sensor;
    state->sensors_unavailable &= (uint8_t)~sensor;
}

static inline void room_sweep_report_sensor_unavailable(
    RoomSweepReportState* state,
    RoomSweepReportSensor sensor) {
    if(!state) return;
    state->sensors_unavailable |= (uint8_t)sensor;
    state->sensors_confirmed &= (uint8_t)~sensor;
}

static inline void room_sweep_report_note_drop(
    RoomSweepReportState* state,
    uint32_t count) {
    if(!state || count == 0) return;
    state->dropped_events += count;
    state->complete = false;
}

static inline void room_sweep_report_note_storage_error(RoomSweepReportState* state) {
    if(!state) return;
    state->storage_error = true;
    state->complete = false;
}

static inline void room_sweep_report_set_gps_included(RoomSweepReportState* state) {
    if(!state) return;
    state->gps_included = true;
    state->gps_omitted = false;
}

static inline void room_sweep_report_set_gps_omitted(RoomSweepReportState* state) {
    if(!state) return;
    state->gps_included = false;
    state->gps_omitted = true;
}

static inline void room_sweep_report_set_tx(
    RoomSweepReportState* state,
    RoomSweepReportTxState tx_state) {
    if(!state) return;
    if(tx_state == RoomSweepReportTxStarted) state->tx_started = true;
    state->tx_state = tx_state;
}

static inline bool room_sweep_report_is_clean(const RoomSweepReportState* state) {
    return state && state->complete && state->dropped_events == 0 && !state->storage_error;
}

/* FULL when every non-unavailable sensor is confirmed. */
static inline bool room_sweep_report_coverage_full(const RoomSweepReportState* state) {
    if(!state) return false;
    uint8_t needed =
        (uint8_t)(ROOM_SWEEP_REPORT_ALL_SENSORS & (uint8_t)~state->sensors_unavailable);
    if(needed == 0) return false;
    return (state->sensors_confirmed & needed) == needed;
}

static inline RoomSweepReportActivity room_sweep_report_activity(
    const RoomSweepReportFindings* f) {
    if(!f) return RoomSweepReportActivityQuiet;
    uint32_t hits = f->rf_observations + f->wifi_observations + f->ble_observations +
                    f->nrf_observations;
    uint32_t active_nrf = f->nrf_active_channels;
    if(hits >= 12U || active_nrf >= 8U) return RoomSweepReportActivityBusy;
    if(hits >= 1U || active_nrf >= 1U) return RoomSweepReportActivitySome;
    return RoomSweepReportActivityQuiet;
}

static inline const char* room_sweep_report_activity_text(RoomSweepReportActivity a) {
    switch(a) {
    case RoomSweepReportActivityBusy:
        return "busy";
    case RoomSweepReportActivitySome:
        return "some activity";
    case RoomSweepReportActivityQuiet:
    default:
        return "quiet";
    }
}

static inline const char* room_sweep_report_tx_text(RoomSweepReportTxState tx_state) {
    switch(tx_state) {
    case RoomSweepReportTxArmed:
        return "armed";
    case RoomSweepReportTxStarted:
        return "started";
    case RoomSweepReportTxCompleted:
        return "completed";
    case RoomSweepReportTxRefused:
        return "refused";
    case RoomSweepReportTxAborted:
        return "aborted";
    case RoomSweepReportTxNone:
    default:
        return "none";
    }
}

static inline void room_sweep_report_append(
    char* output,
    size_t capacity,
    size_t* used,
    const char* format,
    ...) {
    if(!output || !used || !format || *used >= capacity) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + *used, capacity - *used, format, args);
    va_end(args);
    if(written < 0) return;
    size_t available = capacity - *used;
    *used += (size_t)written < available ? (size_t)written : available - 1U;
}

static inline void room_sweep_report_append_sensor_names(
    char* output,
    size_t capacity,
    size_t* used,
    uint8_t sensors) {
    static const struct {
        uint8_t mask;
        const char* name;
    } names[] = {
        {(uint8_t)RoomSweepReportSensorRf, "RF"},
        {(uint8_t)RoomSweepReportSensorWifi, "Wi-Fi"},
        {(uint8_t)RoomSweepReportSensorBle, "BLE"},
        {(uint8_t)RoomSweepReportSensorGps, "GPS"},
        {(uint8_t)RoomSweepReportSensorNrf24, "nRF24"},
    };
    bool first = true;
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if((sensors & names[i].mask) == 0) continue;
        room_sweep_report_append(output, capacity, used, "%s%s", first ? "" : ", ", names[i].name);
        first = false;
    }
    if(first) room_sweep_report_append(output, capacity, used, "none");
}

/* Coverage header. Returns bytes written excluding NUL. */
static inline size_t room_sweep_report_format(
    const RoomSweepReportState* state,
    char* output,
    size_t capacity) {
    if(!output || capacity == 0) return 0;
    output[0] = '\0';
    if(!state) return 0;

    size_t used = 0;
    uint8_t all_sensors = ROOM_SWEEP_REPORT_ALL_SENSORS;
    room_sweep_report_append(output, capacity, &used, "ROOM REPORT\n");
    room_sweep_report_append(
        output,
        capacity,
        &used,
        "Status: %s\nCoverage: %s\n",
        room_sweep_report_is_clean(state) ? "COMPLETE" : "INCOMPLETE",
        room_sweep_report_coverage_full(state) ? "FULL" : "PARTIAL");
    room_sweep_report_append(output, capacity, &used, "Sensors confirmed: ");
    room_sweep_report_append_sensor_names(output, capacity, &used, state->sensors_confirmed);
    room_sweep_report_append(output, capacity, &used, "; unavailable: ");
    room_sweep_report_append_sensor_names(output, capacity, &used, state->sensors_unavailable);
    room_sweep_report_append(output, capacity, &used, "; not confirmed: ");
    room_sweep_report_append_sensor_names(
        output,
        capacity,
        &used,
        (uint8_t)(all_sensors &
                  (uint8_t)~(state->sensors_confirmed | state->sensors_unavailable)));
    room_sweep_report_append(
        output,
        capacity,
        &used,
        "\nTX: %s%s\n",
        room_sweep_report_tx_text(state->tx_state),
        state->tx_started && state->tx_state != RoomSweepReportTxStarted &&
                state->tx_state != RoomSweepReportTxCompleted ?
            " (started earlier)" :
            "");
    room_sweep_report_append(
        output, capacity, &used, "Dropped events: %lu\n", (unsigned long)state->dropped_events);
    room_sweep_report_append(
        output, capacity, &used, "Storage: %s\n", state->storage_error ? "error" : "ok");
    room_sweep_report_append(
        output,
        capacity,
        &used,
        "GPS: %s\n",
        state->gps_included ? "included" : state->gps_omitted ? "omitted" : "not specified");
    room_sweep_report_append(
        output,
        capacity,
        &used,
        "Limitations: RSSI is not distance; wireless evidence does not prove Internet telemetry, recording, ownership, or intent; no observation does not prove absence; bounded carrier is not replay; nRF24 RPD is channel activity only.\n");
    return used;
}

/* Append plain-English findings after the coverage header. */
static inline size_t room_sweep_report_append_findings(
    const RoomSweepReportFindings* f,
    char* output,
    size_t capacity,
    size_t used) {
    if(!output || capacity == 0) return used;
    if(!f) return used;

    RoomSweepReportActivity act = room_sweep_report_activity(f);
    room_sweep_report_append(
        output,
        capacity,
        &used,
        "Summary: This room looked %s on the sensors that ran.\n",
        room_sweep_report_activity_text(act));
    if(f->full_sweep_completed) {
        room_sweep_report_append(
            output, capacity, &used, "Full room sweep: finished in this session.\n");
    }

    if(f->rf_observations > 0) {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "Sub-GHz RF: %lu hits; strongest about %ddBm near %lu Hz.\n",
            (unsigned long)f->rf_observations,
            f->rf_strongest_rssi,
            (unsigned long)f->rf_strongest_hz);
    } else {
        room_sweep_report_append(
            output, capacity, &used, "Sub-GHz RF: no recorded hits in this session.\n");
    }

    if(f->wifi_observations > 0) {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "Wi-Fi: %lu AP observations across %lu scan windows; strongest about %ddBm.\n",
            (unsigned long)f->wifi_observations,
            (unsigned long)f->wifi_windows,
            f->wifi_strongest_rssi);
    } else {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "Wi-Fi: no AP beacons in %lu scan windows.\n",
            (unsigned long)f->wifi_windows);
    }

    if(f->ble_observations > 0) {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "BLE: %lu device observations across %lu scan windows; strongest about %ddBm.\n",
            (unsigned long)f->ble_observations,
            (unsigned long)f->ble_windows,
            f->ble_strongest_rssi);
    } else {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "BLE: no advertisements in %lu scan windows.\n",
            (unsigned long)f->ble_windows);
    }

    if(f->nrf_observations > 0 || f->nrf_active_channels > 0) {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "nRF24 (2.4 GHz): activity on %lu channels; top channel %u; %lu RPD hits. Receive check only.\n",
            (unsigned long)f->nrf_active_channels,
            (unsigned)f->nrf_top_channel,
            (unsigned long)f->nrf_observations);
    } else {
        room_sweep_report_append(
            output,
            capacity,
            &used,
            "nRF24 (2.4 GHz): no channel activity recorded (module off, wrong SPI switch, or quiet band).\n");
    }

    room_sweep_report_append(
        output,
        capacity,
        &used,
        "GPS: %lu snapshots. Exact coordinates only if GPS Log was enabled.\n",
        (unsigned long)f->gps_snapshots);
    return used;
}

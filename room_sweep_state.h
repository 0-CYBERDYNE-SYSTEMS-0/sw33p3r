#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Keep the signal handoff contract independent of Flipper/Furi headers. */
#define ROOM_SWEEP_SIGNAL_THRESHOLD_DBM (-75.0f)
#define ROOM_SWEEP_CANDIDATE_EXPIRY_MS 30000U

typedef enum {
    RoomSweepCandidateNone = 0,
    RoomSweepCandidateSurvey,
    RoomSweepCandidateSweep,
    RoomSweepCandidatePeak,
} RoomSweepCandidateSource;

static inline const char* room_sweep_candidate_source_text(RoomSweepCandidateSource source) {
    switch(source) {
    case RoomSweepCandidateSurvey:
        return "Survey";
    case RoomSweepCandidateSweep:
        return "Sweep";
    case RoomSweepCandidatePeak:
        return "Refine";
    case RoomSweepCandidateNone:
    default:
        return "Unknown";
    }
}

typedef struct {
    uint32_t requested_hz;
    uint32_t tuned_hz;
    float rssi;
    uint32_t tick;
    RoomSweepCandidateSource source;
    bool valid;
} RoomSweepSignalCandidate;

static inline bool room_sweep_candidate_publish(
    RoomSweepSignalCandidate* candidate,
    uint32_t requested_hz,
    uint32_t tuned_hz,
    float rssi,
    uint32_t tick,
    bool completed,
    RoomSweepCandidateSource source) {
    /* A noise result must not destroy a previous usable candidate. */
    if(!candidate || !completed || requested_hz == 0 || tuned_hz == 0 ||
       rssi != rssi || rssi > 0.0f || rssi <= ROOM_SWEEP_SIGNAL_THRESHOLD_DBM ||
       source <= RoomSweepCandidateNone || source > RoomSweepCandidatePeak) {
        return false;
    }

    candidate->requested_hz = requested_hz;
    candidate->tuned_hz = tuned_hz;
    candidate->rssi = rssi;
    candidate->tick = tick;
    candidate->source = source;
    candidate->valid = true;
    return true;
}

static inline bool room_sweep_candidate_is_fresh(
    const RoomSweepSignalCandidate* candidate,
    uint32_t now) {
    if(!candidate || !candidate->valid || candidate->requested_hz == 0 ||
       candidate->tuned_hz == 0) {
        return false;
    }

    /* Unsigned subtraction intentionally handles a uint32_t tick wrap. */
    return (uint32_t)(now - candidate->tick) < ROOM_SWEEP_CANDIDATE_EXPIRY_MS;
}

static inline bool room_sweep_candidate_matches_request(
    const RoomSweepSignalCandidate* candidate,
    uint32_t requested_hz,
    uint32_t now) {
    return room_sweep_candidate_is_fresh(candidate, now) &&
           candidate->requested_hz == requested_hz;
}

static inline void room_sweep_candidate_invalidate(RoomSweepSignalCandidate* candidate) {
    if(candidate) candidate->valid = false;
}

static inline uint32_t room_sweep_candidate_handoff_frequency(
    const RoomSweepSignalCandidate* candidate,
    uint32_t now,
    uint32_t preset_hz,
    bool* used_candidate) {
    bool fresh = room_sweep_candidate_is_fresh(candidate, now);
    if(used_candidate) *used_candidate = fresh;
    return fresh ? candidate->tuned_hz : preset_hz;
}

/* ext_band: 0=Auto, 1=400 MHz path, 2=900 MHz path. */
static inline bool room_sweep_external_band_allows(uint8_t ext_band, uint32_t frequency_hz) {
    if(ext_band == 0) return false;
    if(ext_band == 1) return frequency_hz >= 387000000U && frequency_hz <= 464000000U;
    if(ext_band == 2) return frequency_hz >= 779000000U && frequency_hz <= 928000000U;
    return false;
}

/*
 * Map a frequency onto the BFFB dual-CC1101 top-switch path.
 * Returns 1 (400), 2 (900), or 0 if the freq is not on those paths
 * (typically the ~300 MHz CC1101 band — internal radio only).
 */
static inline uint8_t room_sweep_external_band_for_frequency(uint32_t frequency_hz) {
    if(frequency_hz >= 387000000U && frequency_hz <= 464000000U) return 1;
    if(frequency_hz >= 779000000U && frequency_hz <= 928000000U) return 2;
    return 0;
}

/* True if frequency sits in any standard CC1101 operating band. */
static inline bool room_sweep_cc1101_band_covers(uint32_t frequency_hz) {
    if(frequency_hz >= 300000000U && frequency_hz <= 348000000U) return true;
    if(frequency_hz >= 387000000U && frequency_hz <= 464000000U) return true;
    if(frequency_hz >= 779000000U && frequency_hz <= 928000000U) return true;
    return false;
}

typedef enum {
    RoomSweepTxWorkerIdle = 0,
    RoomSweepTxWorkerRunning,
    RoomSweepTxWorkerStopping,
} RoomSweepTxWorkerState;

typedef enum {
    RoomSweepTxInputDisarmed = 0,
    RoomSweepTxInputArmed,
    RoomSweepTxInputStarting,
    RoomSweepTxInputTransmitting,
} RoomSweepTxInputState;

typedef enum {
    RoomSweepTxDecisionNoop = 0,
    RoomSweepTxDecisionArm,
    RoomSweepTxDecisionStart,
} RoomSweepTxDecision;

typedef enum {
    RoomSweepTxRefusalNone = 0,
    RoomSweepTxRefusalNoRadio,
    RoomSweepTxRefusalInvalidFrequency,
    RoomSweepTxRefusalExpiredCandidate,
    RoomSweepTxRefusalExtBandUnknown,
    RoomSweepTxRefusalPolicy,
    RoomSweepTxRefusalStartFailed,
    RoomSweepTxRefusalCanceled,
} RoomSweepTxRefusal;

static inline const char* room_sweep_tx_refusal_text(RoomSweepTxRefusal refusal) {
    switch(refusal) {
    case RoomSweepTxRefusalNoRadio:
        return "No radio";
    case RoomSweepTxRefusalInvalidFrequency:
        return "Frequency unavailable";
    case RoomSweepTxRefusalExpiredCandidate:
        return "RX candidate expired";
    case RoomSweepTxRefusalExtBandUnknown:
        return "Select EXT 400/900";
    case RoomSweepTxRefusalPolicy:
        return "Freq not allowed in region";
    case RoomSweepTxRefusalStartFailed:
        return "TX start failed";
    case RoomSweepTxRefusalCanceled:
        return "TX canceled";
    case RoomSweepTxRefusalNone:
    default:
        return "";
    }
}

/*
 * Flipper region gate for TX.
 *
 * hardware_region_provisioned "--" / is_provisioned()==false means NO band
 * table is installed — not "every frequency is forbidden". Treating that as a
 * hard ban blocked all TX on devices that never ran official region
 * provisioning (common on Momentum). Radio validity + ExtBand checks remain.
 *
 * When provisioned, honor the installed region band table.
 */
static inline bool room_sweep_tx_region_allows(
    bool region_provisioned,
    bool frequency_allowed_by_region) {
    if(!region_provisioned) return true;
    return frequency_allowed_by_region;
}

static inline RoomSweepTxDecision room_sweep_tx_ok_decision(
    RoomSweepTxInputState state,
    RoomSweepTxWorkerState worker,
    bool frequency_valid,
    bool long_press) {
    if(long_press) {
        return state == RoomSweepTxInputArmed && worker == RoomSweepTxWorkerIdle && frequency_valid
                   ? RoomSweepTxDecisionStart
                   : RoomSweepTxDecisionNoop;
    }

    return state == RoomSweepTxInputDisarmed && worker == RoomSweepTxWorkerIdle && frequency_valid
               ? RoomSweepTxDecisionArm
               : RoomSweepTxDecisionNoop;
}

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Keep the signal handoff contract independent of Flipper/Furi headers. */

/*
 * ROOM_SWEEP_SIGNAL_THRESHOLD_DBM is an ENERGY gate only: a channel-average
 * RSSI strictly above -75 dBm qualifies a "candidate" (energy present on
 * that frequency). It is NOT protocol, modulation, packet-content, source,
 * or device-type identification — a CC1101 RSSI readout cannot identify any
 * of those. UI copy must describe it as energy above a threshold.
 */
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

/*
 * ext_band: 0=Auto, 1=400 MHz path, 2=900 MHz path.
 * AUTO IS A CONFIGURED ASSUMPTION, NOT A DETECTION: the app has no way to
 * sense the physical BFFB bottom/top antenna-switch position. Auto assumes
 * the operator matched the switch to the band in use; it never reads the
 * switch. Do not add switch sensing (out of scope by MISSION contract).
 */
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

/*
 * Honest short on-screen note for the ExtBand selection. The Auto text
 * states the configured assumption explicitly (no switch sensing exists).
 */
static inline const char* room_sweep_ext_band_ui_note(uint8_t ext_band) {
    switch(ext_band) {
    case 1:
        return "EXT 400";
    case 2:
        return "EXT 900";
    default:
        return "AUTO: assumed path";
    }
}

/* True if frequency sits in any standard CC1101 operating band. */
static inline bool room_sweep_cc1101_band_covers(uint32_t frequency_hz) {
    if(frequency_hz >= 300000000U && frequency_hz <= 348000000U) return true;
    if(frequency_hz >= 387000000U && frequency_hz <= 464000000U) return true;
    if(frequency_hz >= 779000000U && frequency_hz <= 928000000U) return true;
    return false;
}

/*
 * Survey bar shading (RF Survey screen). The baseline snapshot is a DISPLAY
 * REFERENCE only: it never qualifies a signal candidate, gates the alert,
 * or feeds TX handoff — those use the absolute energy threshold alone.
 * A bar renders filled when the live average RSSI is above the threshold
 * (the same condition that publishes a candidate) or, when a baseline
 * snapshot exists, more than ROOM_SWEEP_BASELINE_DELTA_DB above that
 * channel's snapshot (a room-energy change indicator, not a signal claim).
 */
#define ROOM_SWEEP_BASELINE_DELTA_DB 3.0f
static inline bool room_sweep_survey_bar_filled(
    float rssi,
    bool baseline_set,
    float baseline_rssi) {
    if(rssi > ROOM_SWEEP_SIGNAL_THRESHOLD_DBM) return true;
    return baseline_set && rssi > baseline_rssi + ROOM_SWEEP_BASELINE_DELTA_DB;
}

/*
 * Honest display rounding for a sweep/refine peak frequency. The receiver
 * runs the OOK 650 kHz preset, so energy anywhere within ~±325 kHz of the
 * tuned point contributes to the RSSI peak — the emitting source can sit
 * that far from the reported center. Round the displayed estimate to
 * 100 kHz and always show it with a "~" / "650k BW" marker; never present
 * kHz-level peak precision on screen.
 */
static inline uint32_t room_sweep_peak_freq_approx(uint32_t freq_hz) {
    return ((freq_hz + 50000U) / 100000U) * 100000U;
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

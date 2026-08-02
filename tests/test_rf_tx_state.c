#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "../room_sweep_state.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    RoomSweepSignalCandidate candidate = {0};

    check(
        !room_sweep_candidate_is_fresh(&candidate, 0),
        "empty candidate is not fresh");
    check(
        room_sweep_candidate_publish(
            &candidate, 433920000, 433919998, -74.9f, 0, true, RoomSweepCandidateSurvey),
        "threshold-qualified survey result publishes");
    check(candidate.valid && candidate.requested_hz == 433920000 &&
              candidate.tuned_hz == 433919998 && candidate.source == RoomSweepCandidateSurvey,
          "candidate stores requested and tuned frequencies");
    check(
        room_sweep_candidate_is_fresh(&candidate, ROOM_SWEEP_CANDIDATE_EXPIRY_MS - 1),
        "candidate is fresh just before expiry");
    check(
        room_sweep_candidate_matches_request(&candidate, 433920000, 1),
        "fresh candidate matches its requested scan channel");
    check(
        !room_sweep_candidate_matches_request(&candidate, 433000000, 1),
        "RF lock rejects a different requested scan channel");
    check(
        !room_sweep_candidate_is_fresh(&candidate, ROOM_SWEEP_CANDIDATE_EXPIRY_MS),
        "candidate expires at 30 seconds");
    check(
        !room_sweep_candidate_matches_request(
            &candidate, 433920000, ROOM_SWEEP_CANDIDATE_EXPIRY_MS),
        "RF lock expires with its source candidate");

    uint32_t old_tuned = candidate.tuned_hz;
    check(
        !room_sweep_candidate_publish(
            &candidate,
            433920000,
            433920000,
            -75.0f,
            100,
            true,
            RoomSweepCandidateSweep),
        "threshold equality is not a signal candidate");
    check(
        candidate.tuned_hz == old_tuned && candidate.source == RoomSweepCandidateSurvey,
        "noise does not overwrite prior candidate");
    check(
        !room_sweep_candidate_publish(
            &candidate, 433920000, 433919998, NAN, 100, true, RoomSweepCandidateSurvey),
        "NaN RSSI cannot publish a candidate");
    check(
        !room_sweep_candidate_publish(
            &candidate, 433920000, 433919998, INFINITY, 100, true,
            RoomSweepCandidateSurvey),
        "infinite RSSI cannot publish a candidate");
    check(
        !room_sweep_candidate_publish(
            &candidate, 433920000, 433919998, -60.0f, 100, true,
            (RoomSweepCandidateSource)99),
        "unknown candidate source is rejected");
    check(
        !room_sweep_candidate_publish(
            &candidate,
            433920000,
            433919998,
            -50.0f,
            101,
            false,
            RoomSweepCandidateSweep),
        "canceled sweep cannot publish a candidate");
    check(
        candidate.tuned_hz == old_tuned && candidate.source == RoomSweepCandidateSurvey,
        "canceled sweep preserves prior candidate");

    check(
        room_sweep_candidate_publish(
            &candidate,
            868350000,
            868349992,
            -65.0f,
            200,
            true,
            RoomSweepCandidateSweep),
        "completed coarse sweep publishes its source and tuned frequency");
    check(
        candidate.source == RoomSweepCandidateSweep && candidate.tuned_hz == 868349992,
        "coarse sweep provenance is retained");

    check(
        room_sweep_candidate_publish(
            &candidate, 418000000, 417999996, -60.0f, UINT32_MAX - 100,
            true,
            RoomSweepCandidatePeak),
        "peak result publishes near tick wrap");
    check(
        room_sweep_candidate_is_fresh(&candidate, 50),
        "candidate freshness is wrap-safe");
    room_sweep_candidate_invalidate(&candidate);
    check(!room_sweep_candidate_is_fresh(&candidate, 51), "explicit invalidation blocks handoff");
    bool used_candidate = true;
    check(
        room_sweep_candidate_handoff_frequency(
            &candidate, 51, 433920000, &used_candidate) == 433920000 && !used_candidate,
        "invalid candidate handoff restores the safe preset");
    check(
        room_sweep_candidate_publish(
            &candidate, 315000000, 314999997, -60.0f, 100, true,
            RoomSweepCandidateSurvey) &&
            room_sweep_candidate_handoff_frequency(
                &candidate, 101, 433920000, &used_candidate) == 314999997 && used_candidate,
        "fresh candidate handoff uses the actual tuned frequency");
    check(
        room_sweep_candidate_handoff_frequency(
            &candidate,
            100 + ROOM_SWEEP_CANDIDATE_EXPIRY_MS,
            433920000,
            &used_candidate) == 433920000 && !used_candidate,
        "expired candidate handoff cannot preserve its old frequency");

    check(
        !room_sweep_external_band_allows(0, 315000000),
        "Auto external band is not specific enough for TX");
    check(room_sweep_external_band_allows(1, 433920000), "400 external band accepts 433 MHz");
    check(!room_sweep_external_band_allows(1, 915000000), "400 external band rejects 915 MHz");
    check(room_sweep_external_band_allows(2, 915000000), "900 external band accepts 915 MHz");
    check(!room_sweep_external_band_allows(2, 433920000), "900 external band rejects 433 MHz");
    check(!room_sweep_external_band_allows(3, 433920000), "unknown external band is rejected");

    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputDisarmed, RoomSweepTxWorkerIdle, true, false) ==
                RoomSweepTxDecisionArm,
        "idle disarmed TX accepts short arm");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputDisarmed, RoomSweepTxWorkerRunning, true, false) ==
                RoomSweepTxDecisionNoop,
        "running worker blocks a new arm");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputDisarmed, RoomSweepTxWorkerIdle, false, false) ==
                RoomSweepTxDecisionNoop,
        "failed preflight blocks a new arm");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputArmed, RoomSweepTxWorkerIdle, true, true) == RoomSweepTxDecisionStart,
        "armed idle TX accepts long start");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputArmed, RoomSweepTxWorkerStopping, true, true) ==
                RoomSweepTxDecisionNoop,
        "stopping worker blocks long start");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputArmed, RoomSweepTxWorkerIdle, false, true) == RoomSweepTxDecisionNoop,
        "invalid frequency blocks long start");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputDisarmed, RoomSweepTxWorkerIdle, true, true) == RoomSweepTxDecisionNoop,
        "long OK cannot start from disarmed");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputStarting, RoomSweepTxWorkerRunning, true, true) ==
                RoomSweepTxDecisionNoop,
        "starting TX cannot be started again");
    check(
        room_sweep_tx_ok_decision(
            RoomSweepTxInputTransmitting, RoomSweepTxWorkerRunning, true, true) ==
                RoomSweepTxDecisionNoop,
        "transmitting TX cannot be started again");

    check(room_sweep_tx_refusal_text(RoomSweepTxRefusalNone)[0] == '\0', "no refusal has no error text");
    check(room_sweep_tx_refusal_text(RoomSweepTxRefusalNoRadio)[0] != '\0', "no-radio refusal is visible");
    check(
        room_sweep_tx_refusal_text(RoomSweepTxRefusalInvalidFrequency)[0] != '\0',
        "invalid-frequency refusal is visible");
    check(
        room_sweep_tx_refusal_text(RoomSweepTxRefusalExpiredCandidate)[0] != '\0',
        "expired-candidate refusal is visible");
    check(
        room_sweep_tx_refusal_text(RoomSweepTxRefusalExtBandUnknown)[0] != '\0',
        "external-band refusal is visible");
    check(room_sweep_tx_refusal_text(RoomSweepTxRefusalPolicy)[0] != '\0', "policy refusal is visible");
    check(
        room_sweep_tx_refusal_text(RoomSweepTxRefusalStartFailed)[0] != '\0',
        "start failure is visible");
    check(
        room_sweep_tx_refusal_text(RoomSweepTxRefusalCanceled)[0] != '\0',
        "canceled TX is visible");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

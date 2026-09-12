#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Wireless rows are identified by a real MAC whenever one is available.
 * SSID/name is a display label only — an ADVERTISED name self-reported by
 * the transmitting device; it proves proximity evidence, never owner,
 * product, or intent — and is used for matching only as a fallback when
 * neither side has a MAC.  Keep this seam independent from Flipper/Furi
 * headers for host tests and for the parser's bounded row update path.
 */
static inline bool room_sweep_wireless_text_present(const char* text) {
    return text != NULL && text[0] != '\0';
}

static inline char room_sweep_wireless_fold_mac_char(char value) {
    if(value >= 'a' && value <= 'f') return (char)(value - 'a' + 'A');
    return value;
}

static inline bool room_sweep_wireless_mac_equal(const char* left, const char* right) {
    if(!room_sweep_wireless_text_present(left) || !room_sweep_wireless_text_present(right)) {
        return false;
    }

    size_t index = 0u;
    while(left[index] != '\0' && right[index] != '\0') {
        if(room_sweep_wireless_fold_mac_char(left[index]) !=
           room_sweep_wireless_fold_mac_char(right[index])) {
            return false;
        }
        index++;
    }
    return left[index] == '\0' && right[index] == '\0';
}

static inline bool room_sweep_wireless_labels_equal(const char* left, const char* right) {
    if(!room_sweep_wireless_text_present(left) || !room_sweep_wireless_text_present(right)) {
        return false;
    }

    size_t index = 0u;
    while(left[index] != '\0' && right[index] != '\0') {
        if(left[index] != right[index]) return false;
        index++;
    }
    return left[index] == '\0' && right[index] == '\0';
}

/*
 * Match one stored row against one observation using MAC-first semantics:
 *
 *   - a MAC-bearing observation can only match the same MAC;
 *   - a MAC-less observation can use its label only when the stored row is
 *     also MAC-less;
 *   - a label never merges two different MAC addresses.
 */
static inline bool room_sweep_wireless_identity_matches(
    const char* stored_mac,
    const char* stored_label,
    const char* observation_mac,
    const char* observation_label) {
    bool stored_has_mac = room_sweep_wireless_text_present(stored_mac);
    bool observation_has_mac = room_sweep_wireless_text_present(observation_mac);

    if(observation_has_mac) {
        return stored_has_mac && room_sweep_wireless_mac_equal(stored_mac, observation_mac);
    }

    if(stored_has_mac || !room_sweep_wireless_text_present(stored_label) ||
       !room_sweep_wireless_text_present(observation_label)) {
        return false;
    }

    /* Labels are case-sensitive identifiers; MAC case is normalized above. */
    return room_sweep_wireless_labels_equal(stored_label, observation_label);
}

/* Evidence strings are deliberately short and novice-readable. */
typedef enum {
    RoomSweepWirelessEvidenceApBeacon = 0,
    RoomSweepWirelessEvidenceBleAdvertisement,
    RoomSweepWirelessEvidenceNotObservedInScan,
    RoomSweepWirelessEvidenceInternetTelemetry,
    RoomSweepWirelessEvidenceNoObservation,
} RoomSweepWirelessEvidence;

static inline const char* room_sweep_wireless_evidence_text(RoomSweepWirelessEvidence evidence) {
    switch(evidence) {
    case RoomSweepWirelessEvidenceApBeacon:
        return "AP beacon heard";
    case RoomSweepWirelessEvidenceBleAdvertisement:
        return "BLE advertisement heard";
    case RoomSweepWirelessEvidenceNotObservedInScan:
        return "Not observed in this scan";
    case RoomSweepWirelessEvidenceInternetTelemetry:
        return "Internet telemetry not measured";
    case RoomSweepWirelessEvidenceNoObservation:
        return "no observation does not prove absence";
    default:
        return "";
    }
}

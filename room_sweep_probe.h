#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "room_sweep_scan.h"     /* prompt stripping */
#include "room_sweep_marauder.h" /* int_after / copy_mac tokenizer helpers */
#include "room_sweep_wireless.h" /* identity match semantics */

/*
 * Phase 5 — client probe requests over Marauder `sniffprobe`, and the
 * hidden-SSID repair decision they enable (a client asking for a network
 * it remembers names that network in cleartext).
 *
 * FORMAT CONFIRMED 2026-09-21 (mntm-012 test rig, BFFB over USART).
 * The repository keeps only a redacted synthetic fixture derived from a
 * `sniffprobe` window; all six fixture records parse with this grammar and
 * nothing in the scan scaffolding false-positives:
 *
 *   RSSI: -60 Ch: 2 Client: 02:11:22:33:44:01 Requesting:
 *   RSSI: -91 Ch: 11 Client: 02:11:22:33:44:03 Requesting: SYNTH_CAMERA_NET
 *
 * i.e. fixture output is the upstream v1.9.1 shape (` RSSI: -NN Ch: N Client:
 * <mac> Requesting: <ssid>`), with the requested SSID last and possibly
 * empty. The parser also accepts the master shape (no leading "RSSI: ") and
 * an explicit "BSSID:" target when a build prints one, so it survives either
 * firmware; with today's live format the target field stays empty because no
 * BSSID is printed — which is why the repair below can only fire on a build
 * that prints one (documented, not a silent no-op).
 *
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 */

#define MAX_PROBE_DEVS 10

#define ROOM_SWEEP_PROBE_HIDDEN_LABEL "Hidden/unknown"

/* One parsed sniffprobe line. */
typedef struct {
    char ssid[33];
    char client_mac[18];
    char target_bssid[18]; /* empty on broadcast probes AND on all currently
                            * pinned formats (no BSSID printed upstream) */
    int8_t rssi;
    bool valid;
} RoomSweepProbeRecord;

/* Bounded table row: a client radio announcing networks it remembers. */
typedef struct {
    char ssid[33];
    char client_mac[18];
    char target_bssid[18];
    int8_t rssi;
    uint32_t first_seen;
    uint32_t last_seen;
    uint16_t observations;
    bool valid;
} ProbeDev;

/* SSID text after "Requesting: " to end of line, trailing spaces trimmed.
 * Empty (or master's "<hidden>") becomes the app's hidden label. */
static inline void room_sweep_probe_essid(const char* line, char* out, size_t out_sz) {
    out[0] = '\0';
    const char* p = strstr(line, "Requesting:");
    if(!p) return;
    p += strlen("Requesting:");
    while(*p == ' ') p++;
    const char* end = line + strlen(line);
    const char* trim = end;
    while(trim > p && *(trim - 1) == ' ') trim--;
    size_t i = 0;
    while(p < trim && i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if(out[0] == '\0' || strcmp(out, "<hidden>") == 0) {
        strncpy(out, ROOM_SWEEP_PROBE_HIDDEN_LABEL, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

/* True iff `line` is a sniffprobe record; fills *out. */
static inline bool room_sweep_probe_parse_line(const char* line, RoomSweepProbeRecord* out) {
    if(!out) return false;
    out->valid = false;
    if(!line) return false;
    line = room_sweep_uart_strip_prompt(line);
    if(line[0] == '\0' || line[0] == '#') return false;
    /* Both pinned shapes carry the Client/Requesting key pair; ESSID /
     * BSSID-only lines belong to the beacon and raw parsers. */
    if(!strstr(line, "Client:") || !strstr(line, "Requesting:")) return false;
    if(strstr(line, "ESSID") || strstr(line, "Device:")) return false;

    int rssi_val = 0;
    bool found_rssi = false;
    if(line[0] == '-' && line[1] >= '0' && line[1] <= '9') {
        char* end;
        long v = strtol(line, &end, 10);
        if(v >= -120 && v <= 0 && (*end == ' ' || *end == '\0')) {
            rssi_val = (int)v;
            found_rssi = true;
        }
    }
    if(!found_rssi && !room_sweep_marauder_int_after(line, "RSSI", &rssi_val)) {
        return false;
    }
    if(rssi_val > 0 || rssi_val < -120) return false;

    /* The client address is the only MAC in the pinned shapes; a target
     * BSSID is honored only via an explicit "BSSID:" key (never printed
     * upstream today — an SSID that merely looks like a MAC must never be
     * mistaken for one). */
    char client[18] = {0};
    if(!room_sweep_marauder_copy_mac(line, client)) return false;

    char target[18] = {0};
    const char* bkey = strstr(line, "BSSID:");
    if(bkey) {
        room_sweep_marauder_copy_mac(bkey, target);
    }

    char ssid[33] = {0};
    room_sweep_probe_essid(line, ssid, sizeof(ssid));

    out->rssi = (int8_t)rssi_val;
    strncpy(out->ssid, ssid, sizeof(out->ssid) - 1);
    out->ssid[sizeof(out->ssid) - 1] = '\0';
    strncpy(out->client_mac, client, sizeof(out->client_mac) - 1);
    out->client_mac[sizeof(out->client_mac) - 1] = '\0';
    strncpy(out->target_bssid, target, sizeof(out->target_bssid) - 1);
    out->target_bssid[sizeof(out->target_bssid) - 1] = '\0';
    out->valid = true;
    return true;
}

/*
 * Pure repair decision: true iff this probe record NAMES the hidden AP's
 * network — the record must carry a target/client-referenced BSSID equal
 * (case-insensitive) to the AP's BSSID, the AP row must still show the
 * hidden placeholder, and the probe must advertise a real name. Caller
 * performs the rename + history-preserving update; with the currently
 * pinned probe formats (no BSSID printed) this never fires — see the
 * format pin note above.
 */
static inline bool room_sweep_probe_names_hidden(
    const char* ap_bssid,
    const char* ap_ssid_label,
    const RoomSweepProbeRecord* p) {
    if(!p || !p->valid) return false;
    if(!room_sweep_wireless_text_present(ap_bssid)) return false;
    if(!room_sweep_wireless_text_present(p->target_bssid)) return false;
    if(!room_sweep_wireless_mac_equal(ap_bssid, p->target_bssid)) return false;
    if(!room_sweep_wireless_text_present(ap_ssid_label) ||
       strcmp(ap_ssid_label, ROOM_SWEEP_PROBE_HIDDEN_LABEL) != 0) {
        return false;
    }
    if(!room_sweep_wireless_text_present(p->ssid)) return false;
    if(strcmp(p->ssid, ROOM_SWEEP_PROBE_HIDDEN_LABEL) == 0) return false;
    return true;
}

/* MAC-first upsert over the bounded probe table (clients are keyed by
 * their own address; the advertised SSID is a label only). Returns the row
 * index, or -1 when full. `now` is the caller's tick. */
static inline int room_sweep_probe_upsert(
    ProbeDev* table,
    int max,
    const RoomSweepProbeRecord* obs,
    uint32_t now) {
    if(!table || max <= 0 || !obs || !obs->valid) return -1;
    for(int i = 0; i < max; i++) {
        if(table[i].valid &&
           room_sweep_wireless_identity_matches(
               table[i].client_mac, table[i].ssid, obs->client_mac, obs->ssid)) {
            table[i].rssi = obs->rssi;
            table[i].last_seen = now;
            if(obs->target_bssid[0]) {
                strncpy(table[i].target_bssid, obs->target_bssid, 17);
                table[i].target_bssid[17] = '\0';
            }
            if(table[i].observations < UINT16_MAX) table[i].observations++;
            return i;
        }
    }
    for(int i = 0; i < max; i++) {
        if(!table[i].valid) {
            memset(&table[i], 0, sizeof(table[i]));
            strncpy(table[i].ssid, obs->ssid, sizeof(table[i].ssid) - 1);
            strncpy(table[i].client_mac, obs->client_mac, 17);
            strncpy(table[i].target_bssid, obs->target_bssid, 17);
            table[i].rssi = obs->rssi;
            table[i].first_seen = now;
            table[i].last_seen = now;
            table[i].observations = 1;
            table[i].valid = true; /* publish the completed row last */
            return i;
        }
    }
    return -1;
}

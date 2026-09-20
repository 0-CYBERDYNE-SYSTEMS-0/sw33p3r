/* Host tests: curated OUI lookup (pure, no Furi). See
 * specs/full-capability-expansion-2026-09-20.md Phase 2 — truth contract:
 * unlisted MACs must return NULL (callers print "unlisted"), and randomized
 * (locally administered) addresses are detected, never guessed as vendors. */
#include <stdio.h>
#include <string.h>

#include "room_sweep_oui.h"

static int fails;

static void check(const char* name, bool cond) {
    if(cond) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        fails++;
    }
}

static void check_str(const char* name, const char* got, const char* want) {
    bool ok = got && want && strcmp(got, want) == 0;
    if(ok) {
        printf("PASS: %s (%s)\n", name, got);
    } else {
        printf("FAIL: %s got=[%s] want=[%s]\n", name, got ? got : "(null)", want ? want : "(null)");
        fails++;
    }
}

int main(void) {
    /* ---------------------------------------------------------- */
    /* Known lookups                                              */
    /* ---------------------------------------------------------- */
    check_str("oui: espressif lowercase",
              room_sweep_oui_lookup("24:0a:c4:12:34:56"),
              "Espressif");
    check_str("oui: espressif uppercase",
              room_sweep_oui_lookup("24:0A:C4:12:34:56"),
              "Espressif");
    check_str("oui: espressif mixed case",
              room_sweep_oui_lookup("24:0a:C4:56:AB:ef"),
              "Espressif");
    check_str("oui: raspberry pi foundation",
              room_sweep_oui_lookup("b8:27:eb:11:22:33"),
              "Raspberry");
    check_str("oui: synology", room_sweep_oui_lookup("00:11:32:aa:bb:cc"), "Synology");
    check_str("oui: apple", room_sweep_oui_lookup("F0:18:98:00:00:01"), "Apple");
    check_str("oui: hikvision", room_sweep_oui_lookup("44:19:b6:00:00:01"), "Hikvision");
    check_str("oui: dji", room_sweep_oui_lookup("60:60:1f:aa:bb:cc"), "DJI");
    check_str("oui: netgear", room_sweep_oui_lookup("9c:3d:cf:00:00:01"), "Netgear");

    /* Unlisted, well-formed, globally administered -> NULL (never guess). */
    check("oui: unlisted global mac returns NULL",
          room_sweep_oui_lookup("bc:24:11:22:33:44") == NULL);
    /* Locally administered but not in the table -> NULL from lookup too;
     * the "randomized" wording is evidence(), not lookup(). */
    check("oui: lookup on randomized mac returns NULL",
          room_sweep_oui_lookup("de:ad:be:ef:00:01") == NULL);

    /* ---------------------------------------------------------- */
    /* Randomized detection (first octet & 0x02)                  */
    /* ---------------------------------------------------------- */
    check("oui: 02 nibble randomized", room_sweep_oui_is_randomized("02:00:00:00:00:00"));
    check("oui: 06 nibble randomized", room_sweep_oui_is_randomized("06:11:22:33:44:55"));
    check("oui: 0a nibble randomized", room_sweep_oui_is_randomized("0a:11:22:33:44:55"));
    check("oui: 0e nibble randomized", room_sweep_oui_is_randomized("0e:11:22:33:44:55"));
    check("oui: 12 nibble randomized", room_sweep_oui_is_randomized("12:34:56:78:9a:bc"));
    check("oui: de nibble randomized", room_sweep_oui_is_randomized("DE:AD:BE:EF:00:01"));
    check("oui: ae nibble randomized", room_sweep_oui_is_randomized("ae:1b:2c:3d:4e:5f"));
    check("oui: 00 nibble not randomized", !room_sweep_oui_is_randomized("00:11:22:33:44:55"));
    check("oui: f0 nibble not randomized", !room_sweep_oui_is_randomized("f0:18:98:00:00:01"));
    check("oui: ac nibble not randomized", !room_sweep_oui_is_randomized("ac:22:0b:11:22:33"));
    check("oui: b8 nibble not randomized", !room_sweep_oui_is_randomized("B8:27:EB:11:22:33"));

    /* ---------------------------------------------------------- */
    /* Malformed / boundary input                                 */
    /* ---------------------------------------------------------- */
    check("oui: NULL mac lookup", room_sweep_oui_lookup(NULL) == NULL);
    check("oui: NULL mac randomized", !room_sweep_oui_is_randomized(NULL));
    check("oui: empty string", room_sweep_oui_lookup("") == NULL);
    check("oui: single octet", room_sweep_oui_lookup("24") == NULL);
    check("oui: two octets", room_sweep_oui_lookup("24:0a") == NULL);
    check("oui: three octets still too short", room_sweep_oui_lookup("24:0a:c4") == NULL);
    check("oui: truncated after 4 octets",
          room_sweep_oui_lookup("24:0a:c4:12:34") == NULL);
    check("oui: dash separators rejected",
          room_sweep_oui_lookup("24-0a-c4-12-34-56") == NULL);
    check("oui: no separators rejected",
          room_sweep_oui_lookup("240ac4123456") == NULL);
    check("oui: space separators rejected",
          room_sweep_oui_lookup("24 0a c4 12 34 56") == NULL);
    check("oui: non-hex character rejected",
          room_sweep_oui_lookup("24:0a:cg:12:34:56") == NULL);
    check("oui: randomized malformed string is false",
          !room_sweep_oui_is_randomized("24-0a-c4-12-34-56"));
    check("oui: randomized empty string is false", !room_sweep_oui_is_randomized(""));

    /* ---------------------------------------------------------- */
    /* Evidence token (UI/CSV single source of truth)             */
    /* ---------------------------------------------------------- */
    check_str("evidence: vendor label", room_sweep_oui_evidence("24:0a:c4:12:34:56"), "Espressif");
    check_str("evidence: unlisted", room_sweep_oui_evidence("bc:24:11:22:33:44"), "unlisted");
    check_str("evidence: randomized wins", room_sweep_oui_evidence("de:ad:be:ef:00:01"), "randomized");
    check("evidence: no MAC is NULL", room_sweep_oui_evidence(NULL) == NULL);
    check("evidence: malformed MAC is NULL", room_sweep_oui_evidence("junk") == NULL);

    /* ---------------------------------------------------------- */
    /* Table hygiene: curated means verified                      */
    /* ---------------------------------------------------------- */
    check("oui: table has entries", ROOM_SWEEP_OUI_COUNT >= 50);
    for(size_t i = 0; i < ROOM_SWEEP_OUI_COUNT; i++) {
        const RoomSweepOuiEntry* e = &room_sweep_oui_entries[i];
        char name[64];
        snprintf(name, sizeof(name), "entry %zu (%s) prefix well-formed", i, e->prefix);
        check(name, room_sweep_oui_prefix_well_formed(e->prefix));
        snprintf(name, sizeof(name), "entry %zu (%s) label <= 12 chars", i, e->prefix);
        check(name, strlen(e->label) <= 12);
        uint8_t octet = room_sweep_oui_first_octet(e->prefix);
        snprintf(name, sizeof(name), "entry %zu (%s) is unicast", i, e->prefix);
        check(name, (octet & 0x01) == 0);
        snprintf(name, sizeof(name), "entry %zu (%s) is globally administered", i, e->prefix);
        check(name, (octet & 0x02) == 0);
    }
    /* No duplicate prefixes (a dup would silently shadow). */
    bool dupes = false;
    for(size_t i = 0; i < ROOM_SWEEP_OUI_COUNT && !dupes; i++) {
        for(size_t j = i + 1; j < ROOM_SWEEP_OUI_COUNT; j++) {
            if(strcmp(room_sweep_oui_entries[i].prefix, room_sweep_oui_entries[j].prefix) == 0) {
                printf("FAIL: duplicate prefix %s\n", room_sweep_oui_entries[i].prefix);
                dupes = true;
                fails++;
                break;
            }
        }
    }
    if(!dupes) check("oui: no duplicate prefixes", true);

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

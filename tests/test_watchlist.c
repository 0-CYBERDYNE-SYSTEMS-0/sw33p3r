/* Host tests: Phase 9 opt-in cross-session watchlist (pure logic, no Furi).
 * See specs/full-capability-expansion-2026-09-20.md Phase 9 — the app's
 * ONLY persistent-identity feature. Parse must tolerate \r, spaces, blank
 * lines and # comments, default the label to "flagged", and refuse garbage
 * and overlong input whole. Matching is case-insensitive exact MAC compare
 * with empty-string safety. */
#include <stdio.h>
#include <string.h>

#include "../room_sweep_watchlist.h"

static int fails;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        fails++;
    }
}

static void check_str(const char* name, const char* got, const char* want) {
    int ok = got && want && strcmp(got, want) == 0;
    if(ok) {
        printf("PASS: %s (%s)\n", name, got);
    } else {
        printf("FAIL: %s got=[%s] want=[%s]\n", name, got ? got : "(null)", want ? want : "(null)");
        fails++;
    }
}

int main(void) {
    /* ---------------------------------------------------------- */
    /* parse: clean line, case, missing/empty label               */
    /* ---------------------------------------------------------- */
    RoomSweepWatchEntry e;
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff,front door", &e),
          "parse: clean line accepted");
    check_str("parse: mac kept verbatim", e.mac, "aa:bb:cc:dd:ee:ff");
    check_str("parse: label kept", e.label, "front door");

    check(room_sweep_watchlist_parse_line("AA:BB:CC:DD:EE:FF", &e) &&
              strcmp(e.label, "flagged") == 0,
          "parse: missing label defaults to \"flagged\"");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff,", &e) &&
              strcmp(e.label, "flagged") == 0,
          "parse: empty label defaults to \"flagged\"");
    check(room_sweep_watchlist_parse_line("12:34:56:78:9A:bc,Tracker", &e) &&
              strcmp(e.mac, "12:34:56:78:9A:bc") == 0 && strcmp(e.label, "Tracker") == 0,
          "parse: mixed-case hex and label survive");

    /* ---------------------------------------------------------- */
    /* parse: tolerance                                           */
    /* ---------------------------------------------------------- */
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff,garage\r", &e) &&
              strcmp(e.label, "garage") == 0,
          "parse: trailing CR tolerated");
    check(room_sweep_watchlist_parse_line("  aa:bb:cc:dd:ee:ff ,  side gate  ", &e) &&
              strcmp(e.mac, "aa:bb:cc:dd:ee:ff") == 0 && strcmp(e.label, "side gate") == 0,
          "parse: surrounding spaces trimmed from both fields");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff,desk,office", &e) &&
              strcmp(e.label, "desk,office") == 0,
          "parse: first comma splits; commas inside labels survive");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff,\r", &e) &&
              strcmp(e.label, "flagged") == 0,
          "parse: CR-only label falls back to default");
    check(room_sweep_watchlist_parse_line("# room_sweep watchlist", &e) == false,
          "parse: comment line skipped");
    check(room_sweep_watchlist_parse_line("   # comment\r", &e) == false,
          "parse: indented comment line skipped");
    check(room_sweep_watchlist_parse_line("", &e) == false,
          "parse: empty line skipped");
    check(room_sweep_watchlist_parse_line("   \r", &e) == false,
          "parse: whitespace-only line skipped");

    /* ---------------------------------------------------------- */
    /* parse: garbage and overlong input refused whole            */
    /* ---------------------------------------------------------- */
    check(room_sweep_watchlist_parse_line("hello world", &e) == false,
          "parse: plain garbage refused");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:f", &e) == false,
          "parse: short MAC refused");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff:aa", &e) == false,
          "parse: overlong MAC refused");
    check(room_sweep_watchlist_parse_line("aa-bb-cc-dd-ee-ff,x", &e) == false,
          "parse: dash-separated MAC refused");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:gz,x", &e) == false,
          "parse: non-hex MAC refused");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff, a label far beyond the "
                                          "23-char budget",
                                          &e) == false,
          "parse: overlong label refused whole (never truncated)");
    check(room_sweep_watchlist_parse_line(NULL, &e) == false, "parse: NULL line refused");
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:ff,x", NULL) == false,
          "parse: NULL out refused");

    /* ---------------------------------------------------------- */
    /* match: case-insensitive exact compare + empty safety       */
    /* ---------------------------------------------------------- */
    RoomSweepWatchEntry m;
    check(room_sweep_watchlist_parse_line("Aa:Bb:Cc:Dd:Ee:Ff,cam", &m), "match: entry parsed");
    check(room_sweep_watchlist_match(&m, "aa:bb:cc:dd:ee:ff"), "match: lower hits upper entry");
    check(room_sweep_watchlist_match(&m, "AA:BB:CC:DD:EE:FF"), "match: upper hits mixed entry");
    check(!room_sweep_watchlist_match(&m, "aa:bb:cc:dd:ee:f0"),
          "match: different address never matches");
    check(!room_sweep_watchlist_match(&m, "aa:bb:cc:dd:ee"),
          "match: truncated address never matches");
    check(!room_sweep_watchlist_match(&m, ""), "match: empty observation MAC never matches");
    check(!room_sweep_watchlist_match(NULL, "aa:bb:cc:dd:ee:ff"), "match: NULL entry safe");
    check(!room_sweep_watchlist_match(&m, NULL), "match: NULL mac safe");
    RoomSweepWatchEntry empty_mac = {{0}, "x"};
    check(!room_sweep_watchlist_match(&empty_mac, "aa:bb:cc:dd:ee:ff"),
          "match: empty entry MAC never matches");

    /* ---------------------------------------------------------- */
    /* copy_label: 23-char truncation, control fold, default      */
    /* ---------------------------------------------------------- */
    char label[24];
    room_sweep_watchlist_copy_label(label, sizeof(label), "Front Door AP");
    check_str("copy_label: normal name kept", label, "Front Door AP");
    room_sweep_watchlist_copy_label(label, sizeof(label), "1234567890123456789012345678");
    check(strlen(label) == 23, "copy_label: truncates to 23 chars");
    check_str("copy_label: truncated prefix", label, "12345678901234567890123");
    room_sweep_watchlist_copy_label(label, sizeof(label), "a\nb\rc\td");
    check_str("copy_label: control chars folded to _", label, "a_b_c_d");
    room_sweep_watchlist_copy_label(label, sizeof(label), "net,name");
    check_str("copy_label: commas kept (round-trip)", label, "net,name");
    label[0] = 'X';
    room_sweep_watchlist_copy_label(label, sizeof(label), "");
    check_str("copy_label: empty source defaults", label, "flagged");
    label[0] = 'X';
    room_sweep_watchlist_copy_label(label, sizeof(label), "   ");
    check_str("copy_label: spaces-only defaults", label, "flagged");
    room_sweep_watchlist_copy_label(label, sizeof(label), NULL);
    check_str("copy_label: NULL source defaults", label, "flagged");

    /* ---------------------------------------------------------- */
    /* list: add/find/reset/full + loader buffer                  */
    /* ---------------------------------------------------------- */
    RoomSweepWatchlist list;
    room_sweep_watchlist_reset(&list);
    check(list.count == 0 && !list.full_seen, "list: reset yields empty list");
    check(room_sweep_watchlist_find(&list, "aa:bb:cc:dd:ee:ff") == -1,
          "list: empty list never matches");

    RoomSweepWatchEntry a, b;
    check(room_sweep_watchlist_parse_line("aa:bb:cc:dd:ee:01,one", &a) &&
          room_sweep_watchlist_parse_line("AA:BB:CC:DD:EE:02,two", &b),
          "list: fixtures parsed");
    check(room_sweep_watchlist_add(&list, &a), "list: first add accepted");
    check(room_sweep_watchlist_add(&list, &b), "list: second add accepted");
    check(list.count == 2, "list: count tracks adds");
    check(room_sweep_watchlist_find(&list, "aa:bb:cc:dd:ee:01") == 0,
          "list: find by lowercase mac");
    check(room_sweep_watchlist_find(&list, "AA:BB:CC:DD:EE:02") == 1,
          "list: find by uppercase mac");
    check(room_sweep_watchlist_find(&list, "aa:bb:cc:dd:ee:03") == -1,
          "list: unknown mac not found");
    check(!room_sweep_watchlist_add(&list, &a), "list: duplicate add refused");
    check(list.count == 2, "list: duplicate add does not grow the list");

    /* Fill to the 16-entry ceiling; the 17th valid add is refused and latched. */
    room_sweep_watchlist_reset(&list);
    RoomSweepWatchEntry filler;
    int added = 0;
    for(uint8_t i = 0; i < ROOM_SWEEP_WATCH_MAX + 4; i++) {
        snprintf(filler.mac, sizeof(filler.mac), "00:00:00:00:00:%02x", i);
        strcpy(filler.label, "filler");
        if(room_sweep_watchlist_add(&list, &filler)) added++;
    }
    check(added == ROOM_SWEEP_WATCH_MAX, "list: fills to exactly 16 entries");
    check(list.count == ROOM_SWEEP_WATCH_MAX && list.full_seen,
          "list: overflow refuses and sets full_seen");

    /* Loader: clean two-line file, comments, blanks, CRLF, case, dup. */
    room_sweep_watchlist_reset(&list);
    const char* file = "# watchlist\n"
                       "aa:bb:cc:dd:ee:01,one\n"
                       "\n"
                       "AA:BB:CC:DD:EE:02,two\r\n"
                       "aa:bb:cc:dd:ee:01,dup\r"
                       "no colon line\n";
    room_sweep_watchlist_load_buffer(&list, file, strlen(file));
    check(list.count == 2, "loader: comments/blanks/dups skipped, 2 entries kept");
    check(strcmp(list.entries[0].label, "one") == 0 &&
              strcmp(list.entries[1].label, "two") == 0,
          "loader: labels survive CR handling");
    check(room_sweep_watchlist_find(&list, "aa:bb:cc:dd:ee:02") == 1,
          "loader: second entry matchable case-insensitively");

    /* Loader: missing trailing newline still parses the last line. */
    room_sweep_watchlist_reset(&list);
    const char* no_nl = "aa:bb:cc:dd:ee:09,back";
    room_sweep_watchlist_load_buffer(&list, no_nl, strlen(no_nl));
    check(list.count == 1 && strcmp(list.entries[0].label, "back") == 0,
          "loader: final line without newline parses");

    /* Loader: overlong lines are skipped whole, never truncated into validity. */
    room_sweep_watchlist_reset(&list);
    /* 18 + 40 = 58 chars: past ROOM_SWEEP_WATCHLIST_LINE_MAX, skipped whole. */
    const char* overlong = "aa:bb:cc:dd:ee:ff,aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                           "bb:bb:cc:dd:ee:ff,ok\n";
    room_sweep_watchlist_load_buffer(&list, overlong, strlen(overlong));
    check(list.count == 1 && strcmp(list.entries[0].mac, "bb:bb:cc:dd:ee:ff") == 0,
          "loader: overlong line skipped, following line still parsed");

    /* Loader: file with more valid lines than RAM fits sets full_seen. */
    room_sweep_watchlist_reset(&list);
    char big[(ROOM_SWEEP_WATCH_MAX + 3) * 24];
    size_t off = 0;
    for(uint8_t i = 0; i < ROOM_SWEEP_WATCH_MAX + 3; i++) {
        off += (size_t)snprintf(
            big + off, sizeof(big) - off, "00:00:00:00:01:%02x,f%02u\n", i, (unsigned)i);
    }
    room_sweep_watchlist_load_buffer(&list, big, off);
    check(list.count == ROOM_SWEEP_WATCH_MAX, "loader: RAM list caps at 16");
    check(list.full_seen, "loader: file larger than RAM marks full_seen");

    /* Loader: degenerate inputs stay safe. */
    room_sweep_watchlist_reset(&list);
    room_sweep_watchlist_load_buffer(&list, "", 0);
    check(list.count == 0, "loader: empty buffer yields empty list");
    room_sweep_watchlist_load_buffer(NULL, file, strlen(file));
    room_sweep_watchlist_load_buffer(&list, NULL, 4);
    check(list.count == 0, "loader: NULL inputs yield empty list");

    /* Hit count popcount: distinct-match reporting base. */
    check(room_sweep_watchlist_hit_count(0) == 0, "hit_count: empty mask counts 0");
    check(room_sweep_watchlist_hit_count((uint16_t)(1U << 0 | 1U << 5 | 1U << 15)) == 3,
          "hit_count: scattered bits count 3");
    check(room_sweep_watchlist_hit_count(UINT16_MAX) == ROOM_SWEEP_WATCH_MAX,
          "hit_count: full mask caps at 16");

    if(fails) {
        printf("RESULT: %d failure(s)\n", fails);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

#include <stdio.h>
#include <string.h>

#include "../room_sweep_record_state.h"

static int failures;

static void check(bool condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

static void test_filenames(void) {
    char name[ROOM_SWEEP_RECORD_NAME_MAX];
    check(
        room_sweep_record_filename(RoomSweepRecordFileSession, 7, name, sizeof(name)) &&
            strcmp(name, "session-7.csv") == 0,
        "session ordinal filename");
    check(
        room_sweep_record_filename(RoomSweepRecordFileUart, 12, name, sizeof(name)) &&
            strcmp(name, "uart-12.txt") == 0,
        "UART ordinal filename");
    check(
        room_sweep_record_filename(RoomSweepRecordFileReport, 12, name, sizeof(name)) &&
            strcmp(name, "report-12.txt") == 0,
        "report ordinal filename");
    check(
        !room_sweep_record_filename(RoomSweepRecordFileCount, 1, name, sizeof(name)),
        "unknown filename kind rejected");
    check(
        !room_sweep_record_filename(RoomSweepRecordFileSession, 1, name, 8),
        "truncated filename rejected");
}

static void test_identifier_redaction(void) {
    RoomSweepRecordIdentifierMap map;
    room_sweep_record_identifier_map_init(&map);
    char first[16];
    char again[16];
    char other[16];
    char separate_kind[16];

    check(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdAccessPoint, "Secret Network", first, sizeof(first)) &&
            strcmp(first, "AP-01") == 0 && strstr(first, "Secret") == NULL,
        "first AP is redacted to AP-01");
    check(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdAccessPoint, "Secret Network", again, sizeof(again)) &&
            strcmp(again, first) == 0,
        "same AP keeps stable ordinal");
    check(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdAccessPoint, "Another Network", other, sizeof(other)) &&
            strcmp(other, "AP-02") == 0 && strstr(other, "Another") == NULL,
        "different AP gets next ordinal");
    check(
        room_sweep_record_identifier_ref(
            &map, RoomSweepRecordIdBle, "Secret Network", separate_kind, sizeof(separate_kind)) &&
            strcmp(separate_kind, "BLE-01") == 0,
        "identifier ordinals are scoped by source kind");
    check(
        room_sweep_record_identifier_ref(&map, RoomSweepRecordIdBle, NULL, other, sizeof(other)) &&
            strcmp(other, "-") == 0,
        "missing identifier is represented without raw data");
}

static void test_bounded_lifecycle(void) {
    RoomSweepRecordState state;
    room_sweep_record_state_init(&state, 2, 10);
    check(room_sweep_record_state_begin(&state), "record state opens");
    check(!room_sweep_record_state_begin(&state), "second begin is rejected");
    check(
        room_sweep_record_state_can_append(&state, 4) && state.records_written == 0,
        "capacity preflight does not claim a durable record");
    check(room_sweep_record_state_append(&state, 4), "first record fits");
    check(room_sweep_record_state_append(&state, 6), "second record reaches byte cap");
    check(!room_sweep_record_state_append(&state, 1), "record cap drops later record");
    check(state.records_written == 2 && state.bytes_written == 10 && state.dropped_records == 1,
          "record and byte bounds are tracked");
    check(room_sweep_record_state_finish(&state, true), "clean end is accepted");
    check(room_sweep_record_state_is_complete(&state), "clean end is complete");
    check(!room_sweep_record_state_append(&state, 1), "closed state cannot append");
}

static void test_storage_failure(void) {
    RoomSweepRecordState state;
    room_sweep_record_state_init(&state, 4, 32);
    check(room_sweep_record_state_begin(&state), "failure state opens");
    room_sweep_record_state_note_storage_failure(&state);
    check(state.storage_failed && state.storage_failures == 1, "storage failure is latched");
    check(!room_sweep_record_state_append(&state, 1), "latched storage failure blocks writes");
    check(!room_sweep_record_state_finish(&state, true), "failed storage cannot claim clean end");
    check(state.end_status == RoomSweepRecordEndIncomplete, "failed storage is incomplete");
}

int main(void) {
    test_filenames();
    test_identifier_redaction();
    test_bounded_lifecycle();
    test_storage_failure();
    printf("RESULT: %s (%d failure%s)\n", failures ? "FAIL" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

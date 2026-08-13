#include <stdio.h>

#include "../room_sweep_ui_layout.h"

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
    /* Constant ordering sanity: the bands tile the 128x64 panel top to bottom. */
    check(
        0 < UI_TAB_STRIP_H && UI_TAB_STRIP_H < UI_ROW_HEADER_BASELINE &&
            UI_ROW_HEADER_BASELINE < UI_ROW_BODY_FIRST_BASELINE &&
            UI_ROW_BODY_FIRST_BASELINE < UI_FOOTER_BAND_TOP &&
            UI_FOOTER_BAND_TOP < UI_ROW_FOOTER_BASELINE &&
            UI_ROW_FOOTER_BASELINE < UI_H,
        "bands are ordered 0 < tab strip < header < body < footer < UI_H");
    check(UI_W == 128, "UI_W is 128");
    check(
        UI_MARGIN_X < UI_TEXT_RIGHT_EDGE && UI_TEXT_RIGHT_EDGE < UI_W,
        "text margins sit inside the panel");
    check(
        UI_ROW_BODY_FIRST_BASELINE + 4 * UI_ROW_BODY_PITCH <=
            UI_ROW_FOOTER_BASELINE,
        "five body rows (22..62 at pitch 10) fit above the footer baseline");

    /* Rows between: bottom baseline is inclusive. */
    check(
        room_sweep_ui_rows_between(22, 63, 10) == 5,
        "body band 22..63 at pitch 10 fits 5 rows (22,32,42,52,62)");
    check(
        room_sweep_ui_rows_between(22, 62, 10) == 5,
        "bottom baseline 62 is included in the count");
    check(
        room_sweep_ui_rows_between(22, 63, 8) == 6,
        "body band 22..63 at pitch 8 fits 6 rows");
    check(room_sweep_ui_rows_between(22, 63, 0) == 0, "zero pitch returns 0 rows");
    check(room_sweep_ui_rows_between(63, 22, 10) == 0, "inverted band returns 0 rows");

    /* Text fits: right edge is start_x + count*px_per_char <= 126. */
    check(
        room_sweep_ui_text_fits(UI_MARGIN_X, 24, UI_FONT_KEYBOARD_PX),
        "24 keyboard chars from x=2 fit (right edge 122 <= 126)");
    check(
        !room_sweep_ui_text_fits(UI_MARGIN_X, 25, UI_FONT_KEYBOARD_PX),
        "25 keyboard chars from x=2 overflow (right edge 127 > 126)");
    check(
        room_sweep_ui_text_fits(UI_MARGIN_X, 0, UI_FONT_KEYBOARD_PX),
        "empty text fits");
    check(
        room_sweep_ui_text_fits(130, 0, UI_FONT_KEYBOARD_PX),
        "empty text fits even with start_x past the margin (nothing to draw)");

    /* Right align. */
    check(
        room_sweep_ui_right_align_x(UI_TEXT_RIGHT_EDGE, 30) == 96,
        "30px text right-aligned at edge 126 starts at 96");
    check(
        room_sweep_ui_right_align_x(UI_TEXT_RIGHT_EDGE, 130) == 0,
        "text wider than the edge flushes to 0");

    /* Center. */
    check(room_sweep_ui_center_x(2, 124, 60) == 34, "60px text centers in box(2,124) at 34");
    check(room_sweep_ui_center_x(0, 128, 128) == 0, "full-width text in full-width box starts at 0");
    check(room_sweep_ui_center_x(0, 128, 200) == 0, "text wider than the box flushes to box start");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

#include <stdio.h>
#include <string.h>

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
    check(
        strlen(UI_HINT_RF_WATCH) <= UI_HINT_MAX_CHARS,
        "RF watch footer hint fits the 20-char line budget");

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

    /* Wi capture source copy (Phase 4/5/10): the static asserts in the
     * header already fail the compile on overflow; these pin the pixel
     * arithmetic for the new strings. */
    check(
        room_sweep_ui_text_fits(UI_MARGIN_X, (uint8_t)strlen(UI_HINT_WI_RAW_IDLE), UI_FONT_KEYBOARD_PX),
        "raw idle copy fits the keyboard budget");
    check(
        room_sweep_ui_text_fits(UI_MARGIN_X, (uint8_t)strlen(UI_HINT_WI_PROBE_IDLE), UI_FONT_KEYBOARD_PX),
        "probe idle copy fits the keyboard budget");
    check(
        room_sweep_ui_text_fits(UI_MARGIN_X, (uint8_t)strlen(UI_HINT_WI_TOOL_IDLE), UI_FONT_KEYBOARD_PX),
        "tool idle copy fits the keyboard budget");
    check(
        room_sweep_ui_text_fits(UI_MARGIN_X, (uint8_t)strlen(UI_HINT_WI_TOOL_TRUTH), UI_FONT_KEYBOARD_PX),
        "tool truth copy fits the keyboard budget");

    /* Center. */
    check(room_sweep_ui_center_x(2, 124, 60) == 34, "60px text centers in box(2,124) at 34");
    check(room_sweep_ui_center_x(0, 128, 128) == 0, "full-width text in full-width box starts at 0");
    check(room_sweep_ui_center_x(0, 128, 200) == 0, "text wider than the box flushes to box start");

    /* Wireless list rows: RSSI column, name clip, right-aligned hint tag. */
    check(UI_ROW_RSSI_X == 1 && UI_ROW_NAME_X == 28, "list rows: RSSI at x1, name from x28");
    check(
        room_sweep_ui_hint_x(4) == 106,
        "4-char hint tag right-aligns at x106 (126 - 4*5)");
    check(
        room_sweep_ui_hint_x(3) == 111,
        "3-char hint tag right-aligns at x111 (126 - 3*5)");
    check(
        room_sweep_ui_name_clip_px(4) == 73,
        "name clips to 73px when a 4-char tag is shown (28..101, 1 glyph gap)");
    check(
        room_sweep_ui_name_clip_px(3) == 78,
        "name clips to 78px when a 3-char tag is shown (28..106, 1 glyph gap)");
    check(
        room_sweep_ui_name_clip_px(0) == 0,
        "degenerate tag length reports no clip width (use UI_ROW_NAME_MAX_PX)");
    check(
        UI_ROW_NAME_X + room_sweep_ui_name_clip_px(4) + UI_FONT_KEYBOARD_PX <=
            room_sweep_ui_hint_x(4),
        "clipped name plus one glyph gap never reaches a 4-char tag");
    check(
        UI_ROW_NAME_X + room_sweep_ui_name_clip_px(3) + UI_FONT_KEYBOARD_PX <=
            room_sweep_ui_hint_x(3),
        "clipped name plus one glyph gap never reaches a 3-char tag");
    check(
        room_sweep_ui_text_fits(
            room_sweep_ui_hint_x(UI_ROW_HINT_MAX_CHARS), UI_ROW_HINT_MAX_CHARS, UI_FONT_KEYBOARD_PX),
        "widest tag fits between its x and the text edge");
    check(
        UI_ROW_NAME_MAX_PX <= room_sweep_ui_name_clip_px(UI_ROW_HINT_MAX_CHARS) ||
            UI_ROW_NAME_X + UI_ROW_NAME_MAX_PX <= UI_TEXT_RIGHT_EDGE,
        "legacy no-tag clip width still respects the text edge");

    /* Phase 6 "!" duplicate-SSID marker column: with no tag it takes the
     * tag column itself; with a tag it sits exactly one glyph left so the
     * two never share pixels. The clipped name never reaches the mark. */
    check(
        room_sweep_ui_mark_x(0) == room_sweep_ui_hint_x(1),
        "mark with no tag uses the tag column");
    check(
        room_sweep_ui_mark_x(4) == room_sweep_ui_hint_x(4) - UI_FONT_KEYBOARD_PX,
        "mark with a 4-char tag sits one glyph left of it (x101)");
    check(
        room_sweep_ui_mark_x(3) == room_sweep_ui_hint_x(3) - UI_FONT_KEYBOARD_PX,
        "mark with a 3-char tag sits one glyph left of it (x106)");
    check(
        room_sweep_ui_text_fits(room_sweep_ui_mark_x(0), 1, UI_FONT_KEYBOARD_PX),
        "mark glyph fits between its x and the text edge (no tag)");
    check(
        room_sweep_ui_text_fits(room_sweep_ui_mark_x(4), 1, UI_FONT_KEYBOARD_PX),
        "mark glyph before a 4-char tag still fits");
    check(
        UI_ROW_NAME_X + room_sweep_ui_name_clip_px_marked(4) + UI_FONT_KEYBOARD_PX <=
            room_sweep_ui_mark_x(4),
        "clipped name plus gap never reaches the mark (4-char tag)");
    check(
        UI_ROW_NAME_X + room_sweep_ui_name_clip_px_marked(0) + UI_FONT_KEYBOARD_PX <=
            room_sweep_ui_mark_x(0),
        "clipped name plus gap never reaches the mark (no tag)");
    check(
        room_sweep_ui_name_clip_px_marked(4) ==
            room_sweep_ui_name_clip_px(4) - UI_FONT_KEYBOARD_PX,
        "the mark costs exactly one glyph of name width (4-char tag)");
    check(
        UI_ROW_NAME_X + room_sweep_ui_name_clip_px_marked(0) <= room_sweep_ui_mark_x(0),
        "no-tag marked clip stays inside the mark column");

    /* Phase 9 "WATCH" badge column: 5 glyphs hugging the text edge; a hint
     * tag moves one glyph left of it and the "!" mark one glyph left of
     * that. The clipped name never reaches the leftmost strip element for
     * every combination of mark x hint-tag x badge. */
    check(
        room_sweep_ui_watch_x() == 101,
        "WATCH badge right-aligns at x101 (126 - 5*5)");
    check(
        room_sweep_ui_text_fits(room_sweep_ui_watch_x(), UI_ROW_WATCH_CHARS, UI_FONT_KEYBOARD_PX),
        "WATCH badge fits between its x and the text edge");
    check(
        room_sweep_ui_hint_x_watched(4) == 76,
        "4-char tag on a WATCH row sits one glyph left of the badge (x76)");
    check(
        room_sweep_ui_hint_x_watched(3) == 81,
        "3-char tag on a WATCH row sits one glyph left of the badge (x81)");
    check(
        room_sweep_ui_mark_x_watched(0) == room_sweep_ui_watch_x() - UI_FONT_KEYBOARD_PX,
        "mark on a WATCH row with no tag sits one glyph left of the badge (x96)");
    check(
        room_sweep_ui_mark_x_watched(4) == room_sweep_ui_hint_x_watched(4) - UI_FONT_KEYBOARD_PX,
        "mark on a WATCH row with a 4-char tag sits one glyph left of it (x71)");
    for(uint8_t tag_chars = 0; tag_chars <= UI_ROW_HINT_MAX_CHARS; tag_chars++) {
        uint8_t watch_x = room_sweep_ui_watch_x();
        /* mark + tag + badge chain, no mark (BLE rows). */
        uint8_t clip_plain = room_sweep_ui_name_clip_px_watch(tag_chars);
        uint8_t left_plain = tag_chars ? room_sweep_ui_hint_x_watched(tag_chars) : watch_x;
        check(
            UI_ROW_NAME_X + clip_plain + UI_FONT_KEYBOARD_PX <= left_plain,
            "WATCH-row name clip clears the tag strip (no mark)");
        check(
            room_sweep_ui_text_fits(left_plain, tag_chars, UI_FONT_KEYBOARD_PX),
            "hint tag on a WATCH row fits before the badge");
        if(tag_chars > 0) {
            check(
                (uint8_t)(left_plain + tag_chars * UI_FONT_KEYBOARD_PX) +
                        UI_FONT_KEYBOARD_PX <=
                    room_sweep_ui_watch_x(),
                "tag strip leaves a glyph gap before the WATCH badge");
        }
        /* mark + tag + badge chain (Wi BEACON rows). */
        uint8_t clip_marked = room_sweep_ui_name_clip_px_watch_marked(tag_chars);
        uint8_t mark_x = room_sweep_ui_mark_x_watched(tag_chars);
        check(
            UI_ROW_NAME_X + clip_marked + UI_FONT_KEYBOARD_PX <= mark_x,
            "WATCH-row marked name clip clears the mark");
        check(
            room_sweep_ui_text_fits(mark_x, 1, UI_FONT_KEYBOARD_PX),
            "mark glyph on a WATCH row still fits");
        check(
            clip_marked == clip_plain - UI_FONT_KEYBOARD_PX,
            "the mark costs exactly one glyph of WATCH-row name width");
    }
    check(
        strlen(UI_HINT_DETAIL_WATCH) <= UI_HINT_MAX_CHARS &&
            strlen(UI_HINT_WATCH_ON) <= UI_HINT_MAX_CHARS &&
            strlen(UI_HINT_WATCH_FULL) <= UI_HINT_MAX_CHARS,
        "watchlist detail copy fits the 20-char line budget");

    /* RF survey strip: every preset gets a bar, and the strip must fit the
     * panel at both the historical 16 and the expanded 20 presets. */
    for(uint8_t channels = 16; channels <= 20; channels = (uint8_t)(channels + 4)) {
        uint8_t stride = room_sweep_ui_rf_bar_stride(channels);
        uint8_t width = room_sweep_ui_rf_bar_width(channels);
        check(stride > 0, "bar stride is non-zero for a 16-20 preset strip");
        check(width < stride, "bar leaves a gap pixel before the next bar");
        check(
            room_sweep_ui_rf_bar_x((uint8_t)(channels - 1), channels) + width <= UI_W,
            "last preset bar stays inside the panel");
        check(
            room_sweep_ui_rf_bar_stride(16) == 8,
            "16 presets keep the historical 8px stride");
        uint8_t step = room_sweep_ui_rf_label_step(channels);
        check(step > 0, "label step is non-zero");
        check(
            room_sweep_ui_rf_label_count(channels) <= 5,
            "at most 5 labels on the strip (5px/glyph, 4-char labels)");
        uint8_t last = (uint8_t)((channels - 1) / step * step);
        check(
            room_sweep_ui_text_fits(
                room_sweep_ui_rf_bar_x(last, channels), 4, UI_FONT_KEYBOARD_PX),
            "last strip label fits inside the text margin");
    }
    check(
        room_sweep_ui_rf_label_step(16) == 4 && room_sweep_ui_rf_label_step(20) == 4,
        "label step is 4 at both 16 and 20 presets");

    if(failures) {
        printf("RESULT: %d failure(s)\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}

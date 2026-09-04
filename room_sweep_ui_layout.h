#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Room Sweep — 128x64 panel layout constants and text-placement helpers.
 * Header-only and Flipper-header-free so host tests compile with plain cc.
 * Integer math only: the project builds with -Wdouble-promotion as an error.
 */

/* Full panel size: the Flipper display is 128x64. */
#define UI_W 128
#define UI_H 64

/* Tab strip covers y0-5 (5px glyphs + 1px separator). */
#define UI_TAB_STRIP_H 6

/* Header baseline: first text line below the tab strip (glyphs y9-14). */
#define UI_ROW_HEADER_BASELINE 14

/* Baseline of the first body row below the header. */
#define UI_ROW_BODY_FIRST_BASELINE 22

/* Vertical pitch between consecutive body-row baselines. */
#define UI_ROW_BODY_PITCH 10

/* Baseline of the single footer text row. */
#define UI_ROW_FOOTER_BASELINE 63

/* Top of the footer band spanning y57-64. */
#define UI_FOOTER_BAND_TOP 57

/* Left text margin in px. */
#define UI_MARGIN_X 2

/* Rightmost x any text may reach (2px margin from the 128px edge). */
#define UI_TEXT_RIGHT_EDGE 126

/* FontKeyboard glyph width in px (smallest font, ~25 chars/line). */
#define UI_FONT_KEYBOARD_PX 5

/* FontSecondary glyph width in px (body text). */
#define UI_FONT_SECONDARY_PX 6

/*
 * Number of row baselines in [top_baseline, bottom_baseline] at the given
 * pitch (bottom baseline inclusive). Rejects a zero pitch or an inverted
 * band, both of which would otherwise loop or count forever.
 */
static inline uint8_t room_sweep_ui_rows_between(
    uint8_t top_baseline,
    uint8_t bottom_baseline,
    uint8_t pitch) {
    if(pitch == 0 || top_baseline > bottom_baseline) return 0;
    return (uint8_t)((uint32_t)(bottom_baseline - top_baseline) / pitch + 1U);
}

/*
 * True iff char_count chars at px_per_char fit between start_x and the right
 * margin. Empty text always fits: with nothing to draw, a start beyond the
 * margin is harmless (documented choice). The width multiply runs in unsigned
 * 32-bit so start_x + count*px cannot overflow a promoted int.
 */
static inline bool room_sweep_ui_text_fits(
    uint8_t start_x,
    uint8_t char_count,
    uint8_t px_per_char) {
    if(char_count == 0) return true;
    return (uint32_t)start_x + (uint32_t)char_count * px_per_char <=
           UI_TEXT_RIGHT_EDGE;
}

/* Start x right-aligning text_px against right_edge; 0 if text is wider. */
static inline uint8_t room_sweep_ui_right_align_x(uint8_t right_edge, uint8_t text_px) {
    if(text_px > right_edge) return 0;
    return (uint8_t)(right_edge - text_px);
}

/*
 * Start x centering text_px inside [box_x, box_x + box_w], clamped so it never
 * exceeds box_x + box_w. Text at least as wide as the box flushes to box_x.
 */
static inline uint8_t room_sweep_ui_center_x(uint8_t box_x, uint8_t box_w, uint8_t text_px) {
    uint32_t box_right = (uint32_t)box_x + box_w;
    if(text_px >= box_w) return box_x;
    uint32_t x = (uint32_t)box_x + (uint32_t)(box_w - text_px) / 2U;
    if(x > box_right) x = box_right;
    return (uint8_t)x;
}

/*
 * Footer/hint copy budget. FontKeyboard averages ~6px/char, so a hint drawn
 * at UI_MARGIN_X must stay <= UI_HINT_MAX_CHARS to clear the 128px screen
 * (anything longer clips at the right edge — QA 2026-09-04). The static
 * asserts make an over-long hint a compile error, not a device surprise.
 *
 * Same-x FontKeyboard rows also need >= 8px between baselines; draw at most
 * one text row per baseline band (body rows use UI_ROW_BODY_PITCH = 10).
 */
#define UI_HINT_MAX_CHARS 20

#define UI_HINT_RF_SWEEP "U/D OK L=AN R=band"
#define UI_HINT_RF_PEAK "U/D mode OK refine"
#define UI_HINT_AN_LIST "U/D=sel HoldL=exit"
#define UI_HINT_AN_FIELD "L=list R=radar"
#define UI_HINT_AN_RADAR "L=list R=meter"
#define UI_HINT_NR_STATUS "OK=scan L=AN R=res"
#define UI_HINT_NR_RESULTS "OK=rescan L=AN R=res"
#define UI_HINT_TX_INT_ONLY "300MHz: INT only"
#define UI_HINT_TX_INT_REFUSAL "300MHz needs INT"
#define UI_HINT_TX_OTHER "U/D for 400/900"
#define UI_HINT_TX_SPI_400 "Set SPI switch 400"
#define UI_HINT_TX_SPI_900 "Set SPI switch 900"
#define UI_HINT_TX_ARMED "U/D=freq HoldOK=TX"
#define UI_HINT_TX_DISARM "B=disarm"
#define UI_HINT_TX_CARRIER "Carrier, no replay"
#define UI_HINT_BT_ADV "advert  HoldOK=lock"

#define UI_ASSERT_HINT(s) \
    _Static_assert( \
        sizeof(s) - 1 <= UI_HINT_MAX_CHARS, \
        "hint copy exceeds the 20-char line budget and clips at 128px")

UI_ASSERT_HINT(UI_HINT_RF_SWEEP);
UI_ASSERT_HINT(UI_HINT_RF_PEAK);
UI_ASSERT_HINT(UI_HINT_AN_LIST);
UI_ASSERT_HINT(UI_HINT_AN_FIELD);
UI_ASSERT_HINT(UI_HINT_AN_RADAR);
UI_ASSERT_HINT(UI_HINT_NR_STATUS);
UI_ASSERT_HINT(UI_HINT_NR_RESULTS);
UI_ASSERT_HINT(UI_HINT_TX_INT_ONLY);
UI_ASSERT_HINT(UI_HINT_TX_INT_REFUSAL);
UI_ASSERT_HINT(UI_HINT_TX_OTHER);
UI_ASSERT_HINT(UI_HINT_TX_SPI_400);
UI_ASSERT_HINT(UI_HINT_TX_SPI_900);
UI_ASSERT_HINT(UI_HINT_TX_ARMED);
UI_ASSERT_HINT(UI_HINT_TX_DISARM);
UI_ASSERT_HINT(UI_HINT_TX_CARRIER);
UI_ASSERT_HINT(UI_HINT_BT_ADV);

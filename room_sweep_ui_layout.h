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
 * Wireless list rows (Wi/BT LIST page). The "%4d " RSSI text occupies the
 * left of the line starting at x=1; the advertised name clips from
 * UI_ROW_NAME_X; an identification hint tag (3-4 chars, e.g. "CAM?") is
 * right-aligned at the text edge with one glyph of gap so a clipped name
 * and the tag never collide.
 */
#define UI_ROW_RSSI_X 1
#define UI_ROW_NAME_X 28
#define UI_ROW_NAME_MAX_PX 96
#define UI_ROW_HINT_MAX_CHARS 4

/*
 * X that right-aligns a tag_chars-long FontKeyboard hint tag against the
 * text edge (4 chars -> 106, 3 chars -> 111).
 */
static inline uint8_t room_sweep_ui_hint_x(uint8_t tag_chars) {
    return room_sweep_ui_right_align_x(
        UI_TEXT_RIGHT_EDGE,
        (uint8_t)((uint32_t)tag_chars * UI_FONT_KEYBOARD_PX));
}

/*
 * Name clip width in px for a row carrying a tag_chars-long hint tag:
 * [UI_ROW_NAME_X .. tag_x - one glyph gap]. 0 when there is no room
 * (tag_chars == 0 callers should use UI_ROW_NAME_MAX_PX instead).
 */
static inline uint8_t room_sweep_ui_name_clip_px(uint8_t tag_chars) {
    if(tag_chars == 0) return 0; /* degenerate: caller uses UI_ROW_NAME_MAX_PX */
    uint8_t tag_x = room_sweep_ui_hint_x(tag_chars);
    if(tag_x <= (uint8_t)(UI_ROW_NAME_X + UI_FONT_KEYBOARD_PX)) return 0;
    return (uint8_t)(tag_x - UI_FONT_KEYBOARD_PX - UI_ROW_NAME_X);
}

/*
 * Duplicate-SSID "!" marker column (Wi list, Phase 6). One FontKeyboard
 * glyph, ASCII-safe. With no hint tag it sits on the tag column itself;
 * with a tag it moves one glyph to the tag's left so the two never share
 * pixels (the "!" goes immediately left of the tag column).
 */
static inline uint8_t room_sweep_ui_mark_x(uint8_t tag_chars) {
    if(tag_chars == 0) return room_sweep_ui_hint_x(1);
    return (uint8_t)(room_sweep_ui_hint_x(tag_chars) - UI_FONT_KEYBOARD_PX);
}

/*
 * Name clip width for a row drawing BOTH a tag_chars-long hint tag and the
 * "!" marker (tag_chars may be 0 = tag absent). The mark always costs one
 * glyph plus the existing one-glyph gap.
 */
static inline uint8_t room_sweep_ui_name_clip_px_marked(uint8_t tag_chars) {
    uint8_t mark_x = room_sweep_ui_mark_x(tag_chars);
    if(mark_x <= (uint8_t)(UI_ROW_NAME_X + UI_FONT_KEYBOARD_PX)) return 0;
    return (uint8_t)(mark_x - UI_FONT_KEYBOARD_PX - UI_ROW_NAME_X);
}

/*
 * Phase 9 watchlist "WATCH" badge: the widest list-row tag (5 FontKeyboard
 * glyphs) hugging the right text edge. When it shares a row with a hint
 * tag, the tag moves one glyph left of the badge; the Phase 6 "!" mark
 * moves one glyph left of that (or of the badge when no hint tag). All
 * widths come from the same chain the mark/tag pair already uses, so the
 * clipped name can never reach the tag strip.
 */
#define UI_ROW_WATCH_CHARS 5

/* X of the WATCH badge's first glyph (right-aligned at the text edge). */
static inline uint8_t room_sweep_ui_watch_x(void) {
    return room_sweep_ui_right_align_x(
        UI_TEXT_RIGHT_EDGE,
        (uint8_t)((uint32_t)UI_ROW_WATCH_CHARS * UI_FONT_KEYBOARD_PX));
}

/*
 * X of a tag_chars-long hint tag when a WATCH badge shares the row (call
 * only with tag_chars > 0): the tag ends one glyph left of the badge.
 */
static inline uint8_t room_sweep_ui_hint_x_watched(uint8_t tag_chars) {
    uint8_t watch_x = room_sweep_ui_watch_x();
    uint32_t tag_px = (uint32_t)tag_chars * UI_FONT_KEYBOARD_PX;
    if((uint32_t)watch_x < tag_px + UI_FONT_KEYBOARD_PX) return 0;
    return (uint8_t)(watch_x - tag_px - UI_FONT_KEYBOARD_PX);
}

/* "!" mark x on a row that also carries the WATCH badge (tag may be 0). */
static inline uint8_t room_sweep_ui_mark_x_watched(uint8_t tag_chars) {
    uint8_t tag_x = tag_chars ? room_sweep_ui_hint_x_watched(tag_chars) :
                                room_sweep_ui_watch_x();
    if(tag_x < UI_FONT_KEYBOARD_PX) return 0;
    return (uint8_t)(tag_x - UI_FONT_KEYBOARD_PX);
}

/* Name clip on a WATCH row without a "!" mark (BLE rows: no mark). */
static inline uint8_t room_sweep_ui_name_clip_px_watch(uint8_t tag_chars) {
    uint8_t left_x = tag_chars ? room_sweep_ui_hint_x_watched(tag_chars) :
                                 room_sweep_ui_watch_x();
    if(left_x <= (uint8_t)(UI_ROW_NAME_X + UI_FONT_KEYBOARD_PX)) return 0;
    return (uint8_t)(left_x - UI_FONT_KEYBOARD_PX - UI_ROW_NAME_X);
}

/* Name clip on a WATCH row that also draws the "!" mark (Wi BEACON rows). */
static inline uint8_t room_sweep_ui_name_clip_px_watch_marked(uint8_t tag_chars) {
    uint8_t mark_x = room_sweep_ui_mark_x_watched(tag_chars);
    if(mark_x <= (uint8_t)(UI_ROW_NAME_X + UI_FONT_KEYBOARD_PX)) return 0;
    return (uint8_t)(mark_x - UI_FONT_KEYBOARD_PX - UI_ROW_NAME_X);
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
#define UI_HINT_BT_ADV "adv OK=lock HOK=scan"
#define UI_HINT_RF_WATCH "U/D:mode H:card"
/* Wi capture sources (Phase 4/5/10): the "listening" copy under an empty
 * table and the TOOL truth line (one line of honest copy — like attack
 * tooling, never proof of intent). */
#define UI_HINT_WI_RAW_IDLE "every transmitter"
#define UI_HINT_WI_PROBE_IDLE "probe requests"
#define UI_HINT_WI_TOOL_IDLE "esp/pwn adverts"
#define UI_HINT_WI_TOOL_TRUTH "not proof of intent"
/* Phase 9 watchlist: the flag affordance on Wi/BT detail pages while the
 * opt-in watchlist is ON, plus the on-watch and list-full lines. */
#define UI_HINT_DETAIL_WATCH "OK=lock HoldUp=watch"
#define UI_HINT_WATCH_ON "ON WATCHLIST"
#define UI_HINT_WATCH_FULL "WATCH FULL"

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
UI_ASSERT_HINT(UI_HINT_RF_WATCH);
UI_ASSERT_HINT(UI_HINT_WI_RAW_IDLE);
UI_ASSERT_HINT(UI_HINT_WI_PROBE_IDLE);
UI_ASSERT_HINT(UI_HINT_WI_TOOL_IDLE);
UI_ASSERT_HINT(UI_HINT_WI_TOOL_TRUTH);
UI_ASSERT_HINT(UI_HINT_DETAIL_WATCH);
UI_ASSERT_HINT(UI_HINT_WATCH_ON);
UI_ASSERT_HINT(UI_HINT_WATCH_FULL);

/*
 * RF survey bar strip. Every preset needs a bar on the 128px panel, so the
 * stride is derived from the preset count rather than hand-tuned: 16 presets
 * keep the historical 8px stride (bars flush to both edges), 20 presets get
 * 6px (5px bar + 1px gap). A bar is one pixel narrower than its stride so
 * neighbouring bars never share an edge pixel.
 */
static inline uint8_t room_sweep_ui_rf_bar_stride(uint8_t channels) {
    if(channels == 0) return 0;
    return (uint8_t)(UI_W / channels);
}

static inline uint8_t room_sweep_ui_rf_bar_width(uint8_t channels) {
    uint8_t stride = room_sweep_ui_rf_bar_stride(channels);
    return stride > 0 ? (uint8_t)(stride - 1U) : 0;
}

static inline uint8_t room_sweep_ui_rf_bar_x(uint8_t channel, uint8_t channels) {
    return (uint8_t)((uint32_t)channel * room_sweep_ui_rf_bar_stride(channels));
}

/*
 * Label stride on the strip: every nth preset gets a 3-4 char label under its
 * bar. Five labels is the budget (5 chars/glyph, 4-char labels), so the step
 * is ceil(channels / 5) — 4 for both 16 and 20 presets. Every fifth label
 * position (0, step, 2*step, ...) must still land inside the text margin;
 * tests/test_ui_layout.c pins that for the preset count in use.
 */
static inline uint8_t room_sweep_ui_rf_label_step(uint8_t channels) {
    if(channels == 0) return 1;
    return (uint8_t)((channels + 4U) / 5U);
}

/* Label count drawn on the strip (indices 0, step, 2*step, ...). */
static inline uint8_t room_sweep_ui_rf_label_count(uint8_t channels) {
    uint8_t step = room_sweep_ui_rf_label_step(channels);
    if(step == 0) return 0;
    return (uint8_t)((channels + step - 1U) / step);
}

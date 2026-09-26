#include <stdio.h>

#include "../room_sweep_rf_presets.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

/* CC1101 real bands (Hz): the radio cannot tune outside them, and the gaps
 * between them are hardware, not policy. */
static int in_cc1101_band(uint32_t hz) {
    return (hz >= 300000000UL && hz <= 348000000UL) || /* low */
           (hz >= 387000000UL && hz <= 464000000UL) || /* mid */
           (hz >= 779000000UL && hz <= 928000000UL);   /* high */
}

/* Band column expected for a frequency, mirroring the dual-CC1101 filter. */
static uint8_t expected_band(uint32_t hz) {
    if(hz <= 348000000UL) return 0;
    if(hz <= 464000000UL) return 1;
    return 2;
}

static int label_len(const char* s) {
    int n = 0;
    while(s[n]) n++;
    return n;
}

int main(void) {
    check(RF_NUM_CHANNELS == 20, "20 presets after the phase-11 expansion");

    /* The three parallel tables share one preset index; a short table read
     * means tuning an arbitrary frequency, so assert the dimensions. */
    check(
        sizeof(rf_channels) / sizeof(rf_channels[0]) == RF_NUM_CHANNELS,
        "rf_channels has one entry per preset");
    check(
        sizeof(rf_labels) / sizeof(rf_labels[0]) == RF_NUM_CHANNELS,
        "rf_labels has one entry per preset");
    check(
        sizeof(rf_channel_band) / sizeof(rf_channel_band[0]) == RF_NUM_CHANNELS,
        "rf_channel_band has one entry per preset");

    int all_tunable = 1, bands_match = 1, no_empty_label = 1, no_wide_label = 1;
    for(int i = 0; i < RF_NUM_CHANNELS; i++) {
        uint32_t hz = rf_channels[i];
        if(!in_cc1101_band(hz)) all_tunable = 0;
        if(expected_band(hz) != rf_channel_band[i]) bands_match = 0;

        int len = label_len(rf_labels[i]);
        if(len == 0) no_empty_label = 0;
        if(len > 4) no_wide_label = 0;
    }
    check(all_tunable, "every preset sits inside a CC1101 band");
    check(bands_match, "band column matches the frequency's CC1101 band");
    check(no_empty_label, "every preset has a label");
    check(no_wide_label, "no label exceeds the 4-char strip budget");

    /* Duplicate presets would waste a bar and double-count a baseline row. */
    int dup = 0;
    for(int i = 0; i < RF_NUM_CHANNELS && !dup; i++) {
        for(int j = i + 1; j < RF_NUM_CHANNELS; j++) {
            if(rf_channels[i] == rf_channels[j]) {
                dup = 1;
                break;
            }
        }
    }
    check(!dup, "no duplicate preset frequencies");

    /* The four phase-11 additions, plus the US ISM centre that was kept. */
    int have_850 = 0, have_880 = 0, have_902 = 0, have_927 = 0, have_915 = 0;
    for(int i = 0; i < RF_NUM_CHANNELS; i++) {
        if(rf_channels[i] == 850000000UL) have_850 = 1;
        if(rf_channels[i] == 880000000UL) have_880 = 1;
        if(rf_channels[i] == 902000000UL) have_902 = 1;
        if(rf_channels[i] == 927500000UL) have_927 = 1;
        if(rf_channels[i] == 915000000UL) have_915 = 1;
    }
    check(have_850, "850 MHz cellular-uplink-edge preset present");
    check(have_880, "880 MHz cellular-uplink-edge preset present");
    check(have_902, "902 MHz US ISM band-edge preset present");
    check(have_927, "927.5 MHz US ISM top preset present");
    check(have_915, "915 MHz US ISM preset kept from the 16-preset table");

    /* Survey strip budget: the bar stride comes from the panel width and the
     * label step from the preset count; the last label must clear the text
     * margin (UI_TEXT_RIGHT_EDGE = 126, 4 chars at 5 px). */
    check(RF_LABEL_STEP > 0, "label step is non-zero");
    check(
        (RF_NUM_CHANNELS + RF_LABEL_STEP - 1) / RF_LABEL_STEP <= 5,
        "at most 5 labels drawn on the strip");
    int last_labeled = ((RF_NUM_CHANNELS - 1) / RF_LABEL_STEP) * RF_LABEL_STEP;
    int stride = 128 / RF_NUM_CHANNELS;
    check(
        last_labeled * stride + 4 * 5 <= 126, "last strip label clears the right text margin");
    check(
        (RF_NUM_CHANNELS - 1) * stride + (stride - 1) <= 128,
        "last bar stays inside the panel");

    if(failures) {
        printf("\nRESULT: %d FAILURE(S)\n", failures);
    } else {
        printf("\nRESULT: ALL PASS (0 failures)\n");
    }
    return failures ? 1 : 0;
}

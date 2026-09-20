/* Host tests: name-pattern class hints (pure, no Furi). See
 * specs/full-capability-expansion-2026-09-20.md Phase 3 — hints are
 * name-pattern guesses, the "?" is part of the contract, and every
 * false-positive guard below is load-bearing. */
#include <stdio.h>
#include <string.h>

#include "room_sweep_classify.h"

static int fails;

static void check(const char* name, bool cond) {
    if(cond) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        fails++;
    }
}

static void check_hint(const char* name, RoomSweepClassHints got, RoomSweepClassHints want) {
    if(got == want) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s got=0x%02x want=0x%02x\n", name, (unsigned)got, (unsigned)want);
        fails++;
    }
}

int main(void) {
    /* ---------------------------------------------------------- */
    /* Camera positives                                           */
    /* ---------------------------------------------------------- */
    check_hint("ssid: IPCAM device default", room_sweep_classify_ssid("IPCAM-2A4E"), ClassHintCamera);
    check_hint("ssid: plain Camera", room_sweep_classify_ssid("Front Door Camera"), ClassHintCamera);
    check_hint("ssid: cam with dash", room_sweep_classify_ssid("Cam-WEST"), ClassHintCamera);
    check_hint("ssid: cctv", room_sweep_classify_ssid("CCTV-Room"), ClassHintCamera);
    check_hint("ssid: dvr", room_sweep_classify_ssid("DVR-LivingRoom"), ClassHintCamera);
    check_hint("ssid: nvr", room_sweep_classify_ssid("NVR shop"), ClassHintCamera);
    check_hint("ssid: hichip", room_sweep_classify_ssid("hichip_1234"), ClassHintCamera);
    check_hint("ssid: onvif", room_sweep_classify_ssid("ONVIF-Server"), ClassHintCamera);
    check_hint("ssid: arlo", room_sweep_classify_ssid("Arlo-Base"), ClassHintCamera);
    check_hint("ble: arlo name", room_sweep_classify_ble_name("Arlo Camera"), ClassHintCamera);
    check_hint("ssid: esp32-cam is camera AND devboard",
               room_sweep_classify_ssid("ESP32-cam"),
               ClassHintCamera | ClassHintDevboard);

    /* ---------------------------------------------------------- */
    /* Camera negatives (hard false-positive contract)            */
    /* ---------------------------------------------------------- */
    check_hint("ssid: cameron never camera", room_sweep_classify_ssid("Cameron"), ClassHintNone);
    check_hint("ssid: cameron wifi suffix", room_sweep_classify_ssid("cameron-wifi"), ClassHintNone);
    check_hint("ssid: macaroni neutral", room_sweep_classify_ssid("macaroni"), ClassHintNone);
    check_hint("ssid: cameraman not camera", room_sweep_classify_ssid("Cameraman"), ClassHintNone);
    check_hint("ssid: camouflaged not camera", room_sweep_classify_ssid("camouflage"), ClassHintNone);
    check_hint("ssid: phpmyadmin not printer", room_sweep_classify_ssid("phpmyadmin"), ClassHintNone);

    /* ---------------------------------------------------------- */
    /* Printer positives + negatives                              */
    /* ---------------------------------------------------------- */
    check_hint("ssid: DIRECT-xx-HP", room_sweep_classify_ssid("DIRECT-47-HP"), ClassHintPrinter);
    check_hint("ssid: HP-Print-LaserJet", room_sweep_classify_ssid("HP-Print-2C-LaserJet"), ClassHintPrinter);
    check_hint("ble: brother printer", room_sweep_classify_ble_name("Brother HL-L2350DW"), ClassHintPrinter);
    check_hint("ssid: epson", room_sweep_classify_ssid("EPSON-Printer"), ClassHintPrinter);
    check_hint("ssid: hp standalone", room_sweep_classify_ssid("HP"), ClassHintPrinter);
    check_hint("ssid: HPOffice glue blocked (case-insensitive right guard)",
               room_sweep_classify_ssid("HPOffice"),
               ClassHintNone);
    check_hint("ssid: HP dash form still matches", room_sweep_classify_ssid("HP-Officejet"), ClassHintPrinter);
    check_hint("ssid: glued *brother suffix matches (no left guard on long words)",
               room_sweep_classify_ble_name("Photobrother"),
               ClassHintPrinter);
    check_hint("ssid: happenstance not printer", room_sweep_classify_ssid("happenstance"), ClassHintNone);

    /* ---------------------------------------------------------- */
    /* Hotspot                                                    */
    /* ---------------------------------------------------------- */
    check_hint("ssid: AndroidAP default", room_sweep_classify_ssid("AndroidAP ab12"), ClassHintPhoneHotspot);
    check_hint("ble: iphone", room_sweep_classify_ble_name("iPhone"), ClassHintPhoneHotspot);
    check_hint("ssid: mifi", room_sweep_classify_ssid("MiFi 2200"), ClassHintPhoneHotspot);
    check_hint("ssid: MyHotspot camel", room_sweep_classify_ssid("MyHotspot"), ClassHintPhoneHotspot);
    check_hint("ssid: glued hotspot suffix matches (no left guard on long words)",
               room_sweep_classify_ssid("sunshinehotspot"),
               ClassHintPhoneHotspot);

    /* ---------------------------------------------------------- */
    /* IoT                                                        */
    /* ---------------------------------------------------------- */
    check_hint("ssid: TuyaSmart camel", room_sweep_classify_ssid("TuyaSmart-ABC"), ClassHintIot);
    check_hint("ssid: SmartPlug", room_sweep_classify_ssid("SmartPlug"), ClassHintIot);
    check_hint("ssid: smart standalone", room_sweep_classify_ssid("smart plug"), ClassHintIot);
    check_hint("ssid: bulb", room_sweep_classify_ssid("Bulb-1"), ClassHintIot);
    check_hint("ssid: switch2 digit right", room_sweep_classify_ssid("Switch2"), ClassHintIot);
    check_hint("ssid: sensor", room_sweep_classify_ssid("temp-sensor"), ClassHintIot);
    check_hint("ssid: Smarties not iot", room_sweep_classify_ssid("Smarties"), ClassHintNone);
    check_hint("ssid: smarter not iot", room_sweep_classify_ssid("smarter-home"), ClassHintNone);

    /* ---------------------------------------------------------- */
    /* Drone                                                      */
    /* ---------------------------------------------------------- */
    check_hint("ssid: dji", room_sweep_classify_ssid("DJI-Mavic-RC"), ClassHintDrone);
    check_hint("ble: mavic", room_sweep_classify_ble_name("Mavic 3"), ClassHintDrone);
    check_hint("ssid: fpv", room_sweep_classify_ssid("FPV450"), ClassHintDrone);
    check_hint("ssid: drone", room_sweep_classify_ssid("Drone-FC"), ClassHintDrone);
    check_hint("ssid: adjim not dji", room_sweep_classify_ssid("adjim"), ClassHintNone);

    /* ---------------------------------------------------------- */
    /* Devboard                                                   */
    /* ---------------------------------------------------------- */
    check_hint("ssid: ESP_ default ssid", room_sweep_classify_ssid("ESP_0B3C6E"), ClassHintDevboard);
    check_hint("ssid: esp32", room_sweep_classify_ssid("ESP32-Test"), ClassHintDevboard);
    check_hint("ssid: esp8266", room_sweep_classify_ssid("esp8266-demo"), ClassHintDevboard);
    check_hint("ssid: esp- separator", room_sweep_classify_ssid("esp-marauder"), ClassHintDevboard);
    check_hint("ble: marauder", room_sweep_classify_ble_name("Marauder"), ClassHintDevboard);
    check_hint("ble: flipper", room_sweep_classify_ble_name("Flipper Zero"), ClassHintDevboard);
    check_hint("ssid: devboard", room_sweep_classify_ssid("my devboard"), ClassHintDevboard);
    check_hint("ssid: vesp- left guard", room_sweep_classify_ssid("vesp-things"), ClassHintNone);

    /* ---------------------------------------------------------- */
    /* Tracker (BLE only)                                         */
    /* ---------------------------------------------------------- */
    check_hint("ble: tile", room_sweep_classify_ble_name("Tile"), ClassHintTrackerBle);
    check_hint("ble: smarttag", room_sweep_classify_ble_name("Samsung SmartTag"), ClassHintTrackerBle);
    check_hint("ble: trackr", room_sweep_classify_ble_name("TrackR bravo"), ClassHintTrackerBle);
    check_hint("ble: airtag", room_sweep_classify_ble_name("My AirTag"), ClassHintTrackerBle);
    check_hint("ble: duo tag", room_sweep_classify_ble_name("duo tag"), ClassHintTrackerBle);
    check_hint("ble: tiles plural neutral", room_sweep_classify_ble_name("Bathroom tiles"), ClassHintNone);
    check_hint("ssid: tracker names never hint on ssid",
               room_sweep_classify_ssid("AirTag finder"),
               ClassHintNone);

    /* ---------------------------------------------------------- */
    /* Placeholder neutrality                                     */
    /* ---------------------------------------------------------- */
    check_hint("ssid: Hidden/unknown placeholder neutral",
               room_sweep_classify_ssid("Hidden/unknown"),
               ClassHintNone);
    check_hint("ble: Hidden/unknown placeholder neutral",
               room_sweep_classify_ble_name("Hidden/unknown"),
               ClassHintNone);
    check_hint("ssid: empty", room_sweep_classify_ssid(""), ClassHintNone);
    check_hint("ble: empty", room_sweep_classify_ble_name(""), ClassHintNone);
    check("ssid: NULL safe", room_sweep_classify_ssid(NULL) == ClassHintNone);
    check("ble: NULL safe", room_sweep_classify_ble_name(NULL) == ClassHintNone);
    check("ssid: hidden contains no pattern",
          room_sweep_classify_ssid("my Hidden/unknown ssid") == ClassHintNone);

    /* ---------------------------------------------------------- */
    /* hint_text contract: "?" is part of the API                 */
    /* ---------------------------------------------------------- */
    check("hint: none is empty string", room_sweep_classify_hint_text(ClassHintNone)[0] == '\0');
    check("hint: none via zero", room_sweep_classify_hint_text(0)[0] == '\0');
    {
        static const struct {
            RoomSweepClassHints h;
            const char* text;
        } cases[] = {
            {ClassHintCamera, "CAM?"},
            {ClassHintPrinter, "PRT?"},
            {ClassHintPhoneHotspot, "HS?"},
            {ClassHintIot, "IOT?"},
            {ClassHintDrone, "DRN?"},
            {ClassHintDevboard, "DEV?"},
            {ClassHintTrackerBle, "TRK?"},
        };
        for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char name[48];
            snprintf(name, sizeof(name), "hint: %s form", cases[i].text);
            const char* got = room_sweep_classify_hint_text(cases[i].h);
            bool ok = got && strcmp(got, cases[i].text) == 0;
            check(name, ok);
        }
        /* First (lowest) hint wins the fixed text. */
        const char* mixed = room_sweep_classify_hint_text(
            ClassHintDevboard | ClassHintCamera);
        check("hint: first bit wins for multi-hint",
              mixed && strcmp(mixed, "CAM?") == 0);
        /* All tags stay within the 4-char UI column budget. */
        bool fits = true;
        for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if(strlen(cases[i].text) > 4) fits = false;
        }
        check("hint: all tags <= 4 chars", fits);
    }

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

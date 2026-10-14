# Spec: RX5808 5.8 GHz FPV energy finder

## Goal
Detect analog FPV video downlinks (5.8 GHz) — the classic surveillance-drone
tell that no Wi-Fi/BLE/RID path sees — using an RX5808-class 5.8 GHz video
receiver module on the Flipper GPIO, swept channel-by-channel with an
energy/peak-hold presentation like the RF tab.

## Hardware / firmware facts (2026-10-11 web research)
- RX5808: 5.8 GHz analog video RX, 48 channels (standard FPV band/channel
  table), sensitivity ≈ -90 dBm, SPI-style 3-wire channel select + an
  ANALOG RSSI output (~0–3.3 V). ~70 mA @ 3.3–5.5 V (OTG power may be
  required, same caveat as nRF24 on the AIO).
- We read ENERGY and CHANNEL only. No video decoding, no audio — ever.
  Detection claim language: "energy on 5.8 GHz CH x", never "camera".
- Same-family modules (RC540R, RX5808 Pro) expose the same RSSI/SPI contract.

## Open validation gates (resolve BEFORE implementation)
1. **ADC availability**: the RSSI pin needs analog read. Verify
   `furi_hal_adc_*` (or a usable comparator path) exists in the API 87.1
   export table (`python3 _verify_api.py` after wiring). If no exported ADC
   path exists on the chosen pin, fall back to a digital threshold
   (comparator/ladder) presentation — coarse detect/no-detect — and say so
   in the UI. Do not fake a dBm scale in either case; label RAW/ACT units.
2. **Pin plan**: must avoid PA4 (AIO nRF24 CS), PB2 (AIO nRF24 CE), USART
   13/14 (BFFB), LPUART 15/16 (GPS), and the bottom-switch SPI mux. Pin
   choice is a bench-verified receipt, not a guess (same standard as the
   PB2/PC3 diagnosis on 2026-10-09).
3. **SPI/mux policy**: extend `room_sweep_radio_path.h` decisions so RX5808,
   nRF24 and CC1101 never contend for shared lines; one RF path active at a
   time (existing `radio_mutex` discipline).

## Contract
1. New RF sub-mode or AV-tab page **5G8** (decide at implementation against
   the 128×64 budget; RF tab is full at 5 sub-modes). State in
   `room_sweep_fpv.h` (Flipper-free): channel sweep loop, peak hold, per-
   channel max, staleness. Host tests (`tests/test_fpv.c`) pin the FPV
   band/channel table and peak-hold math — integer only.
2. Sweep cadence bounded (channel-settle delay honored per module datasheet);
   Back/stop always releases SPI + GPIO (mirror `nrf24_survey.c` teardown,
   including failed-start deinit).
3. Session events: `source=FPV`, channel in observation detail,
   `rssi` column honest (raw ADC units or 0 with ACT hits — same rule as
   nRF24: counts can never be read as dBm).
4. Room Report line: "5.8 GHz FPV energy: n channels with activity".

## Verification
- Host: band table, peak hold, teardown-state tests in `init.sh`.
- `_verify_api.py` clean (ADC gate is pass/fail here), ufbt zero warnings.
- Device: bench test against a known FPV VTX at low power on the operator's
  own hardware (receive-only), receipt in `.omo/evidence/`.

## Out of scope
Video demodulation/display, audio, direction-finding claims (RSSI ≠ bearing
unless triangulated by the dashboard from multiple field positions — future),
any TX at 5.8 GHz.

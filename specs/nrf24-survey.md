# Spec: nRF24 detect-only survey

## Goal
When BFFB bottom switch selects nRF24 on Flipper SPI, Room Sweep can survey
2.4 GHz proprietary channel activity via nRF24 RPD (receive power detector).

## Coexistence
- Sub-GHz uses **internal** CC1101 while SPI path preference is nRF24.
- External dual CC1101 is unavailable on that SPI path (hardware mux).
- ESP32 Marauder UART (Wi-Fi/BLE/GPS CLI) is unaffected.

## Safety
- RX / RPD channel scan only.
- No jam, mousejack, flood, or continuous carrier modes.
- Session events: `source=NRF24`, observation/scan_start/scan_end.

## UI
New tab `nR` (`SweepModeNrf24`): start/stop, channel activity summary, top channels.

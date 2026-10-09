# Spec: nRF24 detect-only survey

## Goal
When BFFB bottom switch selects nRF24 on Flipper SPI, Room Sweep can survey
2.4 GHz channel ENERGY via the nRF24L01+ RPD (receive power detector).

## What the hardware actually gives us (truth contract, 2026-09-11)
The survey reads only the RPD bit (register 0x09 bit 0). Per the Nordic
nRF24L01+ product spec, RPD is set when received power in the CURRENT RF
channel exceeds about -64 dBm — a 1-bit energy snapshot that fires on any
emitter (WiFi, BLE, another nRF24, microwave leakage). It carries no packet,
address, protocol, or device information, and produces no RSSI number.

## Packet identification is NOT practical here (decision)
Decoding an nRF24 packet requires knowing its 5-byte (40-bit) address in
advance, and passively discovering unknown addresses requires attack-style
techniques (mousejack-class address recovery / active sniffing) that
MISSION.md and PROMPT.md explicitly ban. Given the hardware (1-bit detector,
no address), the API/memory budget, and the detect-only mission contract,
packet-based nRF24 identification is ruled out. The nR mode therefore reports
channel ENERGY/hit counts only, labelled "2.4 GHz energy detection" in UI and
docs; every numeric nRF24 readout uses ACTIVITY units (ACT), never dBm.

## Coexistence
- Operator's 3-in-1 AIO board: CS = PA4 / pin 4, CE = PB2 / pin 6.
  Receive-only device diagnostic on 2026-10-09: STATUS 0x0e,
  CONFIG write/read 0x0b, PB2 produced 23/1008 RPD hits; PC3 produced
  0/1008. PC3 chip-select returned 0x00 and is not this board's SPI path.
- Initialize the external SPI handle before probing, and release its GPIO
  configuration on completion or failed start. Host driver regression:
  `tests/test_nrf24_driver.c` (cold CS, retry, sampling, power ownership).
- Sub-GHz uses **internal** CC1101 while SPI path preference is nRF24.
- External dual CC1101 is unavailable on that SPI path (hardware mux).
- ESP32 Marauder UART (Wi-Fi/BLE/GPS CLI) is unaffected.

## Safety
- RX / RPD channel scan only.
- No jam, mousejack, flood, or continuous carrier modes.
- Session events: `source=NRF24`, observation/scan_start/scan_end. The
  observation's CSV `rssi` column stays 0 (nothing measured); hit totals
  travel in dedicated fields so counts can never be read as dBm.

## UI
Tab `nR` (`SweepModeNrf24`): titles "2.4G ENERGY" / "2.4G RESULTS";
start/stop, channel energy summary, top hit channels. Analyzer pages over nR
label the value ACT.

# Spec: BLE / WiFi scan timeout & parser

## Problem
BLE tab shows `ERR` immediately (or almost immediately) after starting sniff, with no devices listed.

## Root cause (source-verified)
In the main loop timeout check:

```c
if(scanning && (last_data_tick == 0 || now - last_data_tick >= TIMEOUT))
```

Starting a scan clears results and sets `*_last_scan_tick = 0`. The `last_data_tick == 0` arm is **true on the first main-loop iteration**, so the scan is stopped and marked `MarauderError` before Marauder can stream any lines.

Secondary: Marauder `sniffbt` only prints **first sighting** of each BLE device (RSSI updates are silent). Silence after results is normal, not a failure.

## Contract
1. Timeout is measured from **scan start** (`last_rescan_tick`), not from a zeroed data tick.
2. Timeout → `ERR` only when **zero** parsed results for the full timeout window.
3. If at least one result exists, silence does **not** force `ERR` (BLE dedup is expected).
4. Auto-rescan may still restart a scan on interval when auto-rescan is ON.
5. Commands: `sniffbt` / `sniffbeacon` (JCMK Marauder CLI; `scanap` removed from current CommandLine.h).
6. BLE line format: `-60 Device: NameOrMac` (verified in WiFiScan.cpp).

## Verification
- Host unit test for pure timeout decision function.
- Build clean (`ufbt`, -Werror).
- Device: BLE tab OK → stays `sniff...` ≥5s without ERR when BFFB present; ERR only after real 30s with no devices.

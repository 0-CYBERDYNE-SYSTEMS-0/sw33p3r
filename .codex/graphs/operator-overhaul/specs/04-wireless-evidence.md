# Wi-Fi and BLE evidence

Identity is MAC-first when a real address exists. Display SSID/name as a label,
not identity; hidden SSIDs remain `Hidden/unknown`, never fabricated as `AP_n`.
Do not collapse same-name devices with different MAC addresses. Track first seen,
last seen, observation count, RSSI, source, channel where available, table-full
count, UART-line drop count, and scan-window state.

Wi-Fi and BLE screens must browse stored rows with Up/Down and show row position,
identity/label, RSSI, observation age/count, channel/source, and lock state. A
zero-result timeout says `Not observed in this scan` rather than `none present`.

Evidence language:

- Beacon: `AP beacon heard`.
- Probe: `Probe request heard`.
- AP/station relation: `Frames heard with AP`.
- BLE: `Advertisement/scan response heard`; current upstream `sniffbt` uses active
  scanning and must not be labeled strictly passive.
- Permanent limitation: `Internet telemetry not measured`.

Adding `scanall`/probe parsing is allowed only with exact upstream fixtures and
without attack commands. Firmware presence/status requires a real response or
version/help handshake, not merely acquiring a UART handle.

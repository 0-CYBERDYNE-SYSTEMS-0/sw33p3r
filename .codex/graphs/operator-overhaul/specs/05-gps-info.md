# GPS, status, and information pages

Only one owner may feed each NMEA parser state. ISR callbacks queue bytes or use
separate parser instances; the main loop merges validated fixes. Never let GPIO
and Marauder callbacks mutate the same parser buffer concurrently.

BFFB hardware routes GPS through its ESP32, so Marauder NMEA is the default BFFB
source. Optional direct LPUART GPS is a separately labeled external-GPS profile,
not silently assumed. Show source, link state, fresh/stale/no-fix, time/date,
position (when present), satellites, speed/course, parser counters, and mark
distance across browsable pages.

Info pages must distinguish unavailable, handle open, response confirmed,
streaming, stale, error, and storage-degraded states. Radio path `None` must never
render as internal. Include a compact glossary for Survey, Sweep, Peak, Lock,
RSSI, and the observation limitations.

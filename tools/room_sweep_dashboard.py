#!/usr/bin/env python3
"""Room Sweep host dashboard — session evidence to static HTML + GeoJSON + WiGLE.

Usage:
    python3 tools/room_sweep_dashboard.py /path/to/sessions/ [-o out_dir]

Inputs (read-only, never modified):
    session-N.csv   event rows (schema 3 columns; older/rotated files tolerated)
    report-N.txt    plain-English Room Report (optional)
    watchlist.txt   opt-in operator labels (optional; enables labeled joins)

Outputs (default ./sweep_dashboard/):
    index.html      self-contained dark console dashboard (Leaflet via CDN with
                    a graceful no-map fallback; renders fine offline)
    map-N.geojson   GPS path + event points + RID drone/operator markers
                    (ordinals only, never raw identifiers)
    wigle-N.csv     WiGLE-compatible CSV, only for sessions that logged
                    coordinates

Privacy contract (mirrors the device):
    * Identifiers render as per-session ordinals (AP-01, BL-02, DR-03, ...).
    * MAC/serial-shaped strings are redacted from every rendered field.
    * Coordinates appear only from sessions whose rows carried them.
    * No cross-session de-anonymization. watchlist.txt joins are opt-in and
      render with a visible honesty banner.

Python 3 stdlib only.
"""

import argparse
import csv
import html
import json
import os
import re
import sys
from datetime import datetime

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

SCHEMA3_COLUMNS = [
    "schema", "seq", "tick_ms", "event", "source", "mode", "submode", "id",
    "rssi", "freq_hz", "channel", "has_pos", "lat", "lon", "has_gps",
    "fix_quality", "sats_used", "sats_view", "speed_kmh", "course_deg",
    "gps_utc", "gps_date", "gps_nav_sentences", "gps_rx_bytes", "count",
    "state", "error_code", "detail",
]

SESSION_FILE_RE = re.compile(r"session-(\d+)\.csv$", re.IGNORECASE)
REPORT_FILE_RE = re.compile(r"report-(\d+)\.txt$", re.IGNORECASE)
WATCHLIST_NAME = "watchlist.txt"

# MAC / EUI-64 / serial-shaped colon-or-dash hex identifiers.
MAC_RE = re.compile(
    r"(?i)(?<![0-9a-f])(?:[0-9a-f]{2}[:-]){5,7}[0-9a-f]{2}(?![0-9a-f])"
)
KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s,;]+)")
ORDINAL_RE = re.compile(r"^[A-Z][A-Z0-9]{0,3}-\d{1,4}$")

KNOWN_SOURCES = ("RF", "WIFI", "BLE", "NRF24", "GPS", "TX", "WATCH",
                 "RID", "FPV", "SYSTEM")

WIGLE_COLUMNS = (
    "MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
    "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type"
)
WIGLE_HEADER = (
    "WigleWifi-1.4,appRelease=room_sweep_dashboard,model=RoomSweep,"
    "release=1,device=flipper,display=room_sweep,board=room_sweep,"
    "brand=Flipper"
)

HONESTY_LINE = (
    "RSSI is not distance; wireless evidence does not prove Internet "
    "telemetry, recording, ownership, or intent; no observation does not "
    "prove absence; bounded carrier is not replay."
)

# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------


def redact(text):
    """Mask MAC/serial-shaped identifiers. Applied to every rendered field."""
    return MAC_RE.sub("[redacted]", text if text else "")


def e(text):
    return html.escape(redact("" if text is None else str(text)), quote=True)


def to_int(value, default=0):
    try:
        return int(float(str(value).strip()))
    except (TypeError, ValueError):
        return default


def to_float(value, default=None):
    try:
        return float(str(value).strip())
    except (TypeError, ValueError):
        return default


def parse_tokens(detail):
    """Extract key=value tokens from the detail column (min=, max=, avg=,
    watch=, band=, lat=, lon=, ssid=, role=, ...)."""
    tokens = {}
    for key, value in KV_RE.findall(detail or ""):
        tokens[key.lower()] = value
    return tokens


def fmt_rssi(value):
    if value is None:
        return "\u2014"
    return "%d" % int(value)


def fmt_t(tick_ms):
    return "%.1f" % (tick_ms / 1000.0)


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------


class Event(object):
    __slots__ = ("tick_ms", "seq", "event", "source", "mode", "submode",
                 "ident", "rssi", "freq_hz", "channel", "lat", "lon",
                 "gps_utc", "count", "state", "detail", "tokens")

    def __init__(self, **kw):
        for slot in self.__slots__:
            setattr(self, slot, kw.get(slot))
        self.tokens = self.tokens or {}

    @property
    def coords(self):
        """(lat, lon) only for coordinate-stamped rows (GPS Log ON)."""
        if self.lat is not None and self.lon is not None:
            return (self.lat, self.lon)
        lat = to_float(self.tokens.get("lat"))
        lon = to_float(self.tokens.get("lon"))
        if lat is not None and lon is not None:
            return (lat, lon)
        return None

    @property
    def rssi_value(self):
        if self.rssi:
            return self.rssi
        return to_int(self.tokens.get("avg"), 0) or None


def _find_header(rows):
    for index, row in enumerate(rows):
        lowered = [cell.strip().lower() for cell in row]
        if "event" in lowered and "source" in lowered:
            return index, lowered
    return None, None


def parse_session_csv(path):
    """Parse one session-N.csv tolerantly: leading/trailing capture junk,
    CRLF, rotated files, malformed rows. Returns (events, n_skipped)."""
    text = read_text(path)
    if not text.strip():
        return [], 0

    rows = list(csv.reader(text.splitlines()))
    rows = [row for row in rows if any(cell.strip() for cell in row)]
    if not rows:
        return [], 0

    header_index, header = _find_header(rows)
    skipped = 0
    if header is not None:
        idx = {}
        for pos, name in enumerate(header):
            if name and name not in idx:
                idx[name] = pos
        body = rows[header_index + 1:]
    else:
        # No header (truncated/rotated): assume schema-3 positional layout.
        idx = {name: pos for pos, name in enumerate(SCHEMA3_COLUMNS)}
        body = rows

    def cell(row, name, default=""):
        pos = idx.get(name)
        if pos is None or pos >= len(row):
            return default
        return row[pos].strip()

    events = []
    for row in body:
        event_name = cell(row, "event")
        source = cell(row, "source").upper()
        if not event_name or not source:
            skipped += 1
            continue
        detail = cell(row, "detail")
        tick = to_int(cell(row, "tick_ms"), -1)
        if tick < 0 and events:
            tick = events[-1].tick_ms
        elif tick < 0:
            tick = 0
        lat = to_float(cell(row, "lat"))
        lon = to_float(cell(row, "lon"))
        if cell(row, "has_pos") not in ("1", "true", "yes"):
            # Coordinates are only trustworthy when the row is stamped.
            if not (cell(row, "lat") and cell(row, "lon")):
                lat, lon = None, None
        events.append(Event(
            tick_ms=tick,
            seq=to_int(cell(row, "seq"), len(events)),
            event=event_name,
            source=source,
            mode=cell(row, "mode"),
            submode=cell(row, "submode"),
            ident=cell(row, "id") or "-",
            rssi=to_int(cell(row, "rssi"), 0),
            freq_hz=to_int(cell(row, "freq_hz"), 0),
            channel=to_int(cell(row, "channel"), 0),
            lat=lat,
            lon=lon,
            gps_utc=cell(row, "gps_utc"),
            count=to_int(cell(row, "count"), 1),
            state=cell(row, "state"),
            detail=detail,
            tokens=parse_tokens(detail),
        ))
    events.sort(key=lambda ev: (ev.tick_ms, ev.seq))
    return events, skipped


def parse_report(path):
    """Return (text, fields) for a Room Report; fields are its Key: value
    lines for the coverage summary."""
    text = read_text(path)
    fields = {}
    for line in text.splitlines():
        match = re.match(r"^([A-Za-z][A-Za-z \-/]{1,30}):\s*(.+)$", line)
        if match:
            key = match.group(1).strip().lower()
            fields.setdefault(key, match.group(2).strip())
    return text, fields


def load_watchlist(path):
    """watchlist.txt: 'key,label' lines. Keys may be ordinals (AP-03) or
    MACs; labels are operator-supplied. Invalid/oversized lines are skipped
    whole. Returns {key_lower: label}."""
    entries = {}
    if not path or not os.path.isfile(path):
        return entries
    for line in read_text(path).splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "," not in line:
            continue
        key, label = line.split(",", 1)
        key, label = key.strip(), label.strip()
        if not key or not label or len(line) > 48:
            continue
        entries[key.lower()] = label
    return entries


def load_sessions(root):
    """Discover session-N.csv / report-N.txt / watchlist.txt. Tolerates
    missing or extra files; duplicate N keeps the first in sorted order."""
    sessions = {}
    warnings = []
    names = sorted(os.listdir(root))
    for name in names:
        path = os.path.join(root, name)
        if not os.path.isfile(path):
            continue
        match = SESSION_FILE_RE.search(name)
        if match:
            number = int(match.group(1))
            slot = sessions.setdefault(number, {"n": number})
            if "csv" in slot:
                warnings.append("duplicate session %d csv, kept %s"
                                % (number, os.path.basename(slot["csv"])))
                continue
            slot["csv"] = path
            continue
        match = REPORT_FILE_RE.search(name)
        if match:
            number = int(match.group(1))
            slot = sessions.setdefault(number, {"n": number})
            if "report" not in slot:
                slot["report"] = path
    watchlist_path = os.path.join(root, WATCHLIST_NAME)
    watchlist = load_watchlist(watchlist_path) if os.path.isfile(
        watchlist_path) else {}

    for number in sorted(sessions):
        slot = sessions[number]
        events, skipped = parse_session_csv(slot.get("csv", ""))
        slot["events"] = events
        slot["skipped"] = skipped
        if skipped:
            warnings.append("session %d: skipped %d malformed row(s)"
                            % (number, skipped))
        report_text, report_fields = parse_report(slot.get("report", ""))
        slot["report_text"] = report_text
        slot["report"] = report_fields
        slot["has_coords"] = any(ev.coords for ev in events)
        slot["watch_labels"] = {}
        for ev in events:
            label = watchlist.get(ev.ident.lower())
            if label:
                slot["watch_labels"][ev.ident] = label
    ordered = [sessions[n] for n in sorted(sessions)]
    return ordered, watchlist, warnings


# --------------------------------------------------------------------------
# Analysis
# --------------------------------------------------------------------------


def ordinal_stats(events):
    """Per-ordinal stats: obs count, min/avg/max RSSI (merging device-side
    min=/max=/avg= tokens), channels, first/last tick, sparkline values.
    Session-lifecycle SYSTEM rows and anonymous '-' ids are not devices and
    are skipped here (they stay in the timeline)."""
    groups = {}
    for ev in events:
        ident = ev.ident or "-"
        if ev.source == "SYSTEM" or ident in ("-", ""):
            continue
        group = groups.setdefault(ident, {
            "ident": ident, "source": ev.source, "n": 0,
            "values": [], "channels": set(), "freqs": set(),
            "first": ev.tick_ms, "last": ev.tick_ms,
            "min": None, "max": None, "avg": None,
            "detail_min": None, "detail_max": None,
            "detail_avg": None, "detail_avg_n": 0,
            "watch": 0,
        })
        group["n"] += 1
        group["first"] = min(group["first"], ev.tick_ms)
        group["last"] = max(group["last"], ev.tick_ms)
        if ev.source not in ("SYSTEM",) and group["source"] == "SYSTEM":
            group["source"] = ev.source
        value = ev.rssi_value
        if value:
            group["values"].append(value)
        if ev.channel:
            group["channels"].add(ev.channel)
        if ev.freq_hz:
            group["freqs"].add(ev.freq_hz)
        if ev.tokens.get("watch") == "1":
            group["watch"] += 1
        for key in ("min", "max", "avg"):
            number = to_float(ev.tokens.get(key))
            if number is None:
                continue
            if key == "min":
                current = group["detail_min"]
                group["detail_min"] = number if current is None \
                    else min(current, number)
            elif key == "max":
                current = group["detail_max"]
                group["detail_max"] = number if current is None \
                    else max(current, number)
            else:
                n = group["detail_avg_n"] + 1
                prior = group["detail_avg"] or 0.0
                group["detail_avg"] = prior + (number - prior) / n
                group["detail_avg_n"] = n
    for group in groups.values():
        values = group["values"]
        observed_min = min(values) if values else None
        observed_max = max(values) if values else None
        observed_avg = (sum(values) / float(len(values))) if values else None
        candidates_min = [v for v in (observed_min, group["detail_min"])
                          if v is not None]
        candidates_max = [v for v in (observed_max, group["detail_max"])
                          if v is not None]
        group["min"] = int(min(candidates_min)) if candidates_min else None
        group["max"] = int(max(candidates_max)) if candidates_max else None
        if observed_avg is not None:
            group["avg"] = int(round(observed_avg))
        elif group["detail_avg"] is not None:
            group["avg"] = int(round(group["detail_avg"]))
        group["channels"] = sorted(group["channels"])
        group["freqs"] = sorted(group["freqs"])
    return groups


def sparkline_svg(values, width=248, height=44, spark_id="s"):
    """Inline SVG RSSI sparkline: gradient area + accent line + end dot."""
    if not values:
        return '<span class="dim num">\u2014</span>'
    low, high = min(values), max(values)
    spread = (high - low) or 1
    pad_x, pad_y = 4.0, 6.0
    count = len(values)

    def point(i, v):
        x = pad_x + (width - 2 * pad_x) * (i / float(max(1, count - 1)))
        y = height - pad_y - (height - 2 * pad_y) * ((v - low) / float(spread))
        return x, y

    pts = [point(i, v) for i, v in enumerate(values)]
    line = " ".join("%.1f,%.1f" % p for p in pts)
    end_x, end_y = pts[-1]
    if count == 1:
        # Single sample: designed baseline + centered dot, not a stray dot.
        mid_x, mid_y = width / 2.0, height / 2.0
        return (
            '<svg class="spark" width="%(w)d" height="%(h)d" '
            'viewBox="0 0 %(w)d %(h)d" preserveAspectRatio="none" '
            'role="img" aria-label="RSSI sparkline: single sample">'
            '<line x1="%(pad).1f" y1="%(my).1f" x2="%(mx2).1f" y2="%(my).1f" '
            'stroke="#2e3949" stroke-width="1" stroke-dasharray="3 4"/>'
            '<circle cx="%(cx).1f" cy="%(cy).1f" r="3" fill="#5aa9ff"/>'
            "</svg>"
        ) % {"w": width, "h": height, "pad": pad_x, "my": mid_y,
             "mx2": width - pad_x, "cx": mid_x, "cy": mid_y}
    area = "%.1f,%.1f " % (pts[0][0], height - 1.0) + line + \
        " %.1f,%.1f" % (pts[-1][0], height - 1.0)
    return (
        '<svg class="spark" width="%(w)d" height="%(h)d" '
        'viewBox="0 0 %(w)d %(h)d" preserveAspectRatio="none" '
        'role="img" aria-label="RSSI sparkline">'
        '<defs><linearGradient id="g%(id)s" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0%%" stop-color="rgba(90,169,255,0.28)"/>'
        '<stop offset="100%%" stop-color="rgba(90,169,255,0.02)"/>'
        '</linearGradient></defs>'
        '<polygon points="%(area)s" fill="url(#g%(id)s)" stroke="none"/>'
        '<polyline points="%(line)s" fill="none" stroke="#5aa9ff" '
        'stroke-width="1.6" stroke-linejoin="round" stroke-linecap="round"/>'
        '<circle cx="%(ex).1f" cy="%(ey).1f" r="2.2" fill="#8cc4ff"/>'
        '</svg>'
    ) % {"w": width, "h": height, "id": spark_id, "area": area,
         "line": line, "ex": end_x, "ey": end_y}


# --------------------------------------------------------------------------
# Exports
# --------------------------------------------------------------------------


def geojson_for_session(session):
    """FeatureCollection: GPS path (LineString), coordinate-stamped event
    points, RID drone/operator markers keyed by ordinals only."""
    features = []
    path = []
    for ev in session["events"]:
        coords = ev.coords
        if not coords:
            continue
        lat, lon = coords
        path.append([lon, lat])
    if len(path) >= 2:
        features.append({
            "type": "Feature",
            "geometry": {"type": "LineString", "coordinates": path},
            "properties": {
                "kind": "gps_path",
                "session": session["n"],
                "points": len(path),
            },
        })
    for ev in session["events"]:
        coords = ev.coords
        if not coords:
            continue
        lat, lon = coords
        base = {
            "session": session["n"],
            "source": ev.source,
            "event": ev.event,
            "ordinal": ev.ident,
            "tick_ms": ev.tick_ms,
        }
        if ev.rssi_value:
            base["rssi"] = ev.rssi_value
        if ev.source == "RID":
            role = (ev.tokens.get("role") or ev.tokens.get("kind") or "").lower()
            if not role:
                if ev.ident.upper().startswith("OP"):
                    role = "operator"
                elif ev.ident.upper().startswith("DR"):
                    role = "drone"
                else:
                    role = "drone"
            if role in ("op",):
                role = "operator"
            base["kind"] = "rid"
            base["role"] = role
        else:
            base["kind"] = "event"
        features.append({
            "type": "Feature",
            "geometry": {"type": "Point",
                         "coordinates": [round(lon, 6), round(lat, 6)]},
            "properties": base,
        })
    return {"type": "FeatureCollection", "features": features}


def wigle_rows(session):
    """WiGLE rows for coordinate-stamped WIFI/BLE observations. The MAC
    column carries the per-session ordinal — privacy mirror, never a raw
    identifier."""
    rows = []
    for ev in session["events"]:
        coords = ev.coords
        if not coords:
            continue
        if ev.source not in ("WIFI", "BLE"):
            continue
        if ev.event not in ("observation", "snapshot", "beacon", "probe",
                            "advertisement"):
            continue
        lat, lon = coords
        ssid = ev.tokens.get("ssid", "")
        auth = ev.tokens.get("auth", "[ESS]" if ev.source == "WIFI" else "[BLE]")
        first_seen = ev.gps_utc or ev.tokens.get("utc", "")
        row_type = "W" if ev.source == "WIFI" else "B"
        rows.append([
            ev.ident,           # MAC column: per-session ordinal (redacted)
            ssid,               # SSID
            auth,               # AuthMode
            first_seen,         # FirstSeen
            str(ev.channel),
            str(ev.rssi_value or 0),
            repr(lat) if lat is not None else "",
            repr(lon) if lon is not None else "",
            "0.0", "0.0",       # AltitudeMeters, AccuracyMeters
            row_type,           # Type
        ])
    return rows


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------

SOURCE_COLORS = {
    "RF": ("#6ea8fe", "rgba(110,168,254,0.12)", "rgba(110,168,254,0.30)"),
    "WIFI": ("#57c785", "rgba(87,199,133,0.12)", "rgba(87,199,133,0.30)"),
    "BLE": ("#9d7cd8", "rgba(157,124,216,0.12)", "rgba(157,124,216,0.30)"),
    "NRF24": ("#d4a03c", "rgba(212,160,60,0.12)", "rgba(212,160,60,0.30)"),
    "GPS": ("#3fc9b0", "rgba(63,201,176,0.12)", "rgba(63,201,176,0.30)"),
    "TX": ("#ef6b6b", "rgba(239,107,107,0.12)", "rgba(239,107,107,0.30)"),
    "WATCH": ("#9aa5b5", "rgba(154,165,181,0.12)", "rgba(154,165,181,0.30)"),
    "RID": ("#f08c46", "rgba(240,140,70,0.12)", "rgba(240,140,70,0.30)"),
    "FPV": ("#4fc3e8", "rgba(79,195,232,0.12)", "rgba(79,195,232,0.30)"),
    "SYSTEM": ("#6b7686", "rgba(107,118,134,0.10)", "rgba(107,118,134,0.24)"),
}


def chip(source, count=None):
    color, bg, border = SOURCE_COLORS.get(
        source.upper(), SOURCE_COLORS["SYSTEM"])
    count_html = (' <span class="n">%d</span>' % count) if count is not None else ""
    return ('<span class="chip" style="color:%s;background:%s;'
            'border-color:%s">%s%s</span>'
            % (color, bg, border, e(source), count_html))


def detail_html(ev):
    parts = []
    if ev.detail:
        parts.append(e(ev.detail))
    extras = []
    if ev.freq_hz:
        extras.append("%.3f MHz" % (ev.freq_hz / 1e6))
    if ev.channel:
        extras.append("ch %d" % ev.channel)
    if extras:
        parts.append('<span class="dim">%s</span>' % e(" \u00b7 ".join(extras)))
    return " ".join(parts) if parts else '<span class="dim">\u2014</span>'


def render_index(sessions, watchlist, warnings, source_dir):
    """Self-contained dark-console dashboard."""
    out = []
    add = out.append
    generated = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")
    has_watch = bool(watchlist)
    joined = any(s["watch_labels"] for s in sessions)

    total_events = sum(len(s["events"]) for s in sessions)
    gps_sessions = sum(1 for s in sessions if s["has_coords"])
    tx_attempts = sum(1 for s in sessions for ev in s["events"]
                      if ev.source == "TX")
    watch_hits = sum(1 for s in sessions for ev in s["events"]
                     if ev.tokens.get("watch") == "1")
    rid_rows = sum(1 for s in sessions for ev in s["events"]
                   if ev.source == "RID")
    source_counts = {}
    for s in sessions:
        for ev in s["events"]:
            source_counts[ev.source] = source_counts.get(ev.source, 0) + 1

    # ---- head ----
    add("<!DOCTYPE html>")
    add('<html lang="en"><head><meta charset="utf-8">')
    add('<meta name="viewport" content="width=device-width, initial-scale=1">')
    add("<title>Room Sweep \u00b7 Session Dashboard</title>")
    add("""<style>
:root{
  --bg:#0b0e14; --bg-soft:#0f131b; --panel:#141a24; --panel-2:#182029;
  --border:#232c39; --border-strong:#2e3949;
  --text:#dbe2ee; --dim:#8c96a6; --faint:#5c6675;
  --accent:#5aa9ff; --alert:#e8b04b;
  --mono:"SF Mono",ui-monospace,"JetBrains Mono",Menlo,Consolas,monospace;
  --sans:-apple-system,BlinkMacSystemFont,"SF Pro Text",Inter,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.6 var(--sans);
  -webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility}
.wrap{max-width:1180px;margin:0 auto;padding:40px 24px 80px}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}
.num{font-family:var(--mono);font-variant-numeric:tabular-nums;font-size:13px}
.dim{color:var(--dim)}
.faint{color:var(--faint)}

/* header */
header.top{display:flex;flex-wrap:wrap;align-items:flex-end;justify-content:space-between;
  gap:16px;padding-bottom:24px;border-bottom:1px solid var(--border)}
.kicker{font-family:var(--mono);font-size:11px;letter-spacing:.18em;
  text-transform:uppercase;color:var(--accent)}
h1{margin:6px 0 0;font-size:23px;font-weight:650;letter-spacing:-0.01em}
.meta{margin-top:6px;font-family:var(--mono);font-size:12px;color:var(--dim)}
.meta b{color:var(--text);font-weight:500}

/* sections */
section{margin-top:48px}
h2{margin:0 0 16px;font-size:12px;font-weight:600;letter-spacing:.16em;
  text-transform:uppercase;color:var(--dim)}
h2 .rule{display:inline-block;width:26px;height:1px;background:var(--accent);
  vertical-align:middle;margin-right:10px;opacity:.7}
h3{margin:28px 0 12px;font-size:15px;font-weight:600;letter-spacing:.01em}
h3 .sub{font-family:var(--mono);font-size:12px;font-weight:400;color:var(--faint);
  margin-left:10px;letter-spacing:0}

/* banners */
.banner{border:1px solid var(--border);border-left:3px solid var(--accent);
  background:linear-gradient(90deg,rgba(90,169,255,0.07),rgba(90,169,255,0.02));
  border-radius:8px;padding:14px 18px;font-size:13px;color:var(--dim);
  line-height:1.65}
.banner.alert{border-left-color:var(--alert);
  background:linear-gradient(90deg,rgba(232,176,75,0.08),rgba(232,176,75,0.02))}
.banner b{color:var(--text);font-weight:600}
.banner + .banner{margin-top:12px}

/* cards */
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(168px,1fr));gap:16px}
.card{background:var(--panel);border:1px solid var(--border);border-radius:10px;
  padding:16px 18px;transition:border-color .15s ease}
.card:hover{border-color:var(--border-strong)}
.card .label{font-family:var(--mono);font-size:10px;letter-spacing:.14em;
  text-transform:uppercase;color:var(--faint)}
.card .value{margin-top:8px;font-family:var(--mono);font-variant-numeric:tabular-nums;
  font-size:26px;font-weight:600;line-height:1.1;letter-spacing:-0.01em}
.card .sub{margin-top:6px;font-size:12px;color:var(--dim);line-height:1.7}

/* source mix strip */
.mix{display:flex;flex-wrap:wrap;gap:8px;margin-top:16px}
.mix .chip{padding:4px 11px;font-size:11px}
.mix .chip .n{opacity:.75;font-weight:500}

/* device cards */
.device-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}
.device{background:var(--panel);border:1px solid var(--border);border-radius:10px;
  padding:16px 18px;transition:border-color .15s ease}
.device:hover{border-color:var(--border-strong)}
.device .head{display:flex;align-items:center;justify-content:space-between;gap:8px}
.device .ident{font-family:var(--mono);font-size:14px;font-weight:600;letter-spacing:.02em}
.device .ident .label{margin-left:8px;font-size:11px;font-weight:500;color:var(--alert)}
.device .stats{display:flex;gap:24px;margin-top:14px}
.device .stat{min-width:52px}
.device .stat .k{font-family:var(--mono);font-size:9px;letter-spacing:.14em;
  text-transform:uppercase;color:var(--faint)}
.device .stat .v{font-family:var(--mono);font-variant-numeric:tabular-nums;
  font-size:15px;font-weight:600;margin-top:3px}
.device .spark-wrap{margin-top:14px;padding-top:14px;border-top:1px solid var(--border)}
.spark{display:block;width:100%;height:44px}
.device .foot{margin-top:12px;font-family:var(--mono);font-size:11px;
  color:var(--faint);line-height:1.6}

/* tables */
.tbl-wrap{overflow-x:auto;border:1px solid var(--border);border-radius:10px;
  background:var(--panel)}
table{width:100%;border-collapse:collapse;font-size:13px}
thead th{position:sticky;top:0;background:var(--panel-2);text-align:left;
  font-family:var(--mono);font-size:10px;font-weight:600;letter-spacing:.14em;
  text-transform:uppercase;color:var(--faint);padding:12px 14px;
  border-bottom:1px solid var(--border-strong);white-space:nowrap}
tbody td{padding:10px 14px;border-bottom:1px solid var(--border);
  vertical-align:top;line-height:1.5}
tbody tr:last-child td{border-bottom:none}
tbody tr{transition:background .12s ease}
tbody tr:hover{background:rgba(90,169,255,0.05)}
td .detail{color:var(--dim);font-family:var(--mono);font-size:11.5px;
  word-break:break-word;max-width:520px}
td.r{text-align:right}
.t-source .chip{margin-right:8px;vertical-align:1px}
tr.sys td{opacity:.62}

/* chips */
.chip{display:inline-flex;align-items:center;gap:5px;padding:2px 9px;
  border:1px solid transparent;border-radius:999px;font-family:var(--mono);
  font-size:10px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;
  white-space:nowrap}

/* session list */
.sess-idx{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}
.sess-card{background:var(--panel);border:1px solid var(--border);border-radius:10px;
  padding:18px 20px;transition:border-color .15s ease}
.sess-card:hover{border-color:var(--border-strong)}
.sess-card .id{font-family:var(--mono);font-size:12px;letter-spacing:.12em;
  text-transform:uppercase;color:var(--accent)}
.sess-card .row{display:flex;justify-content:space-between;gap:12px;
  margin-top:10px;font-size:12.5px;color:var(--dim)}
.sess-card .row b{color:var(--text);font-family:var(--mono);font-weight:500}
.sess-card .links{margin-top:14px;padding-top:12px;border-top:1px solid var(--border);
  display:flex;flex-wrap:wrap;gap:12px;font-family:var(--mono);font-size:11px}
.sess-card .links .none{color:var(--faint)}

/* map */
.map-shell{position:relative;border:1px solid var(--border);border-radius:10px;
  overflow:hidden;background:var(--bg-soft);
  background-image:linear-gradient(rgba(90,169,255,0.05) 1px,transparent 1px),
    linear-gradient(90deg,rgba(90,169,255,0.05) 1px,transparent 1px);
  background-size:28px 28px}
.map{height:380px;width:100%}
.map-fallback{position:absolute;inset:0;display:flex;align-items:center;
  justify-content:center;flex-direction:column;gap:12px;text-align:center;
  padding:24px;color:var(--dim);font-size:13px}
.map-fallback .title{font-family:var(--mono);font-size:10px;letter-spacing:.18em;
  text-transform:uppercase;color:var(--faint);border:1px solid var(--border-strong);
  border-radius:999px;padding:5px 14px;background:rgba(11,14,20,0.7)}
.map-ok .map-fallback{display:none}
.map-ok{background-image:none}
.leaflet-container{background:#0f131b;font:12px/1.4 var(--mono)}

/* report pre */
details.report{border:1px solid var(--border);border-radius:10px;
  background:var(--panel)}
details.report summary{cursor:pointer;padding:14px 18px;font-family:var(--mono);
  font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--dim);
  list-style:none;user-select:none}
details.report summary::-webkit-details-marker{display:none}
details.report summary:hover{color:var(--text)}
details.report pre{margin:0;padding:8px 20px 22px;font-family:var(--mono);
  font-size:12px;line-height:1.8;color:var(--dim);white-space:pre-wrap;
  word-break:break-word}

footer{margin-top:64px;padding-top:24px;border-top:1px solid var(--border);
  font-family:var(--mono);font-size:11px;color:var(--faint);line-height:1.8}
</style></head><body><div class="wrap">""")

    # ---- header ----
    add('<header class="top"><div>')
    add('<div class="kicker">room sweep \u00b7 host dashboard</div>')
    add("<h1>Session evidence console</h1>")
    add('<div class="meta">generated %s &nbsp;\u00b7&nbsp; source <b>%s</b> '
        '&nbsp;\u00b7&nbsp; <b>%d</b> session%s &nbsp;\u00b7&nbsp; <b>%d</b> '
        "events</div>"
        % (e(generated), e(os.path.basename(source_dir.rstrip("/")) or source_dir),
           len(sessions), "" if len(sessions) == 1 else "s", total_events))
    add("</div></header>")

    # ---- privacy banners ----
    add("<section style=\"margin-top:28px\">")
    add('<div class="banner"><b>Privacy mirror.</b> Identifiers render as '
        "per-session ordinals (AP-01, BL-02, DR-03 \u2026); MAC/serial-shaped "
        "values are redacted from every rendered field. Coordinates appear "
        "only for sessions whose rows carried them (GPS Log ON). This "
        "dashboard never de-anonymizes across sessions. <span class=\"faint\">%s</span></div>"
        % e(HONESTY_LINE))
    if has_watch:
        add('<div class="banner alert"><b>Opt-in cross-session identity.</b> '
            "watchlist.txt is present: operator-supplied labels are joined "
            "onto matching per-session ordinals. Ordinals are <b>not</b> stable "
            "identities across sessions \u2014 labels are operator guesses, "
            "shown for convenience only. Raw watchlist keys are never "
            "rendered.</div>")
    add("</section>")

    # ---- coverage summary ----
    add('<section id="coverage"><h2><span class="rule"></span>Coverage summary</h2>')
    add('<div class="cards">')
    cards = [
        ("sessions", str(len(sessions)),
         "%s with GPS" % gps_sessions),
        ("events", str(total_events),
         "%d malformed skipped" % sum(s["skipped"] for s in sessions)),
        ("tx attempts", str(tx_attempts), "bounded carrier only"),
        ("watch hits", str(watch_hits), "burst watch rows"),
        ("rid rows", str(rid_rows), "drone / operator markers"),
        ("sources", str(len(source_counts)), "distinct event sources"),
    ]
    for label, value, sub in cards:
        add('<div class="card"><div class="label">%s</div>'
            '<div class="value">%s</div><div class="sub">%s</div></div>'
            % (e(label), e(value), e(sub)))
    add("</div>")
    add('<div class="mix">')
    for k in list(KNOWN_SOURCES) + sorted(
            k for k in source_counts if k not in KNOWN_SOURCES):
        if k in source_counts:
            add(chip(k, source_counts[k]))
    add("</div>")
    if warnings:
        add('<p class="dim" style="margin-top:16px;font-size:12px">'
            "tolerated input issues: %s</p>"
            % e("; ".join(warnings)))
    add("</section>")

    # ---- session list ----
    add('<section id="sessions"><h2><span class="rule"></span>Sessions</h2>')
    add('<div class="sess-idx">')
    for s in sessions:
        events = s["events"]
        sources = sorted({ev.source for ev in events})
        span = (max(ev.tick_ms for ev in events) - min(ev.tick_ms for ev in events)
                ) / 1000.0 if events else 0.0
        status = s["report"].get("status", "\u2014")
        coverage = s["report"].get("coverage", "\u2014")
        add('<div class="sess-card">')
        add('<div class="id">session %d</div>' % s["n"])
        add('<div class="row"><span>events</span><b>%d</b></div>' % len(events))
        add('<div class="row"><span>duration</span><b>%.1f s</b></div>' % span)
        add('<div class="row"><span>sources</span><b>%s</b></div>'
            % e(", ".join(sources) or "\u2014"))
        add('<div class="row"><span>gps</span><b>%s</b></div>'
            % ("logged" if s["has_coords"] else "omitted"))
        add('<div class="row"><span>report</span><b>%s \u00b7 %s</b></div>'
            % (e(status), e(coverage)))
        links = ['<a href="#timeline-%d">timeline</a>' % s["n"],
                 '<a href="#stats-%d">per-device</a>' % s["n"]]
        if events:
            links.insert(0, '<a href="map-%d.geojson">map-%d.geojson</a>'
                         % (s["n"], s["n"]))
        if any(ev.coords and ev.source in ("WIFI", "BLE") for ev in events):
            links.insert(1, '<a href="wigle-%d.csv">wigle-%d.csv</a>'
                         % (s["n"], s["n"]))
        add('<div class="links">%s</div>' % "".join(links))
        add("</div>")
    if not sessions:
        add('<div class="sess-card"><div class="dim">No session-N.csv files '
            "found in the input directory.</div></div>")
    add("</div></section>")

    # ---- per-session detail ----
    spark_counter = [0]
    for s in sessions:
        events = s["events"]
        if not events:
            continue
        add('<section id="timeline-%d">' % s["n"])
        add('<h2><span class="rule"></span>Unified timeline \u00b7 session %d'
            '<span style="letter-spacing:0;text-transform:none;font-weight:400;'
            'color:var(--faint);margin-left:12px">all sources, sorted by time '
            "(%d rows)</span></h2>" % (s["n"], len(events)))

        add('<div class="tbl-wrap"><table><thead><tr>'
            "<th>t+</th><th>tick ms</th><th>source</th><th>event</th>"
            "<th>id</th><th class=\"r\">rssi</th><th>detail</th>"
            "</tr></thead><tbody>")
        t0 = events[0].tick_ms
        for ev in events:
            label = s["watch_labels"].get(ev.ident)
            id_html = '<span class="num">%s</span>' % e(ev.ident)
            if label:
                id_html += ' <span class="dim" style="font-size:11px">\u00b7 %s</span>' % e(label)
            row_class = ' class="sys"' if ev.source == "SYSTEM" else ""
            add("<tr%s data-session=\"%d\" data-tick=\"%d\">"
                '<td class="num dim">%s</td><td class="num dim">%d</td>'
                '<td class="t-source">%s</td><td class="num">%s</td>'
                "<td>%s</td><td class=\"num r\">%s</td>"
                '<td class="detail">%s</td></tr>'
                % (row_class, s["n"], ev.tick_ms,
                   fmt_t(ev.tick_ms - t0), ev.tick_ms, chip(ev.source),
                   e(ev.event), id_html, fmt_rssi(ev.rssi_value),
                   detail_html(ev)))
        add("</tbody></table></div>")

        # per-ordinal device cards
        add('<h3 id="stats-%d">Per-device history<span class="sub">'
            "RSSI dBm \u00b7 min / avg / max \u00b7 sparkline over time</span></h3>"
            % s["n"])
        stats = ordinal_stats(events)
        add('<div class="device-grid">')
        for ident in sorted(stats, key=lambda k: (stats[k]["source"], k)):
            g = stats[ident]
            spark_counter[0] += 1
            label = s["watch_labels"].get(ident)
            ident_html = '<span class="ident">%s' % e(ident)
            if label:
                ident_html += '<span class="label">%s</span>' % e(label)
            ident_html += "</span>"
            channels = ("ch " + ",".join(str(c) for c in g["channels"])) \
                if g["channels"] else ""
            freqs = (" \u00b7 " + ", ".join("%.3f MHz" % (f / 1e6)
                                           for f in g["freqs"])) \
                if g["freqs"] else ""
            add('<div class="device"><div class="head">%s%s</div>'
                '<div class="stats">'
                '<div class="stat"><div class="k">min</div><div class="v">%s</div></div>'
                '<div class="stat"><div class="k">avg</div><div class="v">%s</div></div>'
                '<div class="stat"><div class="k">max</div><div class="v">%s</div></div>'
                '<div class="stat"><div class="k">obs</div><div class="v">%d</div></div>'
                "</div>"
                '<div class="spark-wrap">%s</div>'
                '<div class="foot">first t+%ss \u00b7 last t+%ss%s%s</div>'
                "</div>"
                % (ident_html, chip(g["source"]),
                   fmt_rssi(g["min"]), fmt_rssi(g["avg"]), fmt_rssi(g["max"]),
                   g["n"],
                   sparkline_svg(g["values"], spark_id="sp%d" % spark_counter[0]),
                   fmt_t(g["first"] - t0), fmt_t(g["last"] - t0),
                   (" \u00b7 " + channels) if channels else "", freqs))
        add("</div>")

        # map
        if s["has_coords"]:
            add('<h3>Map<span class="sub">GPS path + event points + RID '
                "markers (ordinals only)</span></h3>")
            add('<div class="map-shell" id="mapshell-%d">'
                '<div class="map" id="map-%d"></div>'
                '<div class="map-fallback"><div class="title">map offline</div>'
                "<div>Leaflet could not load (no network) \u2014 the GPS path "
                "and event points are listed in the table below.</div></div>"
                "</div>" % (s["n"], s["n"]))
            has_rid = any(ev.source == "RID" and ev.coords for ev in events)
            role_head = "<th>role</th>" if has_rid else ""
            add('<div class="tbl-wrap" style="margin-top:16px"><table>'
                "<thead><tr><th>kind</th>%s<th>id</th>"
                '<th class="r">lat</th><th class="r">lon</th><th>t+</th>'
                "</tr></thead><tbody>" % role_head)
            for ev in events:
                coords = ev.coords
                if not coords:
                    continue
                lat, lon = coords
                role = ev.tokens.get("role") or ev.tokens.get("kind") or ""
                if ev.source == "RID" and not role:
                    role = "operator" if ev.ident.upper().startswith("OP") \
                        else "drone"
                role_cell = ('<td class="num dim">%s</td>' % e(role)) \
                    if has_rid else ""
                add('<tr><td class="num">%s</td>%s'
                    '<td class="num">%s</td>'
                    '<td class="num r">%s</td><td class="num r">%s</td>'
                    '<td class="num dim">%s</td></tr>'
                    % (e("rid" if ev.source == "RID" else ev.source),
                       role_cell, e(ev.ident),
                       ("%.5f" % lat), ("%.5f" % lon),
                       fmt_t(ev.tick_ms - t0)))
            add("</tbody></table></div>")

        # report text
        if s["report_text"]:
            add('<details class="report" style="margin-top:24px"><summary>'
                "Room report \u00b7 report-%d.txt</summary><pre>%s</pre>"
                "</details>" % (s["n"], e(s["report_text"])))
        add("</section>")

    # ---- maps data + leaflet bootstrap ----
    maps = {}
    for s in sessions:
        if s["has_coords"]:
            maps[str(s["n"])] = geojson_for_session(s)
    add("<script>window.SWEEP_MAPS=%s;</script>"
        % redact(json.dumps(maps, separators=(",", ":"))).replace("</", "<\\/"))
    add("""<script>
(function(){
  function showFallbacks(){
    var nodes=document.querySelectorAll('.map-shell');
    for(var i=0;i<nodes.length;i++){nodes[i].classList.remove('map-ok');}
  }
  function boot(){
    try{
      if(!window.L||!window.SWEEP_MAPS){showFallbacks();return;}
      var byId={};
      var ids=Object.keys(window.SWEEP_MAPS);
      for(var k=0;k<ids.length;k++){
        var n=ids[k];
        var el=document.getElementById('map-'+n);
        if(!el){continue;}
        var map=L.map(el,{scrollWheelZoom:false});
        var data=window.SWEEP_MAPS[n];
        var layer=L.geoJSON(data,{
          style:function(){return {color:'#5aa9ff',weight:2.5,opacity:.9};},
          pointToLayer:function(feature,latlng){
            var p=feature.properties||{};
            var color=p.kind==='rid'?(p.role==='operator'?'#e8b04b':'#f08c46'):'#5aa9ff';
            return L.circleMarker(latlng,{radius:p.kind==='rid'?7:5,
              color:color,weight:1.5,fillColor:color,fillOpacity:.35});
          },
          onEachFeature:function(feature,lyr){
            var p=feature.properties||{};
            if(p.kind==='gps_path'){return;}
            var lines=[(p.source||'')+' \u00b7 '+(p.event||'')];
            lines.push('id '+(p.ordinal||'-')+(p.role?'\u00b7'+p.role:''));
            if(p.rssi){lines.push('rssi '+p.rssi);}
            lyr.bindPopup(lines.join('<br>'));
          }
        }).addTo(map);
        try{
          map.fitBounds(layer.getBounds().pad(0.2));
        }catch(err){map.setView([0,0],2);}
        L.control.attribution({prefix:false}).addTo(map);
        document.getElementById('mapshell-'+n).classList.add('map-ok');
      }
    }catch(err){showFallbacks();}
  }
  if(document.readyState==='loading'){
    document.addEventListener('DOMContentLoaded',boot);
  }else{boot();}
})();
</script>""")

    add('<footer>room_sweep_dashboard.py \u00b7 static output \u00b7 no data '
        "leaves this machine \u00b7 WiGLE uploads are manual and optional<br>"
        "%s</footer>" % e(HONESTY_LINE))
    add("</div></body></html>")
    return "\n".join(out)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def generate(root, out_dir):
    """Generate all outputs. Returns list of written paths."""
    sessions, watchlist, warnings = load_sessions(root)
    os.makedirs(out_dir, exist_ok=True)
    written = []

    index_path = os.path.join(out_dir, "index.html")
    with open(index_path, "w", encoding="utf-8") as handle:
        handle.write(render_index(sessions, watchlist, warnings, root))
    written.append(index_path)

    for s in sessions:
        if not s["events"]:
            continue
        geo = geojson_for_session(s)
        map_path = os.path.join(out_dir, "map-%d.geojson" % s["n"])
        with open(map_path, "w", encoding="utf-8") as handle:
            handle.write(redact(json.dumps(geo, indent=1)) + "\n")
        written.append(map_path)
        if s["has_coords"]:
            rows = wigle_rows(s)
            if rows:
                wigle_path = os.path.join(out_dir, "wigle-%d.csv" % s["n"])
                with open(wigle_path, "w", encoding="utf-8",
                          newline="") as handle:
                    handle.write(WIGLE_HEADER + "\n")
                    handle.write(WIGLE_COLUMNS + "\n")
                    writer = csv.writer(handle, lineterminator="\n",
                                        quoting=csv.QUOTE_MINIMAL)
                    for row in rows:
                        writer.writerow([redact(cell) for cell in row])
                written.append(wigle_path)
    return written, sessions


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="room_sweep_dashboard",
        description="Room Sweep host dashboard: session-N.csv / report-N.txt "
                    "to static HTML + GeoJSON + WiGLE CSV (stdlib only).")
    parser.add_argument("sessions_dir",
                        help="directory holding session-N.csv / report-N.txt "
                             "/ watchlist.txt (read-only)")
    parser.add_argument("-o", "--out", default="./sweep_dashboard",
                        help="output directory (default ./sweep_dashboard/)")
    args = parser.parse_args(argv)

    root = os.path.abspath(args.sessions_dir)
    if not os.path.isdir(root):
        parser.error("sessions dir not found: %s" % root)
    out_dir = os.path.abspath(args.out)

    written, sessions = generate(root, out_dir)
    print("sessions: %d" % len(sessions))
    for path in written:
        print("wrote: %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

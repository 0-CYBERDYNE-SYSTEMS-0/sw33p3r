#!/usr/bin/env python3
"""Fixture self-test for tools/room_sweep_dashboard.py (stdlib only).

Synthesizes session fixtures in a tmp dir (multi-source timeline, GPS-stamped
rows, RID rows, malformed/missing-file tolerance), runs the dashboard, and
asserts: timeline ordering, GeoJSON structure, privacy redaction, WiGLE
column order and coordinate gating, watchlist honesty banner, and input
immutability. Then round-trips against any real session CSV in
.omo/evidence/ when one exists (skipped with a message otherwise).

Run: python3 tools/test_dashboard_fixtures.py
"""

import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DASH = os.path.join(HERE, "room_sweep_dashboard.py")
EVIDENCE = os.path.join(REPO, ".omo", "evidence")

SCHEMA3_COLS = [
    "schema", "seq", "tick_ms", "event", "source", "mode", "submode", "id",
    "rssi", "freq_hz", "channel", "has_pos", "lat", "lon", "has_gps",
    "fix_quality", "sats_used", "sats_view", "speed_kmh", "course_deg",
    "gps_utc", "gps_date", "gps_nav_sentences", "gps_rx_bytes", "count",
    "state", "error_code", "detail",
]

WIGLE_COLUMNS = (
    "MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
    "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type"
)

MAC_RE = re.compile(
    r"(?i)(?<![0-9a-f])(?:[0-9a-f]{2}[:-]){5,7}[0-9a-f]{2}(?![0-9a-f])"
)
TIMELINE_RE = re.compile(r'data-session="(\d+)" data-tick="(\d+)"')
ORDINAL_RE = re.compile(r"^[A-Z][A-Z0-9]{0,3}-\d{1,4}$")

RAW_MAC = "aa:bb:cc:dd:ee:ff"
RAW_SERIAL = "01:23:45:67:89:AB"


# --------------------------------------------------------------------------
# Fixture synthesis
# --------------------------------------------------------------------------


def make_row(seq, tick_ms, event, source, ident, **kw):
    row = {name: "" for name in SCHEMA3_COLS}
    row["schema"] = "3"
    row["seq"] = str(seq)
    row["tick_ms"] = str(tick_ms)
    row["event"] = event
    row["source"] = source
    row["id"] = ident
    row["rssi"] = str(kw.get("rssi", 0))
    row["freq_hz"] = str(kw.get("freq_hz", 0))
    row["channel"] = str(kw.get("channel", 0))
    row["has_pos"] = str(kw.get("has_pos", 0))
    row["count"] = "1"
    if kw.get("lat") is not None:
        row["lat"] = repr(kw["lat"])
        row["lon"] = repr(kw["lon"])
        row["has_gps"] = "1"
    row["gps_utc"] = kw.get("gps_utc", "")
    row["state"] = kw.get("state", "")
    row["detail"] = kw.get("detail", "")
    return [row[name] for name in SCHEMA3_COLS]


def write_session(path, rows, junk_prefix=(), junk_suffix=()):
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        for line in junk_prefix:
            handle.write(line + "\n")
        writer.writerow(SCHEMA3_COLS)
        for row in rows:
            writer.writerow(row)
        for line in junk_suffix:
            handle.write(line + "\n")


def build_fixture_sessions(root):
    """Three fixture sessions: (1) multi-source timeline with unsorted ticks,
    detail tokens and MAC/serial strings to prove redaction; (2) GPS-stamped
    rows incl. RID drone/operator markers; (3) coordinate-free (WiGLE
    gating negative)."""
    os.makedirs(root, exist_ok=True)

    # session 1: multi-source timeline, ticks deliberately out of order.
    write_session(os.path.join(root, "session-1.csv"), [
        make_row(0, 1000, "begin", "SYSTEM", "-",
                 detail="session started"),
        make_row(1, 3000, "observation", "WIFI", "AP-01",
                 rssi=-70, channel=6,
                 detail="min=-82 max=-44 avg=-61 band=2 raw=%s ssid=LabNet"
                        % RAW_MAC),
        make_row(2, 2500, "observation", "BLE", "BL-01",
                 rssi=-60, detail="watch=1"),
        make_row(3, 2200, "observation", "NRF24", "ID-07",
                 channel=13, detail="rpd_pass top=13 tot=4"),
        make_row(4, 2800, "rf", "RF", "RF-01",
                 rssi=-88, freq_hz=433920000,
                 detail="min=-90 max=-80 avg=-85"),
        make_row(5, 3500, "tx", "TX", "TX-01",
                 detail="keyed OOK carrier 3 s"),
        make_row(6, 2600, "watch", "WATCH", "WATCH",
                 rssi=-75, detail="watch=1 band=1"),
        make_row(7, 3200, "rid", "RID", "DR-01", detail="role=drone"),
        make_row(8, 3300, "rid", "RID", "OP-01", detail="role=operator"),
        make_row(9, 3400, "fpv", "FPV", "FPV-01",
                 detail="serial=%s" % RAW_SERIAL),
        make_row(11, 3150, "observation", "WIFI", "AP-01",
                 rssi=-75, channel=6, detail="ssid=LabNet"),
        make_row(12, 3450, "observation", "WIFI", "AP-01",
                 rssi=-66, channel=6, detail="ssid=LabNet"),
        make_row(13, 2550, "observation", "BLE", "BL-01",
                 rssi=-64, detail="watch=1"),
        make_row(10, 4000, "end", "SYSTEM", "-", detail="records=10"),
    ])
    with open(os.path.join(root, "report-1.txt"), "w",
              encoding="utf-8") as handle:
        handle.write(
            "ROOM REPORT\nStatus: COMPLETE\nCoverage: FULL\n"
            "Sensors confirmed: RF, Wi-Fi, BLE, nRF24; GPS unavailable\n"
            "TX: 1 bounded carrier attempt\nDropped events: 0\n"
            "Limitations: RSSI is not distance.\n")

    # session 2: GPS-stamped rows + RID drone/operator markers.
    write_session(os.path.join(root, "session-2.csv"), [
        make_row(0, 0, "begin", "SYSTEM", "-", detail="session started"),
        make_row(1, 500, "snapshot", "GPS", "GPS-01",
                 has_pos=1, lat=37.77490, lon=-122.41940,
                 gps_utc="2026-10-11 12:00:00", detail="fix"),
        make_row(2, 1500, "observation", "WIFI", "AP-01",
                 rssi=-66, channel=11, has_pos=1,
                 lat=37.77500, lon=-122.41900,
                 gps_utc="2026-10-11 12:00:01",
                 detail="ssid=CafeWifi auth=[WPA2-CCMP][ESS]"),
        make_row(3, 2000, "observation", "WIFI", "AP-02",
                 rssi=-71, channel=6, has_pos=1,
                 lat=37.77520, lon=-122.41850,
                 gps_utc="2026-10-11 12:00:02", detail="ssid=Guest"),
        make_row(4, 2500, "observation", "BLE", "BL-01",
                 rssi=-58, has_pos=1, lat=37.77540, lon=-122.41800),
        make_row(5, 3000, "rid", "RID", "DR-01",
                 has_pos=1, lat=37.77560, lon=-122.41750,
                 detail="role=drone"),
        make_row(6, 3200, "rid", "RID", "OP-01",
                 has_pos=1, lat=37.77580, lon=-122.41700,
                 detail="role=operator"),
        make_row(7, 4000, "end", "SYSTEM", "-", detail="records=8"),
    ])

    # session 3: no coordinates anywhere (WiGLE gating negative).
    write_session(os.path.join(root, "session-3.csv"), [
        make_row(0, 0, "begin", "SYSTEM", "-"),
        make_row(1, 1000, "observation", "WIFI", "AP-01",
                 rssi=-62, channel=1, detail="ssid=NoGeo"),
        make_row(2, 2000, "observation", "BLE", "BL-01", rssi=-55),
        make_row(3, 3000, "end", "SYSTEM", "-"),
    ])
    return root


def build_tolerance_dir(root):
    """Rotated / malformed / missing-file tolerance: capture junk around a
    valid header, truncated rows, a report with no CSV, an empty CSV, and a
    CSV with invalid UTF-8 bytes."""
    os.makedirs(root, exist_ok=True)
    write_session(
        os.path.join(root, "session-6.csv"),
        [make_row(0, 100, "begin", "SYSTEM", "-"),
         make_row(1, 200, "observation", "WIFI", "AP-01", rssi=-70),
         ["3", "2", "300"],  # truncated row: must be skipped whole
         make_row(3, 400, "end", "SYSTEM", "-")],
        junk_prefix=("storage read /ext/apps_data/room_sweep/session-6.csv",
                     "Size: 221"),
        junk_suffix=(">: ", ""))
    with open(os.path.join(root, "report-7.txt"), "w",
              encoding="utf-8") as handle:
        handle.write("ROOM REPORT\nStatus: COMPLETE\n")
    with open(os.path.join(root, "session-8.csv"), "w",
              encoding="utf-8") as handle:
        handle.write("")
    with open(os.path.join(root, "session-9.csv"), "wb") as handle:
        handle.write(b"schema,seq,tick_ms,event,source,id,detail\n")
        handle.write(b"3,0,0,begin,SYSTEM,-,bad byte:\xff\xfe\n")
        handle.write(b"3,1,10,observation,WIFI,AP-01,ok\n")
    return root


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def hash_tree(root):
    digest = {}
    for name in sorted(os.listdir(root)):
        path = os.path.join(root, name)
        if os.path.isfile(path):
            with open(path, "rb") as handle:
                digest[name] = hashlib.sha256(handle.read()).hexdigest()
    return digest


def run_dashboard(sessions_dir, out_dir):
    return subprocess.run(
        [sys.executable, DASH, sessions_dir, "-o", out_dir],
        capture_output=True, text=True)


# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------


class DashboardFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.mkdtemp(prefix="sweep_dash_test_")
        cls.sessions = os.path.join(cls._tmp, "sessions")
        cls.out = os.path.join(cls._tmp, "out")
        build_fixture_sessions(cls.sessions)
        cls.before = hash_tree(cls.sessions)
        cls.proc = run_dashboard(cls.sessions, cls.out)
        cls.after = hash_tree(cls.sessions)
        cls.index = read(os.path.join(cls.out, "index.html"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls._tmp, ignore_errors=True)

    # -- CLI contract -----------------------------------------------------

    def test_cli_exits_zero_and_prints_file_list(self):
        self.assertEqual(self.proc.returncode, 0, self.proc.stderr)
        self.assertEqual(self.proc.stderr, "")
        self.assertIn("wrote:", self.proc.stdout)
        self.assertIn("index.html", self.proc.stdout)
        self.assertIn("map-2.geojson", self.proc.stdout)
        self.assertIn("wigle-2.csv", self.proc.stdout)

    def test_inputs_never_modified(self):
        self.assertEqual(self.before, self.after)

    # -- timeline ---------------------------------------------------------

    def test_timeline_ordering_per_session(self):
        pairs = TIMELINE_RE.findall(self.index)
        self.assertTrue(pairs, "no timeline rows rendered")
        seen = {}
        for session, tick in pairs:
            seen.setdefault(int(session), []).append(int(tick))
        for session, ticks in seen.items():
            self.assertEqual(ticks, sorted(ticks),
                             "session %d timeline not time-sorted" % session)
        self.assertEqual(sorted(seen), [1, 2, 3])
        # every source renders in the unified timeline
        for source in ("RF", "WIFI", "BLE", "NRF24", "GPS", "TX", "WATCH",
                       "RID", "FPV"):
            self.assertIn(">%s</span>" % source, self.index,
                          "source %s missing from timeline" % source)

    def test_detail_tokens_rendered(self):
        self.assertIn("min=-82 max=-44 avg=-61 band=2", self.index)

    # -- geojson ----------------------------------------------------------

    def test_geojson_structure_and_coordinate_order(self):
        path = os.path.join(self.out, "map-2.geojson")
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(data["type"], "FeatureCollection")
        self.assertIsInstance(data["features"], list)
        geometries = set()
        line_coords = None
        rid_roles = {}
        for feature in data["features"]:
            self.assertEqual(feature["type"], "Feature")
            geometry = feature["geometry"]
            geometries.add(geometry["type"])
            if geometry["type"] == "LineString":
                line_coords = geometry["coordinates"]
            elif geometry["type"] == "Point":
                lon, lat = geometry["coordinates"]
                self.assertIsInstance(lon, (int, float))
                self.assertIsInstance(lat, (int, float))
                self.assertTrue(-180 <= lon <= 180)
                self.assertTrue(-90 <= lat <= 90)
                props = feature["properties"]
                if props.get("kind") == "rid":
                    rid_roles[props["role"]] = props["ordinal"]
                    self.assertRegex(props["ordinal"], ORDINAL_RE)
            else:
                self.fail("unexpected geometry %s" % geometry["type"])
        self.assertIn("LineString", geometries)
        self.assertIn("Point", geometries)
        self.assertIsNotNone(line_coords)
        assert line_coords is not None
        # GeoJSON is [lon, lat]: fixture 2 path starts at lon -122.4194.
        self.assertAlmostEqual(line_coords[0][0], -122.4194, places=4)
        self.assertAlmostEqual(line_coords[0][1], 37.7749, places=4)
        self.assertEqual(rid_roles.get("drone"), "DR-01")
        self.assertEqual(rid_roles.get("operator"), "OP-01")

    def test_geojson_emitted_without_coords_too(self):
        path = os.path.join(self.out, "map-3.geojson")
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        self.assertEqual(data["type"], "FeatureCollection")
        self.assertEqual(data["features"], [])

    def test_geojson_never_carries_raw_identifiers(self):
        for name in os.listdir(self.out):
            if not name.endswith(".geojson"):
                continue
            text = read(os.path.join(self.out, name))
            self.assertIsNone(MAC_RE.search(text), name)
            self.assertNotIn(RAW_MAC, text)
            self.assertNotIn(RAW_SERIAL.lower(), text.lower())

    # -- wigle ---------------------------------------------------------------

    def test_wigle_column_order_and_gating(self):
        wigle = os.path.join(self.out, "wigle-2.csv")
        self.assertTrue(os.path.isfile(wigle),
                        "wigle-2.csv must exist for GPS-logged session")
        lines = read(wigle).splitlines()
        self.assertTrue(lines[0].startswith("WigleWifi-1.4,"))
        self.assertEqual(lines[1], WIGLE_COLUMNS)
        reader = csv.reader(lines[2:])
        rows = [row for row in reader if row]
        self.assertGreaterEqual(len(rows), 3)  # AP-01, AP-02, BL-01
        for row in rows:
            self.assertEqual(len(row), 11)
            self.assertRegex(row[0], ORDINAL_RE)  # MAC column = ordinal
            self.assertTrue(row[6] and row[7])    # lat/lon present
        # coordinate gating: no wigle export for coordinate-free sessions
        self.assertFalse(os.path.exists(os.path.join(self.out, "wigle-1.csv")))
        self.assertFalse(os.path.exists(os.path.join(self.out, "wigle-3.csv")))

    # -- privacy ----------------------------------------------------------

    def test_redaction_no_mac_or_serial_in_any_output(self):
        for name in sorted(os.listdir(self.out)):
            path = os.path.join(self.out, name)
            text = read(path)
            self.assertIsNone(MAC_RE.search(text),
                              "MAC-shaped string leaked into %s" % name)
            self.assertNotIn(RAW_MAC, text, name)
            self.assertNotIn(RAW_SERIAL, text, name)

    def test_watchlist_banner_absent_without_watchlist(self):
        self.assertNotIn("Opt-in cross-session identity", self.index)


class WatchlistBannerTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.mkdtemp(prefix="sweep_dash_watch_")
        self.sessions = os.path.join(self._tmp, "sessions")
        self.out = os.path.join(self._tmp, "out")
        build_fixture_sessions(self.sessions)
        with open(os.path.join(self.sessions, "watchlist.txt"), "w",
                  encoding="utf-8") as handle:
            handle.write("AP-01,lab printer\n")
            handle.write("%s,my phone\n" % RAW_MAC)
            handle.write("this line is garbage and gets skipped\n")
        self.proc = run_dashboard(self.sessions, self.out)

    def tearDown(self):
        shutil.rmtree(self._tmp, ignore_errors=True)

    def test_watchlist_renders_honesty_banner_and_labels(self):
        self.assertEqual(self.proc.returncode, 0, self.proc.stderr)
        index = read(os.path.join(self.out, "index.html"))
        self.assertIn("Opt-in cross-session identity", index)
        self.assertIn("lab printer", index)
        # labels render, raw keys never do
        self.assertNotIn(RAW_MAC, index)
        self.assertIsNone(MAC_RE.search(index))


class ToleranceTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.mkdtemp(prefix="sweep_dash_tol_")
        self.sessions = build_tolerance_dir(
            os.path.join(self._tmp, "sessions"))
        self.out = os.path.join(self._tmp, "out")
        self.proc = run_dashboard(self.sessions, self.out)

    def tearDown(self):
        shutil.rmtree(self._tmp, ignore_errors=True)

    def test_malformed_missing_rotated_files_tolerated(self):
        self.assertEqual(self.proc.returncode, 0, self.proc.stderr)
        self.assertEqual(self.proc.stderr, "")
        self.assertNotIn("Traceback", self.proc.stdout)
        self.assertTrue(os.path.isfile(os.path.join(self.out, "index.html")))
        self.assertTrue(os.path.isfile(os.path.join(self.out, "map-6.geojson")))
        # session 8 (empty) and session 7 (report only) render without maps
        index = read(os.path.join(self.out, "index.html"))
        self.assertIn("session 7", index)


class RoundTripRealEvidenceTest(unittest.TestCase):
    def test_roundtrip_real_session_csv(self):
        if not os.path.isdir(EVIDENCE):
            self.skipTest("no .omo/evidence/ dir on this machine")
        candidates = sorted(
            name for name in os.listdir(EVIDENCE)
            if re.search(r"session-\d+\.csv$", name))
        if not candidates:
            self.skipTest("no real session-*.csv in .omo/evidence/")
        out = tempfile.mkdtemp(prefix="sweep_dash_roundtrip_")
        try:
            before = hash_tree(EVIDENCE)
            proc = run_dashboard(EVIDENCE, out)
            after = hash_tree(EVIDENCE)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(proc.stderr, "",
                             "round-trip must be warning-free on stderr")
            self.assertEqual(before, after,
                             "real evidence must not be modified")
            self.assertTrue(os.path.isfile(os.path.join(out, "index.html")))
            self.assertIn("wrote:", proc.stdout)
        finally:
            shutil.rmtree(out, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)

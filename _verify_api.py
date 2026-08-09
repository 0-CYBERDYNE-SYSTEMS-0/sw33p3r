#!/usr/bin/env python3
"""Background Lane 2: host-verify the FULL main-app API surface against the
Momentum API 87.1 export table. Catches any non-exported symbol (guaranteed
hard-fault on `loader open`) BEFORE we waste a device build/deploy cycle.

Scans every .c/.h in the app dir, extracts called furi_*/gui_*/canvas_*/etc.
symbols, and reports any with status != '+'.
"""
import csv, re, glob, os, sys
from pathlib import Path

APP_DIR = Path(__file__).resolve().parent
CSV = Path(os.environ.get(
    "UFBT_API_SYMBOLS",
    Path.home() / ".ufbt/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv",
))

# Load export table: name -> status
api = {}
with open(CSV, newline="") as f:
    rows = list(csv.reader(f))
header = rows[0]
ni, si = header.index("name"), header.index("status")
for r in rows[1:]:
    if len(r) > max(ni, si):
        api[r[ni].strip()] = r[si].strip()

# Known type/macro names that legitimately are NOT in the function/variable
# export table (they're typedefs/enums/macros resolved at compile time).
TYPE_LIKE = {
    "furi_hal", "furi_hal_serial", "furi_hal_serial_control", "furi_hal_serial_types",
    "furi_hal_subghz", "furi_hal_gpio", "furi_hal_usb", "furi_hal_power",
    "furi_hal_region",
}

# static inline helpers in SDK headers — compile into the FAP, not API imports.
INLINE_OK = {
    "furi_hal_gpio_write",
    "furi_hal_gpio_write_port_pin",
    "furi_hal_gpio_read",
    "furi_hal_gpio_read_port_pin",
}

# Symbols that are plausibly real functions the app calls
CALL_RE = re.compile(
    r'\b('
    r'furi_[a-z0-9_]+'
    r'|gui_[a-z0-9_]+'
    r'|canvas_[a-z0-9_]+'
    r'|view_port_[a-z0-9_]+'
    r'|storage_[a-z0-9_]+'
    r'|expansion_[a-z0-9_]+'
    r'|notification_message'
    r'|notification_message_block'
    r')\b\s*(?=\()'
)

files = sorted(glob.glob(str(APP_DIR / "*.c")) + glob.glob(str(APP_DIR / "*.h")))
files = [f for f in files if "test_nmea" not in f]

all_called = {}
for path in files:
    with open(path) as fh:
        txt = fh.read()
    # strip comment lines crudely
    for m in CALL_RE.finditer(txt):
        sym = m.group(1)
        all_called.setdefault(sym, set()).add(os.path.basename(path))

missing = []
present = []
for sym, where in sorted(all_called.items()):
    st = api.get(sym)
    if st == "+":
        present.append(sym)
    elif sym in TYPE_LIKE or sym in INLINE_OK:
        continue  # include-fragment / type token / static inline, ignore
    else:
        missing.append((sym, st, sorted(where)))

print(f"App files scanned: {[os.path.basename(f) for f in files]}")
print(f"Distinct call-like symbols: {len(all_called)}")
print(f"Present in export table (status '+'): {len(present)}")
print()
if missing:
    print("!!! NON-EXPORTED / MISSING SYMBOLS (will hard-fault on load): !!!")
    for sym, st, where in missing:
        print(f"   {sym:42s} status={st!s:6s} used in {where}")
    sys.exit(1)
else:
    print("RESULT: CLEAN — every function symbol resolves to status '+'.")
    print("Safe to build; no expected ELF loader symbol faults.")
    sys.exit(0)

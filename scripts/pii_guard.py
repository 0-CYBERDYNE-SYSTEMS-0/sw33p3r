#!/usr/bin/env python3
"""PII guard — fail the build if tracked content carries real-world identifiers.

Catches the identifier classes that must never reach a public repo: personal
home paths, non-project email addresses, phone numbers, unredacted MAC/BSSID
values, real device names, and capture-style SSIDs that are not fixtures.

    python3 scripts/pii_guard.py              # tracked files at HEAD (CI gate)
    python3 scripts/pii_guard.py --history    # every blob in every ref (pre-release)
    python3 scripts/pii_guard.py --staged     # files staged for commit (pre-commit)

Exit 0 = clean, exit 1 = findings (printed with locations).

A synthetic fixture the guard still flags goes into scripts/pii_guard_allow.txt
(one ERE per line, ``#`` comments) — never paste a real value into that file,
and never weaken a check to make a real identifier pass.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

BINARY_EXT = (".png", ".jpg", ".jpeg", ".gif", ".pdf", ".fap", ".elf", ".bin", ".ico", ".pyc")

# ---------------------------------------------------------------- allowlists
# Octets that only appear in hand-written dummy addresses.
DUMMY_OCTETS = {"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b", "0c",
                "0d", "0e", "0f", "11", "12", "22", "23", "33", "34", "44", "45", "55", "56",
                "66", "67", "77", "78", "88", "89", "99", "9a", "aa", "ab", "bb", "bc",
                "cc", "cd", "dd", "de", "ad", "ee", "ef", "f0", "ff", "be", "5a", "5b", "58", "59", "57"}
# Real vendor prefixes used as synthetic OUI examples in tests/test_oui.c.
OUI_EXAMPLES = ("24:0a:c4", "b8:27:eb", "f0:18:98", "44:19:b6", "60:60:1f", "9c:3d:cf",
                "bc:24:11", "ac:22:0b", "f4:f5:e8", "4c:57:ca", "c3:11:22", "59:81:ec", "00:11:32",
                "fe:dc:ba", "ae:1b:2c", "2c:cf:67", "c8:4f:86:12", "f0:11:22", "40:11:22",
                "0a:11:22", "06:11:22", "0e:11:22", "99:88:77", "f8:ff:c2", "c8:4f:86:aa",
                "02:5a:9c")   # project redaction family: 02:5a:9c:xx:xx:xx
# Capture-record SSIDs that are fixtures or explicit redactions.
SSID_ALLOW = re.compile(r"^(?:<[a-z-]+>|[\w-]{1,2}|Name|NetworkName|NormalNetwork|SynthNet|FakeNet"
                        r"|Lab|My|Net|Test|Hidden/unknown|REDACTED.*|LAB_.*|SYNTH_.*|HOME_NET_.*"
                        r"|CAM_NET_.*|STA_NET_.*|AP_NET_.*|CLIENT_NET_.*|EVIL_TWIN_.*|.*….*)$", re.I)
# Device-name masks, e.g. flip_XXXX0 / flip_XXX001 (docs/flipper-agent-access.md uses flip_NAME).
DEVNAME_ALLOW = re.compile(r"^flip_(?:X+[A-Za-z0-9]*|NAME)$")
EMAIL_ALLOW = re.compile(r"^[^@]*@(?:users\.noreply\.github\.com|example\.(?:com|org|net))$")

MAC_RE = re.compile(r"\b[0-9a-fA-F]{2}([:-][0-9a-fA-F]{2}){5}\b")
SSID_RE = re.compile(r"""\bESSID\s*[:=]\s*([^\s.,;)\]"'`]+)""")
HOME_RE = re.compile(r"/Users/[A-Za-z0-9][\w.-]*|/home/[A-Za-z0-9][\w.-]*|[A-Za-z]:\\+Users\\?")
EMAIL_RE = re.compile(r"[\w.%+-]+@[\w.-]+\.[A-Za-z]{2,}")
PHONE_RE = re.compile(r"(?<![\d])(?:\(\d{3}\)|\d{3})[ .-]\d{3}[ .-]\d{4}(?!\d)(?!\.\d)")
DEVNAME_RE = re.compile(r"\bflip_[A-Za-z0-9_]+")


def mac_is_synthetic(value: str) -> bool:
    v = value.lower().replace("-", ":")
    octets = v.split(":")
    if all(o in DUMMY_OCTETS for o in octets):
        return True
    return any(v.startswith(p) for p in OUI_EXAMPLES)


def is_binary(path: str, data: bytes) -> bool:
    if path.lower().endswith(BINARY_EXT):
        return True
    if b"\x00" in data:
        return True
    sample = data[:4096]
    if not sample:
        return False
    nontext = sum(1 for b in sample if b < 9 or (13 < b < 32) or b > 126)
    return nontext / len(sample) > 0.10


def extra_allow() -> re.Pattern | None:
    path = "scripts/pii_guard_allow.txt"
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as fh:
        pats = [ln.strip() for ln in fh if ln.strip() and not ln.strip().startswith("#")]
    return re.compile("|".join(f"(?:{p})" for p in pats)) if pats else None


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], capture_output=True).stdout


def sources_head() -> list[tuple[str, bytes]]:
    out = []
    for path in git("ls-files").decode().splitlines():
        if path.startswith("scripts/pii_guard"):
            continue
        try:
            with open(path, "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        if not is_binary(path, data):
            out.append((path, data))
    return out


def sources_staged() -> list[tuple[str, bytes]]:
    out = []
    for path in git("diff", "--cached", "--name-only", "--diff-filter=ACM").decode().splitlines():
        if path.startswith("scripts/pii_guard"):
            continue
        data = git("show", f":{path}")
        if data and not is_binary(path, data):
            out.append((f"{path} (staged)", data))
    return out


def sources_history() -> list[tuple[str, bytes]]:
    blob_paths: dict[str, set[str]] = {}
    for line in git("rev-list", "--objects", "--all").decode().splitlines():
        sha, _, path = line.partition(" ")
        if path:
            blob_paths.setdefault(sha, set()).add(path)
    shas = list(blob_paths)
    proc = subprocess.run(["git", "cat-file", "--batch"], input="\n".join(shas).encode(),
                          capture_output=True)
    out, buf, pos = [], proc.stdout, 0
    for sha in shas:
        nl = buf.find(b"\n", pos)
        if nl < 0:
            break
        header = buf[pos:nl].decode(errors="replace").split()
        pos = nl + 1
        if len(header) != 3 or header[1] != "blob":
            continue
        size = int(header[2])
        data = buf[pos:pos + size]
        pos += size + 1
        paths = sorted(blob_paths[sha])
        label = paths[0] + (" (+%d more paths)" % (len(paths) - 1) if len(paths) > 1 else "")
        if label.startswith("scripts/pii_guard"):
            continue
        if any(p.startswith("docs/field_guide_shots/") for p in paths):
            continue
        if data and not is_binary(paths[0], data):
            out.append((label, data))
    return out


def scan(sources: list[tuple[str, bytes]], extra: re.Pattern | None) -> int:
    findings: dict[str, list[str]] = {}

    def record(check: str, path: str, lineno: int, value: str) -> None:
        if extra and extra.search(value):
            return
        findings.setdefault(check, []).append(f"{path}:{lineno}: {value.strip()[:100]}")

    for path, data in sources:
        for lineno, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            for m in HOME_RE.finditer(line):
                record("personal home paths", path, lineno, m.group(0))
            for m in EMAIL_RE.finditer(line):
                if not EMAIL_ALLOW.search(m.group(0)):
                    record("non-project email addresses", path, lineno, m.group(0))
            for m in PHONE_RE.finditer(line):
                record("phone numbers", path, lineno, m.group(0))
            for m in MAC_RE.finditer(line):
                if not mac_is_synthetic(m.group(0)):
                    record("unredacted MAC / BSSID values", path, lineno, m.group(0))
            for m in DEVNAME_RE.finditer(line):
                if not DEVNAME_ALLOW.match(m.group(0)):
                    record("real device names", path, lineno, m.group(0))
            for m in SSID_RE.finditer(line):
                # drop C-string escapes and trailing quote/backtick artifacts
                value = re.sub(r"\\[nrt]", "", m.group(1)).rstrip('"\'`\\')
                if not (SSID_ALLOW.match(value) or mac_is_synthetic(value)):
                    record("non-fixture capture SSIDs", path, lineno, value)

    if not findings:
        print("PII guard: clean")
        return 0
    total = 0
    for check, hits in findings.items():
        total += len(hits)
        print(f"\033[1;31mPII guard FAIL — {check}\033[0m")
        for hit in hits[:25]:
            print(f"    {hit}")
        if len(hits) > 25:
            print(f"    … {len(hits) - 25} more")
    print(f"\033[1;31mPII guard: {total} finding(s) — scrub before pushing\033[0m")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--history", action="store_true", help="scan every blob in every ref")
    group.add_argument("--staged", action="store_true", help="scan only staged changes")
    args = ap.parse_args()

    if args.history:
        sources, mode = sources_history(), "history"
    elif args.staged:
        sources, mode = sources_staged(), "staged"
    else:
        sources, mode = sources_head(), "HEAD"
    print(f"PII guard: scanning {len(sources)} file(s) ({mode})")
    return scan(sources, extra_allow())


if __name__ == "__main__":
    sys.exit(main())

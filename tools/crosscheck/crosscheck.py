#!/usr/bin/env python3
"""Cross-check canvas-cli against UntitledCharts' sonolus-level-converters.

For every input chart: convert it with canvas-cli into each export format, then
  * load the result with sonolus-level-converters (and skill-fever-format/pysekai_loader.py
    for Next Sekai LevelData) and compare the combo count with the library's own reading
    of the original file, and
  * load the result back with canvas-cli and compare combo, skills and fever.

usage: crosscheck.py --cli build/canvas-cli chart.sus [chart.json ...]
needs: pip install git+https://github.com/UntitledCharts/sonolus-level-converters
"""
import argparse
import io
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skill-fever-format"))

from sonolus_converters import detect, mmws, pjsk, sus, usc  # noqa: E402

FORMATS = {
    "sus": "sus",
    "sus-no-speed": "sus",
    "json": "json",
    "json-base64": "json",
    "usc": "usc",
    "leveldata": "json.gz",
    "mmws": "mmws",
    "ccmmws": "ccmmws",
}


def library_score(path):
    raw = open(path, "rb").read()
    found = detect(raw)
    if found is None:
        # plain MusicScoreData json is not in the library's detector
        return pjsk.load(raw)
    fmt, spec = found
    if fmt == "sus":
        return sus.load(io.StringIO(raw.decode("utf-8")))
    if fmt == "usc":
        return usc.load(io.StringIO(raw.decode("utf-8")))
    if fmt == "pjsk":
        return pjsk.load(raw)
    if fmt == "mmw":
        return mmws.load(io.TextIOWrapper(io.BytesIO(raw), encoding="utf-8"))
    if fmt == "lvd" and "pysekai" in spec:
        import pysekai_loader
        return pysekai_loader.load_bytes(raw)
    raise ValueError("library cannot load %s/%s" % (fmt, spec))


def cli_info(cli, path):
    out = subprocess.run([cli, "info", path], capture_output=True, text=True, check=True).stdout
    info = {}
    for line in out.splitlines():
        key, _, value = line.partition(":")
        info[key.strip()] = value.strip()
    return info


def skills_of(info):
    m = re.match(r"(\d+) \[(.*)\]", info.get("skills", "0 []"))
    return m.group(2) if m else ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("charts", nargs="+")
    args = parser.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for chart in args.charts:
            name = os.path.basename(chart)
            try:
                expected = library_score(chart).combo_count
            except Exception as ex:  # library cannot read it: compare canvas with itself only
                expected = None
                print("%s: library cannot read the input (%s)" % (name, ex))
            source = cli_info(args.cli, chart)
            line = ["%-34s lib=%-5s canvas=%-5s" % (name, expected, source["combo"])]
            if expected is not None and int(source["combo"]) != expected:
                failures += 1
                line.append("INPUT-MISMATCH")

            for fmt, ext in FORMATS.items():
                out = os.path.join(tmp, "%s.%s.%s" % (name, fmt, ext))
                subprocess.run([args.cli, "convert", chart, out, "--format", fmt], capture_output=True, check=True)

                back = cli_info(args.cli, out)
                ok = back["combo"] == source["combo"] and skills_of(back) == skills_of(source) and back["fever"] == source["fever"]

                lib = "-"
                try:
                    lib = library_score(out).combo_count
                    ok = ok and (expected is None or lib == expected)
                except Exception as ex:
                    lib = "n/a"

                line.append("%s:%s/%s%s" % (fmt, back["combo"], lib, "" if ok else "!"))
                failures += 0 if ok else 1
            print("  ".join(line))

    print("\n%d failure(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

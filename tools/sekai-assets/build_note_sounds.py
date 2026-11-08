#!/usr/bin/env python3
"""
Builds Canvas Editor's note sounds and live effect sounds from OpenSekai's CRI sound banks:

    res/sound/01   OpenSekai's default note sounds (startapp/live/sound/live_se/default)
    res/sound/02   OpenSekai custom 1  (startapp/live/tap_se/custom01)
    res/sound/03   OpenSekai custom 2  (startapp/live/tap_se/custom02)
    res/sound/live/  skill, FEVER CHANCE, FEVER, ALL PERFECT and card cut-in sounds (se_live_common)

The sets MikuMikuWorld shipped move to res/sound/04 and 05 the first time ("Classic"). Each new
sound is levelled to the classic sound of the same name so the mix stays as it was, and saved
as FLAC (lossless, and no MP3 start delay on hit sounds).

    python build_note_sounds.py <OpenSekai root>

Needs the cricodecs package (pip install cricodecs) and ffmpeg.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

import cricodecs

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "MikuMikuWorld", "res")
NAMES = ["se_live_perfect", "se_live_flick", "se_live_connect", "se_live_trace", "se_live_long", "se_live_critical",
         "se_live_flick_critical", "se_live_connect_critical", "se_live_trace_critical", "se_live_long_critical",
         "se_live_tap", "se_live_good", "se_live_great"]
EFFECTS = {"SE_LIVE_SKILL": "skill", "SE_LIVE_FEVER_CHANCE": "fever_chance", "SE_LIVE_FEVER": "fever",
           "se_live_all_perfect": "all_perfect", "SE_LIVE_CUTIN_A": "cutin"}


def extract(acb_path, out):
    """every waveform of the bank as <name>.wav (the first one of a name wins)"""
    acb = cricodecs.acb.load(acb_path)
    os.makedirs(out, exist_ok=True)
    found = {}
    for i in range(acb.waveform_count):
        name = re.sub(r"^\d+_", "", os.path.splitext(os.path.basename(acb.waveform_filename(i)))[0])
        if name in found:
            continue
        path = os.path.join(out, name + ".wav")
        with open(path, "wb") as f:
            f.write(cricodecs.hca.decode(acb.waveform_bytes(i)))
        found[name] = path
    return found


def volume(path):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-i", path, "-af", "volumedetect", "-f", "null", "-"], capture_output=True, text=True)
    mean = re.search(r"mean_volume: (-?[\d.]+) dB", r.stderr)
    peak = re.search(r"max_volume: (-?[\d.]+) dB", r.stderr)
    return float(mean.group(1)), float(peak.group(1))


def classic(name):
    for folder in ("04", "05"):
        for ext in (".mp3", ".flac", ".wav"):
            p = os.path.join(RES, "sound", folder, name + ext)
            if os.path.exists(p):
                return p
    return None


def encode(src, dst, gain_db):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-af", "volume=%.2fdB" % gain_db, "-ar", "44100", "-ac", "2",
                    "-c:a", "flac", dst], check=True)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    live = os.path.join(sys.argv[1], "Assets", "Sekai", "assetbundle", "resources", "startapp", "live")
    tmp = tempfile.mkdtemp()

    # the classic sets keep their place as 04 / 05
    for old, new in (("01", "04"), ("02", "05")):
        o, n = os.path.join(RES, "sound", old), os.path.join(RES, "sound", new)
        if os.path.isdir(o) and not os.path.isdir(n) and any(f.endswith(".mp3") for f in os.listdir(o)):
            shutil.move(o, n)

    banks = {
        "01": os.path.join(live, "sound", "live_se", "default", "se_live_default.acb.bytes"),
        "02": os.path.join(live, "tap_se", "custom01", "custom01.acb.bytes"),
        "03": os.path.join(live, "tap_se", "custom02", "custom02.acb.bytes"),
    }
    sets = {k: extract(v, os.path.join(tmp, k)) for k, v in banks.items()}
    for folder, sounds in sets.items():
        out = os.path.join(RES, "sound", folder)
        if os.path.isdir(out):
            shutil.rmtree(out)
        for name in NAMES:
            # the default bank has no critical trace: custom 2's is the same family
            src = sounds.get(name) or sets["03"].get(name) or sets["02"].get(name)
            if not src:
                print(folder, "missing", name)
                continue
            gain = 0.0
            ref = classic(name)
            mean, peak = volume(src)
            if ref:
                gain = volume(ref)[0] - mean
            gain = min(gain, -1.0 - peak)	# never clip
            encode(src, os.path.join(out, name + ".flac"), gain)
        print("sound/" + folder, len(os.listdir(out)), "sounds")

    common = extract(os.path.join(live, "sound", "live_se", "default", "se_live_common.acb.bytes"), os.path.join(tmp, "common"))
    out = os.path.join(RES, "sound", "live")
    os.makedirs(out, exist_ok=True)
    for name, short in EFFECTS.items():
        if name in common:
            mean, peak = volume(common[name])
            encode(common[name], os.path.join(out, short + ".flac"), min(0.0, -1.0 - peak))
    print("sound/live", sorted(os.listdir(out)))


if __name__ == "__main__":
    main()

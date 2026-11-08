"""
Skill + Fever for Project SEKAI charts — reference implementation.

Two things this file does:
  1. read/write the three .sus encodings for skill / fever-chance / fever-start
  2. read/write the MusicScoreData JSON extension key "CanvasSkillFever"
and a generator that fills in official-style positions when a chart has none.

Pure standard library. See README.txt for the format writeup.
"""
from __future__ import annotations
import json
import re

TICKS_PER_BEAT = 480

# .sus tap-channel (1) encodings
SKILL = "41"          # #mmm10:41  -> lane 0, type 4
FEVER_CHANCE = "11"   # #mmm1f:11  -> lane f, type 1
FEVER_START = "21"    # #mmm1f:21  -> lane f, type 2

# median positions across 43 official expert charts, as a fraction of the note span
DEFAULT_SKILLS = (0.112, 0.243, 0.370, 0.534, 0.703, 0.862)
DEFAULT_FEVER_CHANCE = 0.556
DEFAULT_FEVER_START = 0.676

EXT_KEY = "CanvasSkillFever"

_DATA = re.compile(r"^#(\d{3})([0-9a-zA-Z])([0-9a-zA-Z]{1,2})\s*:\s*(\S*)\s*$")
_BASE = re.compile(r"^#MEASUREBS\s+(\d+)\s*$", re.IGNORECASE)
_BAR = re.compile(r"^#(\d{3})02\s*:\s*([0-9.]+)\s*$")


# --------------------------------------------------------------------------- #
# JSON extension key (MusicScoreData)
# --------------------------------------------------------------------------- #
def read_json_skill_fever(musicscore: dict) -> dict:
    """-> {'skills': [tick,...], 'feverChance': tick|None, 'feverStart': tick|None}."""
    ext = musicscore.get(EXT_KEY) or {}
    return {
        "skills": list(ext.get("skills") or []),
        "feverChance": ext.get("feverChance"),
        "feverStart": ext.get("feverStart"),
    }


def write_json_skill_fever(musicscore: dict, skills, fever_chance, fever_start) -> dict:
    """Attach the extension key. skills = list of ticks; fever_* = tick or None."""
    if skills or fever_chance is not None or fever_start is not None:
        musicscore[EXT_KEY] = {
            "skills": sorted(int(t) for t in skills),
            "feverChance": int(fever_chance) if fever_chance is not None else None,
            "feverStart": int(fever_start) if fever_start is not None else None,
        }
    return musicscore


# --------------------------------------------------------------------------- #
# generator: official-style positions from a note span
# --------------------------------------------------------------------------- #
def generate(first_tick: int, last_tick: int, snap_ticks: int = TICKS_PER_BEAT * 4):
    """Six skills + fever chance/start at the official medians of [first,last].
    Snapped to snap_ticks (default one 4/4 measure). Returns (skills, chance, start)."""
    span = max(1, last_tick - first_tick)

    def at(frac):
        t = first_tick + round(span * frac)
        return round(t / snap_ticks) * snap_ticks

    return ([at(f) for f in DEFAULT_SKILLS], at(DEFAULT_FEVER_CHANCE), at(DEFAULT_FEVER_START))


# --------------------------------------------------------------------------- #
# .sus helpers
# --------------------------------------------------------------------------- #
def read_sus_skill_fever(sus_text: str) -> dict:
    """Scan a .sus for the three markers. Returns tick lists keyed skills/feverChance/feverStart."""
    base, out = 0, {"skills": [], "feverChance": [], "feverStart": []}
    bar_len = {}
    for raw in sus_text.splitlines():
        line = raw.strip()
        b = _BASE.match(line)
        if b:
            base = int(b.group(1))
            continue
        bl = _BAR.match(line)
        if bl:
            bar_len[int(bl.group(1))] = float(bl.group(2))
            continue
        m = _DATA.match(line)
        if not m:
            continue
        measure = base + int(m.group(1))
        chan, lane, data = m.group(2), m.group(3).lower(), m.group(4)
        if chan != "1":
            continue
        pairs = [data[i:i + 2] for i in range(0, len(data), 2)]
        n = len(pairs) or 1
        for i, cell in enumerate(pairs):
            if cell == "00":
                continue
            tick = measure * 4 * TICKS_PER_BEAT + round(i / n * 4 * TICKS_PER_BEAT)
            if lane == "0" and cell == SKILL:
                out["skills"].append(tick)
            elif lane == "f" and cell == FEVER_CHANCE:
                out["feverChance"].append(tick)
            elif lane == "f" and cell == FEVER_START:
                out["feverStart"].append(tick)
    return out


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 2 and sys.argv[1].endswith(".json"):
        ms = json.load(open(sys.argv[1], encoding="utf-8"))
        print(read_json_skill_fever(ms))
    elif len(sys.argv) == 2 and sys.argv[1].endswith(".sus"):
        print(read_sus_skill_fever(open(sys.argv[1], encoding="utf-8").read()))
    else:
        # demo: a 100-measure chart's default skill/fever positions
        skills, fc, fs = generate(0, 100 * 4 * TICKS_PER_BEAT)
        print("skills:", skills, "feverChance:", fc, "feverStart:", fs)

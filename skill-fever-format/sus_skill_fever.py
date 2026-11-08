"""skill and fever markers in project sekai .sus -- reading them, and putting them in when a chart
arrives without any.

how the game encodes them, read straight off official charts:

    #mmm10:41    tap channel, lane 0, type 4   -> a skill trigger
    #mmm1f:11    tap channel, lane f, type 1   -> fever chance starts
    #mmm1f:21    tap channel, lane f, type 2   -> fever starts

every official chart has exactly six skills. we measured 43 expert charts and the six land at
roughly the same fractions of the song every time, with fever chance and fever start between the
fourth and fifth -- those medians are DEFAULT_* below.

why charts show up without them: the in-game Chart Maker's JSON has nowhere to put skills or
fever (its event enum is BPM / HighSpeed / SeVolume / TimeSignature and nothing else). the game
strips them on purpose (FilterOutSkillFeverEvents) and merges them back at play time from the
song's OFFICIAL chart (BuildMergedSkillFeverEvents). a brand new custom song has no official chart
to merge from, so it plays with no skills at all unless we put them in. (the sonolus-converters
library itself is fine -- it reads, writes and round-trips all three. charts arrive without them
only because the format they came from never had any.)

no dependencies on purpose: this file gets copied into HolodoriToSekai as well.
"""

from __future__ import annotations

import re
from fractions import Fraction

SKILL = "41"
FEVER_CHANCE = "11"
FEVER_START = "21"

DEFAULT_SKILLS = (0.112, 0.243, 0.370, 0.534, 0.703, 0.862)
DEFAULT_FEVER_CHANCE = 0.556
DEFAULT_FEVER_START = 0.676

_DATA = re.compile(r"^#(\d{3})([0-9a-zA-Z])([0-9a-zA-Z]{1,2})\s*:\s*(\S*)\s*$")
_BASE = re.compile(r"^#MEASUREBS\s+(\d+)\s*$", re.IGNORECASE)
_BAR = re.compile(r"^#(\d{3})02\s*:\s*([0-9.]+)\s*$")

# note-bearing channels: 1 taps, 3 long/slide, 5 directional, 9 guide. 2-channel is bar length
# and 08 is BPM, neither of which is a note.
_NOTE_CHANNELS = set("1359")


def _lines(text: str):
    """(absolute measure, channel, lane, data) for every data line, with MEASUREBS applied."""
    base = 0
    for raw in text.splitlines():
        line = raw.strip()
        m = _BASE.match(line)
        if m:
            base = int(m.group(1))
            continue
        m = _DATA.match(line)
        if m:
            yield base + int(m.group(1)), m.group(2), m.group(3).lower(), m.group(4)


def markers(text: str) -> dict:
    """how many skill / fever-chance / fever-start markers the chart already has."""
    out = {"skill": 0, "fever_chance": 0, "fever_start": 0}
    for _meas, ch, lane, data in _lines(text):
        if ch != "1" or lane not in ("0", "f"):
            continue
        for i in range(0, len(data) - 1, 2):
            tok = data[i:i + 2]
            if lane == "0" and tok == SKILL:
                out["skill"] += 1
            elif lane == "f" and tok == FEVER_CHANCE:
                out["fever_chance"] += 1
            elif lane == "f" and tok == FEVER_START:
                out["fever_start"] += 1
    return out


def has_markers(text: str) -> bool:
    m = markers(text)
    return m["skill"] > 0 or m["fever_chance"] > 0 or m["fever_start"] > 0


def _bar_lengths(text: str) -> dict:
    """measure -> beats per measure, for every #mmm02 line (MEASUREBS respected)."""
    out, base = {}, 0
    for raw in text.splitlines():
        line = raw.strip()
        m = _BASE.match(line)
        if m:
            base = int(m.group(1))
            continue
        m = _BAR.match(line)
        if m:
            out[base + int(m.group(1))] = Fraction(m.group(2)).limit_denominator(1000)
    return out


class _Timeline:
    """measure <-> beat, honouring time-signature changes."""

    def __init__(self, text: str, last_measure: int):
        bars = _bar_lengths(text)
        self.starts = [Fraction(0)]
        length = bars.get(0, Fraction(4))
        self.lengths = []
        for meas in range(last_measure + 2):
            length = bars.get(meas, length)
            self.lengths.append(length)
            self.starts.append(self.starts[-1] + length)

    def to_beat(self, measure: int, frac: Fraction) -> Fraction:
        measure = min(measure, len(self.lengths) - 1)
        return self.starts[measure] + frac * self.lengths[measure]

    def to_pos(self, beat: Fraction) -> tuple[int, Fraction]:
        for meas in range(len(self.lengths)):
            if beat < self.starts[meas + 1]:
                return meas, (beat - self.starts[meas]) / self.lengths[meas]
        last = len(self.lengths) - 1
        return last, Fraction(0)


def _note_beats(text: str) -> tuple[list, int]:
    positions, last = [], 0
    for meas, ch, lane, data in _lines(text):
        if ch not in _NOTE_CHANNELS:
            continue
        if ch == "1" and lane in ("0", "f"):
            continue  # those are the markers themselves
        n = len(data) // 2
        for i in range(n):
            if data[2 * i:2 * i + 2] != "00":
                positions.append((meas, Fraction(i, n)))
                last = max(last, meas)
    return positions, last


def _emit(events, timeline: _Timeline) -> list:
    """(kind, beat) -> '#mmmCL:data' lines, grouped per measure and lane so two markers in one
    measure share a line like the official files do."""
    by_line: dict = {}
    for kind, beat in events:
        meas, frac = timeline.to_pos(Fraction(beat))
        lane = "0" if kind == "skill" else "f"
        token = {"skill": SKILL, "fever_chance": FEVER_CHANCE, "fever_start": FEVER_START}[kind]
        by_line.setdefault((meas, lane), []).append((frac.limit_denominator(192), token))
    out = []
    current_base = None
    for (meas, lane) in sorted(by_line):
        base = (meas // 1000) * 1000
        if base != current_base:
            out.append(f"#MEASUREBS {base}")
            current_base = base
        slots = by_line[(meas, lane)]
        den = 1
        for frac, _tok in slots:
            den = den * frac.denominator // _gcd(den, frac.denominator)
        cells = ["00"] * den
        for frac, tok in slots:
            cells[int(frac * den)] = tok
        out.append(f"#{meas - base:03d}1{lane}:{''.join(cells)}")
    if current_base not in (None, 0):
        out.append("#MEASUREBS 0")
    return out


def _gcd(a: int, b: int) -> int:
    while b:
        a, b = b, a % b
    return a


def default_events(text: str) -> list:
    """six skills + fever chance + fever start where official charts put them, each snapped to
    the start of a measure (which is where the official ones sit too). [] if there are no notes."""
    positions, last = _note_beats(text)
    if not positions:
        return []
    tl = _Timeline(text, last)
    beats = sorted(tl.to_beat(m, f) for m, f in positions)
    first, end = beats[0], beats[-1]
    span = end - first
    if span <= 0:
        return []

    def measure_start(fraction: float) -> Fraction:
        meas, _ = tl.to_pos(first + span * Fraction(fraction).limit_denominator(1000))
        return tl.starts[meas]

    events, used = [], set()
    for f in DEFAULT_SKILLS:
        b = measure_start(f)
        while b in used and b < tl.starts[-1]:          # short chart: two skills in one bar
            b += Fraction(1)                             # -> push the later one a beat on
        used.add(b)
        events.append(("skill", b))
    chance = measure_start(DEFAULT_FEVER_CHANCE)
    start = measure_start(DEFAULT_FEVER_START)
    if start <= chance:
        start = chance + tl.lengths[tl.to_pos(chance)[0]]
    events += [("fever_chance", chance), ("fever_start", start)]
    return events


def inject(text: str, events) -> str:
    """append marker lines for (kind, beat) events. the original text above is left untouched."""
    if not events:
        return text
    _positions, last = _note_beats(text)
    last = max([last] + [0])
    tl = _Timeline(text, last + 64)
    lines = _emit(events, tl)
    tail = "\n".join(["", "# skill / fever markers added by Canvas Editor", *lines, ""])
    return text.rstrip("\n") + "\n" + tail


def ensure(text: str) -> tuple[str, bool]:
    """a chart with no skill or fever markers at all gets the official-style defaults.
    returns (text, whether anything was added). charts that already have ANY marker are
    returned byte for byte -- a charter who placed their own is never second-guessed."""
    if has_markers(text):
        return text, False
    events = default_events(text)
    if not events:
        return text, False
    return inject(text, events), True


HARDEST_FIRST = ("append", "master", "expert", "hard", "normal", "easy")


def existing_events(text: str) -> list:
    """(kind, beat) for the markers a chart already has, so they can be copied to its siblings."""
    positions, last = _note_beats(text)
    tl = _Timeline(text, max([last] + [m for m, *_ in _lines(text)]) + 1)
    out = []
    for meas, ch, lane, data in _lines(text):
        if ch != "1" or lane not in ("0", "f"):
            continue
        n = len(data) // 2
        for i in range(n):
            tok = data[2 * i:2 * i + 2]
            kind = {("0", SKILL): "skill", ("f", FEVER_CHANCE): "fever_chance",
                    ("f", FEVER_START): "fever_start"}.get((lane, tok))
            if kind:
                out.append((kind, tl.to_beat(meas, Fraction(i, n))))
    return out


def ensure_song(charts: dict) -> tuple[dict, list]:
    """the same, but for every difficulty of one song at once -- and that matters: official songs
    put skills and fever at the SAME place on every difficulty, and fever is shared in multi live,
    so per-chart defaults could put players on different difficulties out of step.

    charts = {difficulty: sus text}. difficulties that already have markers are untouched; the rest
    get the markers of whichever difficulty the charter DID place them in, or else defaults
    computed once from the hardest chart. returns (charts, difficulties that were filled in)."""
    missing = [d for d, t in charts.items() if not has_markers(t)]
    if not missing:
        return charts, []
    order = sorted(charts, key=lambda d: HARDEST_FIRST.index(d) if d in HARDEST_FIRST else 99)
    donor = next((d for d in order if has_markers(charts[d])), None)
    events = existing_events(charts[donor]) if donor else default_events(charts[order[0]])
    if not events:
        return charts, []
    out = dict(charts)
    for d in missing:
        out[d] = inject(charts[d], events)
    return out, missing


def events_from_score(score) -> list:
    """(kind, beat) for the sekai Skill / FeverChance / FeverStart events a sonolus-converters
    Score carries."""
    out = []
    for note in getattr(score, "notes", []):
        name = type(note).__name__
        beat = getattr(note, "beat", None)
        if beat is None:
            continue
        if name == "Skill":
            out.append(("skill", Fraction(beat).limit_denominator(1920)))
        elif name == "FeverChance":
            out.append(("fever_chance", Fraction(beat).limit_denominator(1920)))
        elif name == "FeverStart":
            out.append(("fever_start", Fraction(beat).limit_denominator(1920)))
    return out


def extract_and_strip(text: str) -> tuple[str, list]:
    """(text with skill/fever lines zeroed out, [(kind, beat)] of what was there).

    For handing a chart to a loader that does not know channel-1 lane-0/lane-f are reserved --
    a Holodori .sus with Sekai-style markers embedded is the case this exists for (see the
    module-level note above). Only the matching 2-char tokens are cleared, not the whole line,
    in case a line ever legitimately mixes marker and non-marker cells; a line left all-"00"
    is dropped rather than kept as a dead placeholder.
    """
    events = existing_events(text)
    if not events:
        return text, []
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        dm = _DATA.match(line)
        if dm and dm.group(2) == "1" and dm.group(3).lower() in ("0", "f"):
            lane = dm.group(3).lower()
            data = dm.group(4)
            wanted = {SKILL} if lane == "0" else {FEVER_CHANCE, FEVER_START}
            toks = [data[i:i + 2] for i in range(0, len(data) - 1, 2)]
            if any(t in wanted for t in toks):
                cleaned = "".join("00" if t in wanted else t for t in toks)
                if cleaned.strip("0"):
                    out.append(f"#{dm.group(1)}{dm.group(2)}{lane}:{cleaned}")
                continue
        out.append(raw)
    return "\n".join(out), events

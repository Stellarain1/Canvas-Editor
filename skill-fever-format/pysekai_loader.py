"""next sekai / pysekai LevelData -> sonolus Score. what untitledcharts serves and ChartDownloader
saves as NSLevelData.json(.gz).

sonolus-level-converters only goes the other way (its next_sekai.load just raises), so this is
that exporter read backwards: same archetype names, same ease/direction/colour codes. slides are
walked through each note's `next` ref from the head -- the Connector and SimLine entities are
only there for drawing, and TransientHiddenTickNote rows that AREN'T in a `next` chain are the
engine's own half-beat combo ticks, which sekai regenerates by itself, so both get skipped.

replaces scripts/nslevel_to_sus.py (our first port, marked "validate before bulk use", which
never learned per-note hi-speed groups and is what the #number charts were built with).
"""
import gzip
import io
import json

from sonolus_converters.notes.score import Score
from sonolus_converters.notes.metadata import MetaData
from sonolus_converters.notes.bpm import Bpm
from sonolus_converters.notes.timescale import TimeScaleGroup, TimeScalePoint
from sonolus_converters.notes.single import Single, Skill, FeverChance, FeverStart
from sonolus_converters.notes.slide import Slide, SlideStartPoint, SlideRelayPoint, SlideEndPoint
from sonolus_converters.notes.guide import Guide, GuidePoint

EASES = {5: "outin", 3: "out", 1: "linear", 2: "in", 4: "inout"}
DIRECTIONS = {1: "left", 0: "up", 2: "right"}
GUIDE_COLORS = {101: "neutral", 102: "red", 103: "green", 104: "blue", 105: "yellow",
                106: "purple", 107: "cyan", 108: "black"}


def _read(raw: bytes) -> dict:
    try:
        raw = gzip.decompress(raw)
    except OSError:
        pass
    return json.loads(raw.decode("utf-8"))


def load_bytes(raw: bytes) -> Score:
    level = _read(raw)
    ents = level["entities"]
    by_name = {}
    for e in ents:
        d = {}
        for kv in e.get("data", []):
            d[kv["name"]] = kv.get("ref", kv.get("value"))
        e["_d"] = d
        if "name" in e:
            by_name[e["name"]] = e

    notes = []

    # hi-speed groups, in the order they appear (that's their index, same as the exporter)
    group_idx = {}
    for e in ents:
        if e["archetype"] == "#TIMESCALE_GROUP":
            # nameless groups exist (nothing can ref them); still count them so indexes line up
            group_idx[e.get("name") or f"_anon{id(e)}"] = len(group_idx)
            tsg = TimeScaleGroup()
            cur = by_name.get(e["_d"].get("first"))
            guard = 0
            while cur is not None and guard < 100000:
                tsg.append(TimeScalePoint(beat=float(cur["_d"].get("#BEAT", 0)),
                                          timeScale=float(cur["_d"].get("#TIMESCALE", 1))))
                cur = by_name.get(cur["_d"].get("next"))
                guard += 1
            if not tsg.changes:
                tsg.append(TimeScalePoint(beat=0.0, timeScale=1.0))
            notes.append(tsg)
    if not group_idx:
        notes.append(TimeScaleGroup(changes=[TimeScalePoint(beat=0.0, timeScale=1.0)]))

    def g(d):
        return group_idx.get(d.get("#TIMESCALE_GROUP"), 0)

    for e in ents:
        a, d = e["archetype"], e["_d"]
        if a == "#BPM_CHANGE":
            notes.append(Bpm(beat=float(d.get("#BEAT", 0)), bpm=float(d.get("#BPM", 120))))
        elif a == "Skill":
            notes.append(Skill(beat=float(d["#BEAT"])))
        elif a == "FeverChance":
            notes.append(FeverChance(beat=float(d["#BEAT"])))
        elif a == "FeverStart":
            notes.append(FeverStart(beat=float(d["#BEAT"])))

    # singles: note archetypes with no activeHead (anything that belongs to a slide carries one)
    for e in ents:
        a, d = e["archetype"], e["_d"]
        if not a.endswith("Note") or "activeHead" in d or a in ("AnchorNote", "TransientHiddenTickNote"):
            continue
        if any(k in a for k in ("Head", "Tail", "Tick")):
            continue
        fake = a.startswith("Fake")
        core = a[4:] if fake else a
        damage = core.startswith("Damage")
        crit = core.startswith("Critical")
        body = core.replace("Critical", "", 1).replace("Normal", "", 1)
        trace = body.startswith("Trace")
        flick = "Flick" in body
        notes.append(Single(
            beat=float(d["#BEAT"]), critical=crit, lane=float(d["lane"]), size=float(d["size"]),
            fake=fake, timeScaleGroup=g(d), trace=trace,
            direction=DIRECTIONS.get(int(d.get("direction", 0))) if flick else None,
            type="damage" if damage else "single"))

    # slides: every head entity (activeHead points at itself), walked through `next`
    referenced = {e["_d"].get("next") for e in ents if e["_d"].get("next")}
    for e in ents:
        d = e["_d"]
        if not e.get("name") or d.get("activeHead") != e.get("name") or e["name"] in referenced:
            continue
        chain, cur, guard = [], e, 0
        while cur is not None and guard < 100000:
            chain.append(cur)
            cur = by_name.get(cur["_d"].get("next"))
            guard += 1
        if len(chain) < 2:
            continue
        kind = int(d.get("segmentKind", 1))
        slide = Slide(critical=(kind % 50) == 2, fake=kind >= 50 or e["archetype"].startswith("Fake"))
        last = len(chain) - 1
        for i, n in enumerate(chain):
            a, nd = n["archetype"], n["_d"]
            a = a[4:] if a.startswith("Fake") else a
            crit = a.startswith("Critical")
            base = dict(beat=float(nd["#BEAT"]), lane=float(nd["lane"]), size=float(nd["size"]),
                        timeScaleGroup=g(nd))
            ease = EASES.get(int(nd.get("connectorEase", 1)), "linear")
            if i == 0:
                jt = "none" if a.startswith("Anchor") else ("trace" if "Trace" in a else "normal")
                slide.append(SlideStartPoint(critical=crit if jt != "none" else slide.critical,
                                             ease=ease, judgeType=jt, **base))
            elif i == last:
                jt = "none" if a.startswith("Anchor") else ("trace" if "Trace" in a else "normal")
                direction = DIRECTIONS.get(int(nd.get("direction", 0))) if "Flick" in a else None
                slide.append(SlideEndPoint(critical=crit if jt != "none" else slide.critical,
                                           judgeType=jt, direction=direction, **base))
            else:
                attach = int(nd.get("isAttached", 0)) == 1
                if a.startswith("Anchor") or a.startswith("TransientHiddenTick"):
                    rc = None
                else:
                    rc = crit
                slide.append(SlideRelayPoint(ease=ease, type="attach" if attach else "tick",
                                             critical=rc, **base))
        notes.append(slide)

    # guides: AnchorNote chains with no activeHead, coloured by segmentKind 101-108
    for e in ents:
        d = e["_d"]
        if e["archetype"] != "AnchorNote" or not e.get("name") or "activeHead" in d or e["name"] in referenced:
            continue
        chain, cur, guard = [], e, 0
        while cur is not None and guard < 100000:
            chain.append(cur)
            cur = by_name.get(cur["_d"].get("next"))
            guard += 1
        if len(chain) < 2:
            continue
        kind = int(d.get("segmentKind", 103))
        head_a = float(chain[0]["_d"].get("segmentAlpha", 1))
        tail_a = float(chain[-1]["_d"].get("segmentAlpha", 1))
        fade = "in" if head_a < tail_a else ("out" if tail_a < head_a else "none")
        guide = Guide(color=GUIDE_COLORS.get(kind, "green"), fade=fade)
        for n in chain:
            nd = n["_d"]
            guide.append(GuidePoint(beat=float(nd["#BEAT"]),
                                    ease=EASES.get(int(nd.get("connectorEase", 1)), "linear"),
                                    lane=float(nd["lane"]), size=float(nd["size"]), timeScaleGroup=g(nd)))
        notes.append(guide)

    meta = MetaData(title="", artist="", designer="", waveoffset=float(level.get("bgmOffset") or 0),
                    requests=None)
    return Score(meta, notes)


def load(fp) -> Score:
    return load_bytes(fp.read())

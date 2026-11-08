"""One way in for every chart format we accept, out as .sus text.

The score assetbundle carries a .sus TextAsset -- that is what the client parses -- so
whatever a charter hands us has to end up as .sus eventually. This is the single place that
happens, so the folder scanner and the panel's upload forms cannot drift apart on which
formats they take.

Everything is delegated to UntitledCharts' sonolus-level-converters
(https://github.com/UntitledCharts/sonolus-level-converters), which is a much wider and
better-maintained reader than the hand-rolled scripts/musicscore_to_sus.py it replaces.

Accepted, all auto-detected from the bytes -- the caller never has to say which is which:
  .sus                    passed through untouched
  .json  MusicScoreData   plain, raw gzip, or base64(gzip(...)) -- the game's own compiled
                          chart, which is the format the panel now prefers over .sus
  .usc                    MMW4CC
  .mmws/.ccmmws/.unchmmws
  LevelData               Chart Cyanvas / Next Sekai / Untitled Sekai, plain or gzipped
  holodori .sus           hololive Dreams charts, via from_holodori()

what the 6.8.1 client actually reads, pulled out of its own regexes (Sekai.SUS.Converter), and
why the export flags below are what they are:
  - per-note speed ratios ARE supported ("xy,1.5" cells, NotesSpeedRatioRegex). the library strips
    them by default, which is what made score-maker charts scroll wrong -- a real draft had 430
    notes with their own speed. keep_note_speed_ratios=True now.
  - hi-speed is read from ONE line only, the literal "#TIL00: ". extra layers (#TIL01, #HISPEED
    switches) are silently ignored in-game, so multi-layer charts get their layers merged here
    instead of shipping lines the game drops. see _merge_layers.
  - note/lane cells are lowercase only ([0-9a-z]); the exporter already writes lowercase.
  - two notes on the same lane at the same instant collapse into one (the parser keys notes by
    time + lane). the exporter's shift() nudges them a tick apart, which is as close as sus gets.

skills + fever: MusicScoreData has no event type for them (the enum is BPM / HighSpeed /
SeVolume / TimeSignature and that's it). so we carry them in our own top-level key,
"CanvasSkillFever", which the game's json reader ignores and we read back here. a json
without it still gets official-style ones from build_score, so nothing ends up skill-less.
"""

import io
import json
import gzip
import base64

__all__ = ["to_sus_text", "looks_like_chart", "SUPPORTED", "combo_count", "load_score",
           "score_to_musicscore_json", "from_holodori", "LAST_WARNINGS"]

SUPPORTED = ".sus, .json (MusicScoreData), .usc, .mmws/.ccmmws/.unchmmws, LevelData, holodori .sus"
TICKS_PER_BEAT = 480
EXT_KEY = "CanvasSkillFever"

# whatever the last conversion had to say (merged layers, dropped duplicates...). builders log it.
LAST_WARNINGS: list = []


def _as_bytes(raw):
    if isinstance(raw, str):
        return raw.encode("utf-8")
    if isinstance(raw, memoryview):
        return raw.tobytes()
    return bytes(raw)


def _text(raw: bytes) -> str:
    return raw.decode("utf-8", "replace")


def _musicscore_obj(raw: bytes):
    """MusicScoreData as plain json, raw gzip or base64(gzip). None if it isn't one."""
    for attempt in (lambda: json.loads(_text(raw)),
                    lambda: json.loads(gzip.decompress(raw)),
                    lambda: json.loads(gzip.decompress(base64.b64decode(raw)))):
        try:
            obj = attempt()
        except Exception:
            continue
        if isinstance(obj, dict) and "NoteList" in obj and "MusicScoreEventDataList" in obj:
            return obj
    return None


def _is_musicscore(raw: bytes) -> bool:
    return _musicscore_obj(raw) is not None


def _add_skill_fever(score, ext: dict):
    """put our extension's skills/fever back onto a loaded Score."""
    from sonolus_converters.notes.single import Skill, FeverChance, FeverStart
    if not isinstance(ext, dict):
        return
    for t in ext.get("skills") or []:
        score.notes.append(Skill(beat=round(int(t) / TICKS_PER_BEAT, 6)))
    if ext.get("feverChance") is not None:
        score.notes.append(FeverChance(beat=round(int(ext["feverChance"]) / TICKS_PER_BEAT, 6)))
    if ext.get("feverStart") is not None:
        score.notes.append(FeverStart(beat=round(int(ext["feverStart"]) / TICKS_PER_BEAT, 6)))


def _load_score(raw: bytes):
    """bytes -> (Score, format-name). Raises if the bytes are not a chart we know."""
    from sonolus_converters import LevelData, detect, mmws, pjsk, sus, usc

    ms = _musicscore_obj(raw)
    if ms is not None:
        # always hand the loader plain json: it copes with all three wrappings, but our
        # extension key has to be read off the same object anyway
        score = pjsk.load(io.BytesIO(json.dumps(ms).encode("utf-8")))
        _add_skill_fever(score, ms.get(EXT_KEY))
        return score, "MusicScoreData json"

    found = detect(raw)
    if not found:
        raise ValueError("unrecognised chart format")
    fmt, spec = found

    if fmt == "sus":
        return sus.load(io.StringIO(_text(raw))), "sus"
    if fmt == "pjsk":
        return pjsk.load(io.BytesIO(raw)), "MusicScoreData json"
    if fmt == "usc":
        return usc.load(io.StringIO(_text(raw))), "usc/%s" % spec
    if fmt == "mmw":
        # mmws is a BINARY format: the loader reads fp.buffer. it used to get a StringIO here,
        # which has no .buffer, so every .mmws upload quietly failed to convert.
        return mmws.load(io.TextIOWrapper(io.BytesIO(raw), encoding="utf-8")), "mmws/%s" % spec
    if fmt == "lvd":
        kind = spec.replace("compress_", "")
        if kind == "pysekai":
            # untitledcharts / next sekai data (what ChartDownloader saves as NSLevelData). the
            # library can only EXPORT this one, so we read it with our own inverse of its exporter
            try:
                from scripts import pysekai_loader
            except ImportError:
                import pysekai_loader
            return pysekai_loader.load_bytes(raw), "LevelData/%s" % spec
        # the detector's names aren't the module names
        mod = {"chcy": LevelData.chart_cyanvas,
               "usekai": LevelData.untitled_sekai}.get(kind) or getattr(LevelData, kind, None)
        if mod is None:
            raise ValueError("unsupported LevelData variant: %s" % spec)
        return mod.load(io.BytesIO(raw)), "LevelData/%s" % spec
    raise ValueError("unsupported chart format: %s/%s" % (fmt, spec))


def load_score(raw):
    return _load_score(_as_bytes(raw))[0]


def looks_like_chart(raw) -> bool:
    """True if these bytes are a chart in any format we can turn into .sus."""
    raw = _as_bytes(raw)
    try:
        from sonolus_converters import detect

        return detect(raw) is not None or _is_musicscore(raw)
    except Exception:
        try:
            from scripts.musicscore_to_sus import looks_like_musicscore

            return looks_like_musicscore(json.loads(_text(raw)))
        except Exception:
            return False


def _dedupe_timescale(score, warn):
    """two hi-speed changes on the same tick: keep the later one, that's what the chart meant.
    (holodori m0300 has 0.4 and 0.5 both at bar 86 -- which one the client picks is a coin flip.)"""
    from sonolus_converters.notes.timescale import TimeScaleGroup
    for g in score.notes:
        if not isinstance(g, TimeScaleGroup):
            continue
        seen = {}
        for pt in g.changes:
            seen[round(pt.beat * TICKS_PER_BEAT)] = pt
        if len(seen) != len(g.changes):
            warn(f"dropped {len(g.changes) - len(seen)} duplicate hi-speed change(s) on the same tick")
            keep = sorted(seen.values(), key=lambda p: p.beat)
            g.changes.clear()
            g.changes.extend(keep)


def _merge_layers(score, warn):
    """the client only reads #TIL00, so fold every note onto layer 0.

    layers that never change speed (all 1.0) merge exactly. a layer that does change speed can't
    be kept -- the game would have ignored it anyway, so the note just follows layer 0 like it
    would have in-game. we say so instead of pretending."""
    from sonolus_converters.notes.timescale import TimeScaleGroup
    from sonolus_converters.notes.single import Single
    from sonolus_converters.notes.slide import Slide
    from sonolus_converters.notes.guide import Guide
    groups = [n for n in score.notes if isinstance(n, TimeScaleGroup)]
    if len(groups) <= 1:
        return
    lossy = [i for i, g in enumerate(groups[1:], 1)
             if any(abs(p.timeScale - 1.0) > 1e-9 for p in g.changes)]
    if lossy:
        warn(f"chart has {len(groups)} hi-speed layers but the game only reads layer 00 -- "
             f"layer(s) {lossy} had their own speed changes, those notes now follow layer 00")
    for n in score.notes:
        pts = [n] if isinstance(n, Single) else (n.connections if isinstance(n, Slide) else
                                                 (n.midpoints if isinstance(n, Guide) else []))
        for p in pts:
            if getattr(p, "timeScaleGroup", 0):
                p.timeScaleGroup = 0
    first = groups[0]
    score.notes[:] = [n for n in score.notes if not isinstance(n, TimeScaleGroup) or n is first]


def _export_sus(score) -> str:
    from sonolus_converters import sus
    LAST_WARNINGS.clear()
    warn = LAST_WARNINGS.append
    _dedupe_timescale(score, warn)
    _merge_layers(score, warn)
    out = io.StringIO()
    sus.export(out, score, keep_note_speed_ratios=True)
    return out.getvalue()


def to_sus_text(raw, title: str = "") -> str | None:
    """Any supported chart -> .sus text. None when these bytes are not a chart at all.

    A .sus comes back byte-for-byte as it went in. Re-exporting one would be lossless in
    note terms but still rewrites the file, and charters tune .sus by hand -- there is no
    reason to touch what already works.
    """
    raw = _as_bytes(raw)

    try:
        from sonolus_converters import detect

        found = detect(raw)
        if found and found[0] == "sus" and not _is_musicscore(raw):
            return _text(raw)  # already what the bundle wants
        if found or _is_musicscore(raw):
            score, _fmt = _load_score(raw)
            if title and not getattr(score.metadata, "title", ""):
                score.metadata.title = title
            return _export_sus(score)
        return None
    except ImportError:
        pass
    # no silent fallback to the old hand-rolled reader anymore: it had the ease curves backwards
    # (1/2 swapped vs the client's NoteLineType enum) and dropped per-note speed, so "it
    # converted" used to mean "it converted wrong". a real parse error now surfaces instead.

    # library missing entirely: the old converter is still better than nothing
    try:
        from scripts.musicscore_to_sus import looks_like_musicscore, musicscore_to_sus

        data = json.loads(_text(raw))
        if not looks_like_musicscore(data):
            return None
        return musicscore_to_sus(data, title=title)
    except Exception:
        return None


def combo_count(sus_text: str) -> int:
    """the note count the game shows, counted the way it counts (half-beat slide ticks and all).
    matched sega's own totalNoteCount on all 257 official charts we had, where the old
    builder.count_sus_notes matched none."""
    from sonolus_converters import sus
    return sus.load(io.StringIO(sus_text)).combo_count


def score_to_musicscore_json(score, music_id: int = 0) -> dict:
    """Score -> MusicScoreData dict (what the in-game score maker saves), plus our skill/fever key."""
    from sonolus_converters import pjsk
    from sonolus_converters.notes.single import Skill, FeverChance, FeverStart
    buf = io.BytesIO()
    pjsk.export(buf, score, music_id)
    data = json.loads(gzip.decompress(base64.b64decode(buf.getvalue())))
    skills = sorted(round(n.beat * TICKS_PER_BEAT) for n in score.notes if isinstance(n, Skill))
    fc = [round(n.beat * TICKS_PER_BEAT) for n in score.notes if isinstance(n, FeverChance)]
    fs = [round(n.beat * TICKS_PER_BEAT) for n in score.notes if isinstance(n, FeverStart)]
    if skills or fc or fs:
        data[EXT_KEY] = {"skills": skills,
                         "feverChance": fc[0] if fc else None,
                         "feverStart": fs[0] if fs else None}
    return data


def from_holodori(text: str, music_id: int = 0):
    """hololive Dreams .sus -> (MusicScoreData dict, Score). holodori's own skill cues and fever
    windows come through as sekai skill / fever-chance / fever-start (the library does that)."""
    from sonolus_converters import holodori_sus
    from sonolus_converters.notes.holodorievents import convert_holodori_events
    score = holodori_sus.loads(text)
    # slot skills -> sekai skills, charge window -> fever chance, fever window -> fever start.
    # the window ENDS have nowhere to go in sekai and get dropped here, same as the sus export.
    score.notes = convert_holodori_events(score.notes)
    return score_to_musicscore_json(score, music_id), score

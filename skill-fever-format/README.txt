================================================================================
Skill + Fever in Project SEKAI charts — the format, and how to build it
================================================================================

THE PROBLEM
--------------------------------------------------------------------------------
Project SEKAI's own in-game Chart Maker exports MusicScoreData JSON. That JSON has
nowhere to store skill triggers or the fever window — its event enum is only
BPM / HighSpeed / SeVolume / TimeSignature, nothing else.

The game strips skill/fever out of a submitted chart on purpose
(FilterOutSkillFeverEvents) and merges them back at play time from the song's
OFFICIAL chart (BuildMergedSkillFeverEvents). That's fine for real songs, but a
brand-new custom song has no official chart to merge from, so it plays with no
skills and no fever at all.

So a chart tool needs to (a) carry skill/fever through its own pipeline, and
(b) write them somewhere the chart actually keeps.


TWO PLACES SKILL/FEVER LIVE
--------------------------------------------------------------------------------

1) In .sus text — the game DOES read these when they're in the .sus itself.
   Encoding (read straight off official charts):

     #mmm10:41    tap channel (1), lane 0, type 4   -> a skill trigger
     #mmm1f:11    tap channel (1), lane f, type 1   -> fever chance starts
     #mmm1f:21    tap channel (1), lane f, type 2   -> fever starts

   mmm = measure number (with #MEASUREBS offset applied if present).
   Every official chart has exactly SIX skills, plus one fever-chance and one
   fever-start. Measured over 43 expert charts, they land at about these
   fractions of the song, every time:

     skills:        0.112, 0.243, 0.370, 0.534, 0.703, 0.862
     fever chance:  0.556   (between the 4th and 5th skill)
     fever start:   0.676

   Fever is shared across difficulties in multi-live, so official charts put the
   six skills at identical positions on every difficulty of a song.

2) In MusicScoreData JSON — the JSON can't hold them natively, so carry them in a
   top-level extension key the game's JSON reader simply ignores:

     "CanvasSkillFever": {
       "skills":      [tick, tick, tick, tick, tick, tick],
       "feverChance": tick,      // or null
       "feverStart":  tick       // or null
     }

   tick = round(beat * 480)   (480 ticks per beat).
   A tool reads this key back and re-attaches Skill / FeverChance / FeverStart
   notes to the chart before exporting to .sus. A JSON without the key just gets
   default positions filled in (see below), so nothing ends up skill-less.


HOW TO BUILD / GENERATE IT
--------------------------------------------------------------------------------
Given a chart with a known note span (first note tick .. last note tick):

  span   = last_tick - first_tick
  skills = [ first_tick + round(span * f) for f in
             (0.112, 0.243, 0.370, 0.534, 0.703, 0.862) ]
  fever_chance = first_tick + round(span * 0.556)
  fever_start  = first_tick + round(span * 0.676)

Snap each to the nearest measure start (the game places them at measure
boundaries). Then either:
  - write them into the .sus using the three encodings above, or
  - write the CanvasSkillFever key into the MusicScoreData JSON.

If a chart already has skill/fever markers, KEEP them — only fill in defaults for
a chart that has none. When a song has multiple difficulties, copy one
difficulty's markers to the others so fever lines up across all of them.

The reference implementation of all of the above is skill_fever_reference.py in
this folder (no third-party dependencies, pure Python).


FILES IN THIS FOLDER
--------------------------------------------------------------------------------
skill_fever_reference.py  Standalone, dependency-free reference: read/write the .sus
                          encodings, read/write the CanvasSkillFever JSON key, and
                          generate official-style positions. Start here.

The next three are the real production pipeline they were lifted from. They depend on
the "sonolus-converters" library (Sonolus/UntitledCharts format converters); read them
as worked examples of the same ideas at full scale.

sus_skill_fever.py        The full skill/fever engine: reads markers out of a .sus,
                          measures where a chart's existing markers are, and fills in
                          the six official positions (per-difficulty, copying a
                          sibling difficulty's markers when one exists) for a chart
                          that has none. This is what skill_fever_reference.py is a
                          slimmed-down version of.
chart_convert.py          One entry point in, .sus text out, for every chart format:
                          .sus, MusicScoreData .json (plain / gzip / base64), .usc,
                          .mmws, LevelData, holodori .sus. This is where the
                          CanvasSkillFever key is written on export
                          (score_to_musicscore_json) and read back on import
                          (_add_skill_fever) — the exact JSON round-trip described
                          above. Also merges hi-speed layers down to #TIL00 and keeps
                          per-note speed ratios.
pysekai_loader.py         Reader for the "pysekai" LevelData variant (Next Sekai /
                          UntitledCharts NSLevelData), which the converter library can
                          only export — this is the inverse so those charts can be
                          imported too.

NOTE: chart_convert.py and pysekai_loader.py import `sonolus_converters`
(pip install from github.com/UntitledCharts/sonolus-level-converters). Only
skill_fever_reference.py runs with the standard library alone.

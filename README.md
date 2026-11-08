# Canvas Editor

A standalone chart editor for Project SEKAI-style rhythm charts (`.sus`, MusicScoreData `.json`,
Sonolus LevelData, `.mmws`/`.usc`). It is built on
[External-Unofficial-ChartMaker](https://github.com/watagashi-uni/External-Unofficial-ChartMaker)
(a MikuMikuWorld fork) and keeps everything MMW does: taps, criticals, flicks, holds with relay
and hidden steps, guides, traces, eases, per-note speed, BPM, hi-speed, time signatures, audio
with offset/waveform/scrub/speed, undo/redo, copy/paste, mirror and dockable panels.

It works entirely offline with local files. Nothing needs an account or a server; the online
bits (export targets, profile link) are optional and switched off until you set them up.

## What Canvas adds

- **Issue List**: scans the chart for real problems (overlapping notes, stacked holds, hidden steps, broken holds,
  bad BPM / hi-speed values, missing or misplaced skills and FEVER, mismatched difficulties) in a sortable table;
  click a row to jump there. *Edit > Detect Chart Issues* (F7).
- **Chart overview**: a strip beside the timeline showing the whole chart, its markers and where the issues are.
- **Skill & FEVER**: first-class markers that survive every format; *Chart > Auto-place Skills & FEVER* puts the six
  official-style skill positions and the FEVER window in place.
- **Song projects** (`.canvassong`): every difficulty of a song in one project, each with its own undo history.
- **Formats**: open anything (detected from the file contents); export `.mmws`, `.ccmmws`, `.sus`, MusicScoreData
  `.json`, `.usc`, Sonolus LevelData. Canvas's own file format is `.ce`.
- **Score Preview** with the game's UI art, card cut-ins, FEVER and SUPER FEVER, and the game's sounds.
- **Video export**: *File > Export Preview Video* renders the preview to `.mp4` through ffmpeg.
- **Export targets** and an optional **profile link** (both off until you set them up).
- **SEKAI Mode**: dresses the editor like SEKAI-Stories (colours, rounded buttons, transitions, backgrounds).
- **canvas-cli**: the chart engine without the window: `info`, `convert`, `skill-fever`, `check` (CI friendly), `dump`, `edit`.

## Running

Unzip anywhere and run `CanvasEditor` (`CanvasEditor.exe` on Windows). The builds are not code-signed, so
macOS and Windows may warn the first time you open them (on macOS right-click > Open).

## Credits and licenses

Built on External-Unofficial-ChartMaker / MikuMikuWorld, Dear ImGui, GLFW, DirectXMath and other open-source
libraries; their license texts are in `Licenses/`. Game art and sounds in Score Preview come from OpenSekai.
This is an unofficial fan tool and is not affiliated with SEGA or Colorful Palette.

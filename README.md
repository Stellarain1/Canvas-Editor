# Canvas Editor

A standalone chart editor for Project SEKAI-style rhythm charts. It opens `.sus`, MusicScoreData `.json`,
Sonolus LevelData and `.mmws`/`.usc` files, keeps everything MikuMikuWorld does, and adds a lot on top.
It works entirely offline with local files. No account or server is needed.

**Downloads are on the [Releases page](../../releases).** This repository only holds this page; the source is not published.

## Download

| Platform | File |
|---|---|
| Windows (x64) | `CanvasEditor-1.2.2b-windows-x64.zip` |
| macOS (Apple silicon) | `CanvasEditor-1.2.2b-macos-arm64.zip` |
| macOS (Intel) | `CanvasEditor-1.2.2b-macos-x86_64.zip` |
| Linux (x64) | `CanvasEditor-1.2.2b-linux-x64.zip` |

Unzip anywhere and run `CanvasEditor`. Each zip also contains `canvas-cli` (command line tools) and `canvas-mcp`
(an MCP server so an AI agent can chart with it), plus the open-source license texts in `Licenses/`.
The builds are not code-signed, so macOS and Windows may warn the first time you open them.

## What Canvas adds

- **Issue List**: scans the chart for real problems (overlapping notes, stacked holds, hidden steps, bad BPM or
  hi-speed values, missing skills/FEVER, mismatched difficulties) with a sortable table and click-to-jump.
- **Chart overview**: a strip beside the timeline showing the whole chart, markers and issues.
- **Skill & FEVER** markers that survive every format, and auto-placement of the official skill positions.
- **Song projects** (`.canvassong`): every difficulty of a song in one project, each with its own undo history.
- **Formats**: open anything; export `.mmws`, `.ccmmws`, `.sus`, MusicScoreData `.json`, `.usc`, Sonolus LevelData.
  Canvas's own file format is `.ce`; `.mmws` still opens and exports.
- **Score Preview** with the game's own UI art, card cut-ins, FEVER and SUPER FEVER, and the game's sounds.
- **Video export** of the preview to `.mp4`.
- **Auto charting**: analyses a song and charts any difficulty in the style of official charts.
- **AI agents (MCP)**: `canvas-mcp` lets an MCP client analyse a song, chart it and check it for issues.

## Version 1.2.2b

New:

1. Score Preview uses the game's own UI art in the game's layout: score bar, life bar, combo, PERFECT and score rank.
2. Live Cards (Settings > Canvas > Live Cards): pick the 5 cards Score Preview uses, paste a sekai.best card link or use
   your own icon, cut-in art and voice line, and change each card's skill.
3. Game-style card cut-ins (zoomed card scene in a tilted panel with glass shards, alternating sides) with the game's
   cut-in sound and voice line, in Score Preview and video export. Can be turned off.
4. Co-op mode shows the member row under the score: card icons, everyone's combo, "YOU", and "OK!" during FEVER CHANCE.
5. Reworked FEVER: everything outside the lanes dims during FEVER CHANCE and the lane edges fill up, rainbow to
   "FEVER!", then purple to "SUPER FEVER!!", followed by the SUPER FEVER!! frame and particles.
6. The game's note sounds (3 sets plus the old MMW ones), skill, FEVER, ALL PERFECT and cut-in sounds.
7. Charting timer (Session and Total).
8. Chart end: right-click the timeline > "Place End Here", drag the END flag to move it.
9. Frame rate setting (Settings > General > Video): VSync, 60 up to 480, or unlimited.
10. Combo colour option: rainbow ALL PERFECT (default) or the normal pink.
11. Canvas Editor's own file format `.ce`; `.mmws` files still open and export.
12. Auto Fix Chart Issues now fixes every overlap by itself, including hold points stacked on hold points.

Fixes: ALL PERFECT!! background and 60 FPS playback, the "+" icon and score-number spacing, skill panel lines, the
note tool highlight, note perspective in Score Preview, and life bar number placement.

## Credits and licenses

Canvas Editor is built on [External-Unofficial-ChartMaker](https://github.com/watagashi-uni/External-Unofficial-ChartMaker)
(a MikuMikuWorld fork), and uses Dear ImGui, GLFW, DirectXMath and other open-source libraries. Their license texts
ship inside every download (`Licenses/`). Game art and sounds in Score Preview come from OpenSekai.
This is an unofficial fan tool and is not affiliated with SEGA or Colorful Palette.

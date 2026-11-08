# Canvas Editor

Chart Editor for Project SEKAI, You can open almost any file type (as long as it's pjsk.), and adds a lot more features then normal MMW!

Some online features are coming soon, Like Browse!

**Downloads are on the [Releases page](../../releases).** 

## Download

| Platform | File |
|---|---|
| Windows (x64) | `CanvasEditor-1.3.0b-windows-x64.zip` |
| macOS (Apple silicon) | `CanvasEditor-1.3.0b-macos-arm64.zip` |
| macOS (Intel) | `CanvasEditor-1.3.0b-macos-x86_64.zip` |
| Linux (x64) | `CanvasEditor-1.3.0b-linux-x64.zip` |

Note: The builds are not code-signed, so macOS and Windows may warn the first time you open them.

## What Canvas adds

- **Issue List**: scans the chart for problems (overlapping notes, stacked holds, hidden steps, etc) with a sortable table and click-to-jump.
- **Chart overview**: a strip beside the timeline showing the whole chart along with it's markers and issues.
- **Song projects** (`.canvassong`/`.ce`): every difficulty of a song in one file. each with its own undo history!
- **Formats**: open anything; export `.mmws`, `.ccmmws`, `.sus`, MusicScoreData `.json`, `.usc`, Sonolus LevelData.
  Canvas's own file format is `.ce` and `.mmws` still opens and exports fine.
- **Score Preview** with the game's own UI art, card cut-ins, FEVER and SUPER FEVER, and the game's sounds.
- **Video export** of the preview to `.mp4`. (up to 4K 60FPS!)

## Credits and licenses

Canvas Editor is built on [External-Unofficial-ChartMaker](https://github.com/watagashi-uni/External-Unofficial-ChartMaker)
(a MikuMikuWorld fork), and uses Dear ImGui, GLFW, DirectXMath and other open-source libraries. Their license texts
ship inside every download (`Licenses/`). Game art and sounds in Score Preview come from OpenSekai.
This is an unofficial fan tool and is not affiliated with SEGA or Colorful Palette.

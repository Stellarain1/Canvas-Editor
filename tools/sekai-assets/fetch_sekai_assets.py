#!/usr/bin/env python3
"""
Download the parts of SEKAI-Stories that Canvas Editor's SEKAI Mode uses.

SEKAI-Stories (https://github.com/lezzthanthree/SEKAI-Stories) is GPL-3.0 code, but its
images, sounds and fonts are property of SEGA, Colorful Palette and Crypton. Canvas does not
redistribute them; this script fetches them for your own local use, straight from GitHub.

    python3 tools/sekai-assets/fetch_sekai_assets.py [target-folder] [--full-backgrounds]

Then set Settings > Canvas > SEKAI-Stories folder to the printed path and turn on SEKAI Mode.

What it downloads (a sparse git checkout of public/):
  sound/                 open / close / select UI sounds
  fonts/                 FOT-Rodin (intro title)
  img/                   Background_Between_Worlds.jpg etc.
  background_low_jpg/    backgrounds for the timeline
  background_compressed/ higher quality backgrounds (+240 MB, only with --full-backgrounds)

About 210 MB in total without --full-backgrounds.

Needs git 2.25+.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys

REPO = "https://github.com/lezzthanthree/SEKAI-Stories.git"


def default_target() -> pathlib.Path:
    if sys.platform == "win32":
        base = pathlib.Path(os.environ.get("APPDATA", pathlib.Path.home()))
    elif sys.platform == "darwin":
        base = pathlib.Path.home() / "Library" / "Application Support"
    else:
        base = pathlib.Path(os.environ.get("XDG_CONFIG_HOME", pathlib.Path.home() / ".config"))
    return base / "CanvasEditor" / "SEKAI-Stories"


def run(*args: str, cwd: pathlib.Path | None = None) -> None:
    print("+", " ".join(args))
    subprocess.run(args, cwd=cwd, check=True)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    full = "--full-backgrounds" in sys.argv
    target = pathlib.Path(args[0]).expanduser() if args else default_target()

    folders = ["public/sound", "public/fonts", "public/img", "public/background_low_jpg"]
    if full:
        folders.append("public/background_compressed")

    if (target / ".git").is_dir():
        run("git", "sparse-checkout", "set", *folders, cwd=target)
        run("git", "pull", "--ff-only", cwd=target)
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", "--depth", "1", "--filter=blob:none", "--sparse", REPO, str(target))
        run("git", "sparse-checkout", "set", *folders, cwd=target)

    print()
    print("SEKAI-Stories assets are in:", target)
    print("Set Settings > Canvas > SEKAI-Stories folder to that path and enable SEKAI Mode.")
    print("(These assets are property of SEGA, Colorful Palette and Crypton; keep them for personal use.)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Builds MikuMikuWorld/res/sekai, the Project SEKAI asset pack Canvas Editor ships with (CMake
option CANVAS_BUNDLE_GAME_ASSETS, on by default). Everything in it is SEGA / Colorful Palette /
Crypton / Fontworks property, taken from three public repositories:

  OpenSekai               https://github.com/cubeww/OpenSekai               UI sprites, SEKAI background
  SEKAI-Stories           https://github.com/lezzthanthree/SEKAI-Stories    UI sounds, FOT-Rodin fonts
  pjsekai-overlay-APPEND  https://github.com/TootieJin/pjsekai-overlay-APPEND  skill / FEVER / ALL PERFECT art

    python tools/sekai-assets/build_asset_pack.py <OpenSekai> <SEKAI-Stories> <pjsekai-overlay-APPEND>

(clone them with --depth 1 first). Needs Pillow. Without the pack Canvas draws its own
stand-ins, so a build without game assets still works (-DCANVAS_BUNDLE_GAME_ASSETS=OFF).
"""
import glob
import json
import os
import re
import shutil
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "MikuMikuWorld", "res", "sekai")

# OpenSekai sprites cut out of the packed atlases (name -> folder in the pack)
SPRITES = {
    "ui": [
        # buttons and panels (9-slice, borders in sprites.json)
        "btn_round_h56_wh", "btn_round_h80_wh", "btn_round_h80_gn", "btn_round_r16_wh", "btn_circle_h56_wh", "btn_circle_h80_wh",
        "btn_circle_h96_wh", "btn_circle_128_gn", "btn_close_wh", "btn_close_dbl", "eff_btn_round_h80_a70_wh", "eff_btn_circle_h56_a70_wh",
        "bg_base_r8_wh", "bg_base_r10_wh", "bg_base_r12_wh", "bg_base_r16_wh", "bg_base_r20_wh", "bg_base_r30_wh", "bg_base_wh",
        "bg_base_round_h48_wh", "bg_base_round_h56_wh", "bg_base_round_h64_wh", "bg_base_shadow_r16", "bg_base_shadow_r30",
        "bg_base_circle_h48_wh", "bg_base_half_r16_wh", "noti_base_round_h64_wh", "frame_select_r10_wh", "bg_label_rangeselect",
        "bg_tips_lgr", "gauge_base_h48_dbl", "line_dot_h4_wh",
        # icons
        "icon_back_wh", "icon_menu_wh", "icon_setting_wh", "icon_tips_wh", "icon_infomation_wh", "icon_plus_wh", "icon_triangle_wh",
        "icon_check_pk", "icon_caution_wh", "icon_zoomup_wh", "icon_zoomdown_wh", "icon_play_h52_wh", "icon_stop_h52_wh",
        "icon_testplay_wh", "icon_copy_h40_wh", "icon_dustbox_h40_wh", "icon_clipboard_wh", "icon_reflect_hor_h40_wh",
        "icon_reflect_ver_h40_wh", "icon_bpm_change_h112_wh", "icon_beat_h112_wh", "icon_notes_speed_h112_wh",
        "icon_notes_speed_h32_wh", "icon_move_wh", "icon_overwrite_wh", "icon_repeeet_h36_wh", "icon_restarth36_wh",
        "icon_scale_horizon_h40_wh", "icon_swipescroll_h112_wh", "icon_change_h40_wh", "icon_noteslock_wh", "icon_notesunlock_wh",
        "icon_longall_wh", "badge_attention_rd", "balloon_bpm_dgr",
    ],
    # the game's Score Maker note palette
    "notes": "notes_icon_*",
    # live UI
    "live": [
        "bg_cutin_skill_scoreUp", "bg_cutin_skill_judge", "bg_cutin_skill_life", "icon_skill_scoreUp", "icon_skill_judge",
        "icon_skill_life", "icon_skill_scoreSupport", "label_autolive", "btn_stop", "btn_skip",
    ] + ["skillEffect_text_%d" % i for i in range(13)],
    "judge": "judge_*",
    # the live HUD (score, life, combo, skill cut-in), laid out like FrontUIView.prefab
    "hud": [
        "bg_scoreGauge", "gauge_base_h16_dbl", "gauge_bar_ingame_freeLive_s", "bg_base_ingame_wh", "meter_mark", "effect_scoreGauge",
        "A", "B", "C", "S", "score_S", "score_A", "score_B", "score_C", "score_D", "txt_scoreRank_ingame_wh",
        "text_plus_36wh", "text_outline_plus_36wh", "bg_lifeGauge", "bar_lifeGauge_gn", "bar_lifeGauge_rd", "effect_lifeGauge",
        "icon_heart_gn", "txt_combo_pr", "txt_combo_ap_rbw", "eff_combo", "txt_cutin_skill_wh", "txt_skill_scoreUp",
        "txt_skill_judge", "txt_skill_life", "skillLv_40px_Lv_wh", "skillLv_40px_shadow_Lv", "cardFrame_S_1", "cardFrame_S_2",
        "cardFrame_S_3", "cardFrame_S_4", "cardFrame_S_bd", "lane_skill",
    ] + ["number_48px_%d_wh" % i for i in range(10)] + ["number_48px_shadow_%d" % i for i in range(10)]
      + ["number_combo_154px_%d_pr" % i for i in range(10)] + ["number_combo_ap_154px_%d_rbw" % i for i in range(10)]
      + ["eff_%d_150" % i for i in range(10)] + ["skillLv_40px_%d_wh" % i for i in range(1, 5)]
      + ["skillLv_40px_shadow_%d" % i for i in range(1, 5)],
}

# art made for Canvas (the SCORERANK wordmarks in each rank's colour, the "+" of the score popup)
EXTRA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "extra")

OVERLAY_FILES = [
    "assets/ap.mp4",
    "assets/skill/SE_LIVE_SKILL.mp3",

    "extra-assets/clear_status/normal_ap.png",
    "extra-assets/clear_status/normal_fc.png",
]
OVERLAY_GLOBS = ["assets/skill/v3/**/*.png", "assets/skill/profile/*.png", "assets/judge/v3/*.png", "extra-assets/fever/*"]


def cut_sprites(opensekai):
    atlases = {}
    for meta in glob.glob(os.path.join(opensekai, "Assets", "Texture2D", "*.png.meta")):
        guid = re.search(r"guid: (\w+)", open(meta, encoding="utf-8").read()).group(1)
        atlases[guid] = meta[:-5]
    wanted = {}
    for folder, names in SPRITES.items():
        if isinstance(names, str):
            wanted[folder] = names
        else:
            for n in names:
                wanted[n] = folder
    images, info = {}, {}
    for path in glob.glob(os.path.join(opensekai, "Assets", "Sprite", "*.asset")):
        s = open(path, encoding="utf-8", errors="replace").read()
        m = re.search(r"texture: \{fileID: \d+, guid: (\w+)", s)
        if not m or m.group(1) not in atlases:
            continue
        name = re.search(r"m_Name: (.+)", s).group(1).strip()
        folder = wanted.get(name)
        if folder is None:
            for f, pattern in wanted.items():
                if isinstance(pattern, str) and "*" in pattern and re.fullmatch(pattern.replace("*", ".*"), name):
                    folder = f
                    break
        if folder is None:
            continue
        r = re.search(r"textureRect:\s*\n\s*serializedVersion: 2\s*\n\s*x: ([\d.]+)\s*\n\s*y: ([\d.]+)\s*\n\s*width: ([\d.]+)\s*\n\s*height: ([\d.]+)", s)
        x, y, w, h = (float(v) for v in r.groups())
        if w <= 0 or h <= 0:
            continue
        atlas = atlases[m.group(1)]
        if atlas not in images:
            images[atlas] = Image.open(atlas).convert("RGBA")
        im = images[atlas]
        crop = im.crop((round(x), round(im.height - y - h), round(x + w), round(im.height - y)))
        rotation = (int(re.search(r"settingsRaw: (\d+)", s).group(1)) >> 2) & 0xF
        crop = {1: lambda c: c.transpose(Image.FLIP_LEFT_RIGHT), 2: lambda c: c.transpose(Image.FLIP_TOP_BOTTOM),
                3: lambda c: c.transpose(Image.ROTATE_180), 4: lambda c: c.transpose(Image.ROTATE_90)}.get(rotation, lambda c: c)(crop)
        os.makedirs(os.path.join(OUT, folder), exist_ok=True)
        crop.save(os.path.join(OUT, folder, name + ".png"))
        border = [float(b) for b in re.search(r"m_Border: \{x: ([\d.]+), y: ([\d.]+), z: ([\d.]+), w: ([\d.]+)\}", s).groups()]
        pivot = [round(float(v), 4) for v in re.search(r"m_Pivot: \{x: ([\d.\-e]+), y: ([\d.\-e]+)\}", s).groups()]
        # border = left, bottom, right, top; pivot from the bottom-left, 0..1
        # ppu: texture pixels per world unit (the live HUD's camera shows 108 units per 1080 pixels)
        ppu = float(re.search(r"m_PixelsToUnits: ([\d.]+)", s).group(1))
        info[folder + "/" + name] = {"size": [int(w), int(h)], "border": border, "pivot": pivot, "ppu": ppu}
    return info


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    opensekai, stories, overlay = sys.argv[1:4]
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    info = cut_sprites(opensekai)

    # the SEKAI sky behind the timeline, the floating shapes and the 2D-mode lane window
    bg = os.path.join(OUT, "bg")
    os.makedirs(bg)
    shutil.copy(os.path.join(opensekai, "Assets", "Resources", "Common", "bg", "bg_sekai.png"), bg)
    shutil.copy(os.path.join(opensekai, "Assets", "Texture2D", "bg_2dmode_window_nml.png"), bg)
    shapes = Image.open(os.path.join(opensekai, "Assets", "Resources", "background", "eff_bgcommon_circle_triangle.png")).convert("RGBA")
    half = shapes.width // 2
    for i, name in enumerate(["circle_white", "circle_pink", "triangle_pink", "triangle_mint"]):
        shapes.crop(((i % 2) * half, (i // 2) * half, (i % 2 + 1) * half, (i // 2 + 1) * half)).save(os.path.join(bg, name + ".png"))
    shutil.copy(os.path.join(stories, "public", "img", "Background_Between_Worlds.jpg"), bg)

    # UI sounds and fonts
    for sub, files in (("sound", ["open.wav", "close.wav", "select.wav", "slide.wav"]),
                       ("fonts", ["FOT-RodinNTLGPro-DB.otf", "FOT-RodinNTLGPro-EB.otf"])):
        os.makedirs(os.path.join(OUT, sub), exist_ok=True)
        for f in files:
            shutil.copy(os.path.join(stories, "public", sub, f), os.path.join(OUT, sub, f))

    # pjsekai-overlay-APPEND's art, same layout as the repository (Canvas reads it as an overlay folder)
    copied = list(OVERLAY_FILES)
    for pattern in OVERLAY_GLOBS:
        copied += [os.path.relpath(p, overlay).replace("\\", "/") for p in glob.glob(os.path.join(overlay, pattern), recursive=True)]
    for rel in copied:
        to = os.path.join(OUT, "overlay", rel)
        os.makedirs(os.path.dirname(to), exist_ok=True)
        shutil.copy(os.path.join(overlay, rel), to)

    # Canvas's own additions, and a soft glow behind each rank letter
    for d, _, names in os.walk(EXTRA):
        for n in names:
            rel = os.path.relpath(os.path.join(d, n), EXTRA)
            os.makedirs(os.path.dirname(os.path.join(OUT, rel)), exist_ok=True)
            shutil.copy(os.path.join(d, n), os.path.join(OUT, rel))
    from PIL import ImageFilter
    for r in "SABCD":
        letter = Image.open(os.path.join(OUT, "hud", "score_%s.png" % r)).convert("RGBA")
        pad = 24
        glow = Image.new("RGBA", (letter.width + pad * 2, letter.height + pad * 2), (0, 0, 0, 0))
        glow.alpha_composite(letter, (pad, pad))
        glow = glow.filter(ImageFilter.GaussianBlur(9))
        glow.save(os.path.join(OUT, "hud", "score_%s_glow.png" % r))

    json.dump(info, open(os.path.join(OUT, "sprites.json"), "w"), indent=1, sort_keys=True)
    with open(os.path.join(OUT, "NOTICE.txt"), "w", encoding="utf-8") as f:
        f.write(__doc__.strip() + "\n\nCanvas Editor is a fan-made tool and is not affiliated with SEGA, Colorful Palette or Crypton.\n")
    total = sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(OUT) for n in ns)
    print("wrote %s: %d sprites, %.1f MB" % (OUT, len(info), total / 1048576))


if __name__ == "__main__":
    main()

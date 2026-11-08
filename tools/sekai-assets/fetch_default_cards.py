#!/usr/bin/env python3
"""
Fetches the five cards Canvas Editor's Score Preview uses out of the box (the Hatsune Miku cards
pjsekai-overlay-APPEND shows in its skill cut-in) from sekai.best into
tools/sekai-assets/extra/cards, where build_asset_pack.py picks them up:

    <assetbundle>/thumbnail.png   the square card icon (members row, skill panel)
    <assetbundle>/cutin.png       the character cut-out the skill cut-in slides in
    <assetbundle>/voice.mp3       the card's gacha line, played with the cut-in
    deck.json                     name, skill, level value, rarity, trained, link for each

    python tools/sekai-assets/fetch_default_cards.py

Needs curl. Canvas's own "Get from sekai.best" (Settings > Canvas > Live cards) does the same at
run time for any card.
"""
import json
import os
import subprocess
import sys
import tempfile

STORAGE = "https://storage.sekai.best/sekai-jp-assets/"
MASTER = "https://sekai-world.github.io/sekai-master-db-diff/"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "extra", "cards")

# assetbundle, trained art?
DECK = [("res021_no016", True), ("res021_no028", True), ("res021_no011", True), ("res021_no013", True), ("res021_no033", True)]


def fetch(url, path):
    r = subprocess.run(["curl", "-L", "-f", "-s", "-S", "--compressed", "-o", path, url])
    return r.returncode == 0 and os.path.getsize(path) > 0


def main():
    tmp = tempfile.mkdtemp()
    tables = {}
    for t in ("cards", "skills", "gameCharacters"):
        p = os.path.join(tmp, t + ".json")
        if not fetch(MASTER + t + ".json", p):
            sys.exit("could not download " + t + ".json")
        tables[t] = json.load(open(p, encoding="utf-8"))
    skills = {s["id"]: s for s in tables["skills"]}
    chars = {c["id"]: c for c in tables["gameCharacters"]}
    deck = []
    for ab, trained in DECK:
        card = next(c for c in tables["cards"] if c["assetbundleName"] == ab)
        skill = skills[card["skillId"]]
        kind = {"score_up": "score", "judgment_up": "judgment", "life_recovery": "life"}.get(skill["descriptionSpriteName"], "score")
        value = 0
        for effect in skill["skillEffects"]:
            if effect["skillEffectType"].startswith("score_up"):
                value = max([value] + [d["activateEffectValue"] for d in effect["skillEffectDetails"] if d["level"] == 4])
        rarity = card["cardRarityType"]
        trained = trained and rarity in ("rarity_3", "rarity_4")
        art = "after_training" if trained else "normal"
        folder = os.path.join(OUT, ab)
        os.makedirs(folder, exist_ok=True)
        ok = fetch(STORAGE + "thumbnail/chara/%s_%s.png" % (ab, art), os.path.join(folder, "thumbnail.png"))
        # the cut-in shows the card's scene, like the game's 2D cut-in
        trimmed = False
        if not fetch(STORAGE + "character/member_cutout/%s/%s.png" % (ab, art), os.path.join(folder, "cutin.png")):
            trimmed = fetch(STORAGE + "character/member_cutout_trm/%s/%s.png" % (ab, art), os.path.join(folder, "cutin.png"))
        voice = card["gachaPhrase"] != "-" and fetch(STORAGE + "sound/gacha/get_voice/%s/%s.mp3" % (ab, ab), os.path.join(folder, "voice.mp3"))
        chara = chars[card["characterId"]]
        deck.append({
            "name": "%s%s - %s" % (chara.get("firstName", ""), chara.get("givenName", ""), card["prefix"]),
            "folder": ab, "skill": kind, "value": "%d%%" % value if value else "", "level": 4,
            "rarity": {"rarity_1": 1, "rarity_2": 2, "rarity_3": 3, "rarity_4": 4}.get(rarity, 5),
            "trained": trained, "trimmed": trimmed, "voice": bool(voice),
            "source": "https://sekai.best/card/%d" % card["id"],
        })
        print(ab, "ok" if ok else "NO THUMBNAIL", "trimmed" if trimmed else "untrimmed", "voice" if voice else "no voice")
    with open(os.path.join(OUT, "deck.json"), "w", encoding="utf-8") as f:
        json.dump({"format": "canvas-editor/deck", "cards": deck}, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()

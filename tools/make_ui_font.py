#!/usr/bin/env python3
"""
Build the UI font embedded in the plugin: Nanum Gothic (SIL OFL 1.1) cut down to the characters
the interface uses (printable ASCII + every non-ASCII character in src/plugin/UiStrings.h).

Because the subset is a Modified Version under the OFL, it must not use the Reserved Font Names
("Nanum", "NanumGothic", ...), so it is renamed to "AbleAEC UI KR".

usage:
  python3 tools/make_ui_font.py [--src DIR]   rebuild resources/fonts/AbleAECUiKR-{Regular,Bold}.ttf
  python3 tools/make_ui_font.py --check       fail if a character used in UiStrings.h is missing

requires: fonttools (pip install fonttools); for a rebuild, NanumGothic.ttf and NanumGothicBold.ttf
(Debian/Ubuntu: apt-get install fonts-nanum -> /usr/share/fonts/truetype/nanum).
"""

import argparse
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRINGS = os.path.join(ROOT, "src", "plugin", "UiStrings.h")
OUT_DIR = os.path.join(ROOT, "resources", "fonts")
FAMILY = "AbleAEC UI KR"
STYLES = {"Regular": "NanumGothic.ttf", "Bold": "NanumGothicBold.ttf"}
EXTRA = "·→…–—°±×≈"


def wanted_chars():
    text = open(STRINGS, encoding="utf-8").read()
    chars = {chr(c) for c in range(0x20, 0x7F)}
    chars |= {ch for ch in text if ord(ch) > 0x7E}
    chars |= set(EXTRA)
    return chars


def out_path(style):
    return os.path.join(OUT_DIR, f"AbleAECUiKR-{style}.ttf")


def rename(font, style):
    full = f"{FAMILY} {style}" if style != "Regular" else FAMILY
    ps = "AbleAECUiKR-" + style
    for rec in list(font["name"].names):
        if rec.nameID in (1, 16):
            rec.string = FAMILY
        elif rec.nameID in (2, 17):
            rec.string = style
        elif rec.nameID == 3:
            rec.string = f"{ps};AbleAEC UI subset of an OFL font by NAVER"
        elif rec.nameID == 4:
            rec.string = full
        elif rec.nameID == 6:
            rec.string = ps


def build(src_dir):
    chars = wanted_chars()
    os.makedirs(OUT_DIR, exist_ok=True)
    for style, fname in STYLES.items():
        src = os.path.join(src_dir, fname)
        font = TTFont(src)
        opts = subset.Options()
        opts.layout_features = ["*"]
        opts.name_IDs = ["*"]
        opts.name_languages = ["*"]
        opts.notdef_outline = True
        opts.hinting = False
        sub = subset.Subsetter(opts)
        sub.populate(unicodes=[ord(c) for c in chars])
        sub.subset(font)
        rename(font, style)
        font.save(out_path(style))
        print(f"wrote {out_path(style)} ({os.path.getsize(out_path(style)) // 1024} KB, {len(chars)} characters)")


def check():
    chars = wanted_chars() - set(EXTRA)
    bad = 0
    for style in STYLES:
        path = out_path(style)
        if not os.path.exists(path):
            print(f"missing font file {path}")
            bad += 1
            continue
        cmap = TTFont(path).getBestCmap()
        missing = sorted(c for c in chars if ord(c) not in cmap)
        if missing:
            print(f"{os.path.basename(path)} lacks {len(missing)} characters: {''.join(missing)}")
            bad += 1
    print("UI font covers every UI character" if not bad else "run: python3 tools/make_ui_font.py")
    return 1 if bad else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--src", default="/usr/share/fonts/truetype/nanum")
    a = ap.parse_args()
    sys.exit(check() if a.check else build(a.src) or 0)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Put COSMOS's site together (GitHub Pages, branch gh-pages):

  index.html, img/      the project page (web/site)
  radio/player/         RADIO's test player: a page that plays on the radio (web/site/radio-player)
  install/, firmware/, words/   the web installer and its package (tools/make_install.py)
  try/                  COSMOS in the browser: the firmware itself on a simulated FM-1 (APP_DIR/build/emu, from
                        base/tools/build.py APP_DIR --web with tools/wasm/emcc on the PATH; see docs/BUILDING.md)

  tools/make_pages.py APP_DIR PACKAGE.fwsc VERSION OUT_DIR"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main(app, pkg, version, out):
    out = Path(out)
    subprocess.run([sys.executable, str(ROOT / "tools" / "make_install.py"), app, pkg, version, str(out)], check=True)
    shutil.copy(ROOT / "web" / "site" / "index.html", out / "index.html")
    shutil.copytree(ROOT / "web" / "site" / "img", out / "img")
    shutil.copytree(ROOT / "web" / "site" / "radio-player", out / "radio" / "player")
    emu = Path(app) / "build" / "emu"
    if not (emu / "app.wasm").exists():
        raise SystemExit(f"{emu}: no emulator: build it first (base/tools/build.py {app} --web, tools/wasm on the PATH)")
    shutil.copytree(emu, out / "try", ignore=shutil.ignore_patterns("o2"))
    print(f"pages: {out}: index.html, img/, radio/player/, install/, try/")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])

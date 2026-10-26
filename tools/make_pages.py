#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Put COSMOS's site together (GitHub Pages, branch gh-pages):

  index.html, img/      the project page (web/site)
  install/, firmware/, words/   the web installer and its package (tools/make_install.py)
  try/                  the sound in the browser (web/emulator: index.src.html with data.js)

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
    emu = ROOT / "web" / "emulator"
    page = (emu / "index.src.html").read_text(encoding="utf-8").replace("/*DATA*/", (emu / "data.js").read_text(encoding="utf-8"))
    (out / "try").mkdir()
    (out / "try" / "index.html").write_text(
        '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        '<link rel="icon" href="../img/icon.png">\n'
        '<style>body{margin:0}[hidden]{display:none!important}</style>\n</head>\n<body>\n'
        + page + '\n</body>\n</html>\n', encoding="utf-8")
    print(f"pages: {out}: index.html, img/, install/, try/")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])

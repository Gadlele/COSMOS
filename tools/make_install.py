#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a firmware's install site (GitHub Pages) with fm1-base's web installer, without the rest of
base/tools/make_site.py's site (front page, manual, emulator: they need Emscripten):

  index.html                  a link to install/
  install/index.html          the web installer (Chrome or Edge, Web MIDI): APP_DIR/web/installer.html around
                              fm1-base's installer script when the app has one, else fm1-base's page
  firmware/NAME-VERSION.fwsc  the package it writes; also the download
  firmware/m-bootloader-X.Y.fwsc  m-bootloader (MIT), which the installer offers after the firmware
  words/                      the screen reader's words files (APP_DIR/web/words)

  tools/make_install.py APP_DIR APP_DIR/build/NAME-VERSION.fwsc VERSION OUT_DIR

The package must be a release build (./build.sh APP_DIR --release VERSION)."""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parents[1] / "base"
sys.path.insert(0, str(BASE / "tools"))
from build import read_conf  # noqa: E402
from make_site import MBOOT_VERSION, product_of, strip_module  # noqa: E402


def main(app, pkg, version, out):
    app, pkg, out = Path(app).resolve(), Path(pkg), Path(out)
    conf = read_conf(app / "fm1.toml")
    conf.setdefault("name", app.name)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(re.escape(conf.get("id_prefix", "FM-1_0")) + r"\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh {app.name} --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"{conf['name'].lower()}-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    boot = BASE / "web" / "m-bootloader" / f"fm1-uboot-{MBOOT_VERSION}.fwsc"
    boot_name = f"m-bootloader-{MBOOT_VERSION}.fwsc"
    shutil.copy(boot, out / "firmware" / boot_name)
    shutil.copy(BASE / "web" / "m-bootloader" / "LICENSE", out / "firmware" / "m-bootloader-LICENSE.txt")
    html = (BASE / "web" / "installer.html").read_text(encoding="utf-8")
    own = app / "web" / "installer.html"
    if own.is_file():                        # the app's own page around fm1-base's installer script
        script = html[html.index('<script type="module">'):html.index("</script>", html.index('<script type="module">')) + 9]
        html = own.read_text(encoding="utf-8").replace("<!--INSTALLER-SCRIPT-->", script)
    lib = strip_module((BASE / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((BASE / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name,
                       "backup": bool(conf.get("backup")),
                       "boot": {"pkg": "../firmware/" + boot_name, "version": MBOOT_VERSION,
                                "sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}})
    repo = conf.get("repo", "")
    credits = conf.get("credits", "")
    for key, val in (("NAME", conf["name"]), ("VERSION", version), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                     ("LICENSE", conf.get("licence", "GPL-3.0-or-later")),
                     ("CREDITS", " " + credits if credits else "")):
        html = html.replace("{{" + key + "}}", val)
    (out / "install" / "index.html").write_text(html.replace("/*LIB*/", lib).replace("/*META*/", meta)
                                                .replace("/*APP*/", "null"), encoding="utf-8")
    if (app / "web" / "words").is_dir():
        shutil.copytree(app / "web" / "words", out / "words")
    (out / "index.html").write_text(
        f'<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width">'
        f'<title>{conf["name"]}</title><meta http-equiv="refresh" content="0; url=install/">'
        f'<p><a href="install/">Install {conf["name"]} {version}</a></p>\n', encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: install/ ({product}), firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])

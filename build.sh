#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a COSMOS firmware with fm1-base's build (base/tools/build.py), in Terminus.
#   ./build.sh [APP_DIR] [build.py's options]      APP_DIR: probe (the default)
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain; base/tools/get_toolchain.sh)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
# The JieLi toolchain runs on Linux x86-64; on a Mac the build runs its tools in Docker (fm1-base's README).
set -e
cd "$(dirname "$0")"
APP="${1:-probe}"
[ $# -gt 0 ] && shift
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
export X0X_FONTSET=terminus
[ -f base/tools/build.py ] || git submodule update --init --recursive
exec "${PYTHON:-python3}" base/tools/build.py "$APP" "$@"

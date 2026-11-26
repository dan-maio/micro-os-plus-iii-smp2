#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# build-openocd-wch.sh -- build and install the WCH OpenOCD the CH32V307-EVT
# board uses (test/boards/ch32v307-evt/hw.sh).
#
#     build-openocd-wch.sh [<source dir>] [<prefix>]
#
# Source: karlp/openocd-hacks, branch mrs-wch-riscv-221010 (the MounRiver
# WCH fork, 0.11.0+dev, commit 266d7c8d3), plus the two patches beside this
# script:
#   gcc16-werror-fixes.patch  source fixes for GCC 16 (the fork builds with
#                             -Werror; gcc16-werror-fixes.md lists them)
#   wlinke-type-0x12.patch    accept WCH-LinkE probe type 0x12 (firmware
#                             2.10), which 221010 rejects ("unknow
#                             WCH-LINK")
# Default source dir: ~/Downloads/CH32/openocd-hacks (cloned if missing).
# Default prefix:     ~/.local/openocd-wch (an existing bin/openocd is kept
#                     as bin/openocd.prev).
# Needs: autotools, libusb-1.0, libftdi1, hidapi, libjaylink, capstone.
# -----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${1:-$HOME/Downloads/CH32/openocd-hacks}"
PREFIX="${2:-$HOME/.local/openocd-wch}"
COMMIT=266d7c8d3

if [[ ! -d "$SRC/.git" ]]; then
  git clone --branch mrs-wch-riscv-221010 https://github.com/karlp/openocd-hacks.git "$SRC"
fi
cd "$SRC"
git -c advice.detachedHead=false checkout "$COMMIT"

# Apply each patch once (a tree that already has it is left alone).
for p in gcc16-werror-fixes.patch wlinke-type-0x12.patch; do
  if git apply --check "$HERE/$p" 2>/dev/null; then
    git apply "$HERE/$p"
    echo "applied $p"
  elif git apply --reverse --check "$HERE/$p" 2>/dev/null; then
    echo "already applied: $p"
  else
    echo "build-openocd-wch.sh: $p does not apply to $SRC" >&2
    exit 1
  fi
done

[[ -x configure ]] || ./bootstrap
./configure --prefix="$PREFIX" --enable-wlink
make -j"$(nproc)"

if [[ -x "$PREFIX/bin/openocd" ]]; then
  cp -p "$PREFIX/bin/openocd" "$PREFIX/bin/openocd.prev"
fi
make install
"$PREFIX/bin/openocd" --version 2>&1 | head -1

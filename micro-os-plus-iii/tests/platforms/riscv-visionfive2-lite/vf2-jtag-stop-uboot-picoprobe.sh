#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# vf2-jtag-stop-uboot-picoprobe.sh -- reset the VisionFive 2 Lite and stop its U-Boot
# autoboot using JTAG only. No UART cable and no key sender.
#
#     ./vf2-jtag-stop-uboot-picoprobe.sh
#
# How:
#   1. arm the JH7110 watchdog from JTAG (soft SoC reset);
#   2. as soon as JTAG is available again, halt the boot hart, put UART0 into
#      internal loopback, send a CR, restore the UART, and resume.
#      U-Boot's autoboot check reads that CR from the RX FIFO and stops;
#   3. repeat during the boot window, then disarm the watchdog.
#
# U-Boot reaches its `StarFive #` prompt. The prompt itself is a UART output;
# if you have no UART cable you cannot see it, but the board is waiting there
# and will no longer start Linux. Connect UART only for output if you want to
# see it.
#
# Environment:
#   PROJ       directory with the jh7110_*.cfg files (default: this
#              script's own; an Eclipse workspace passes its project dir)
#   OPENOCD    openocd binary
#   KEY_TRIES  number of injection attempts (default: 5)
# -----------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="${PROJ:-$HERE}"   # the jh7110_*.cfg files sit beside this script
OCD="${OPENOCD:-$(ls -d "$HOME"/.local/xPacks/@xpack-dev-tools/openocd/*/.content/bin/openocd 2>/dev/null | sort -V | tail -1)}"
OCD="${OCD:-openocd}"
SCRIPTS_ARG=()
if [[ "$OCD" == *"/xPacks/"* ]]; then
  SCRIPTS_ARG=(-s "$(dirname "$(dirname "$OCD")")/openocd/scripts")
fi

CFG_WDT="$PROJ/jh7110_wdt_reset_picoprobe.cfg"
CFG_KEY="$PROJ/jh7110_jtag_key_picoprobe.cfg"
CFG_OFF="$PROJ/jh7110_wdt_disable_picoprobe.cfg"
TRIES="${KEY_TRIES:-5}"

die () { echo "vf2-jtag-stop-uboot: $*" >&2; exit 1; }

[[ -f "$CFG_WDT" ]] || die "no $CFG_WDT (set PROJ=...)"
[[ -f "$CFG_KEY" ]] || die "no $CFG_KEY"
[[ "$TRIES" =~ ^[0-9]+$ ]] || die "KEY_TRIES must be a number"

if command -v lsusb >/dev/null 2>&1; then
  if ! lsusb | grep -qi '2e8a:000c'; then
    die "no JTAG probe on Pico in lsusb (2e8a:000c); replug it"
  fi
fi

echo "jtag-stop: arming the watchdog (soft reset)"
"$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG_WDT" || die "could not arm the watchdog"

echo "jtag-stop: injecting U-Boot keys through UART0 loopback"
for i in $(seq 1 "$TRIES"); do
  if "$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG_KEY" >/dev/null 2>&1; then
    echo "  key injected (try $i)"
  fi
  sleep 1
done

echo "jtag-stop: trying to disarm the watchdog"
for _try in $(seq 1 10); do
  if "$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG_OFF" >/dev/null 2>&1; then
    echo "  WDT disabled"
    break
  fi
  sleep 1
done

echo
echo "done. If U-Boot was in its countdown it is now at the 'StarFive #' prompt."
echo "The prompt is a UART output; the board itself is stopped there."
echo "If it still booted Linux, power-cycle and run this again immediately."

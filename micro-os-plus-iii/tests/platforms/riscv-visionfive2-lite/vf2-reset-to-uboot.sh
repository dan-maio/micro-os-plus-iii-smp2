#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# vf2-reset-to-uboot.sh -- reset the VisionFive 2 Lite and hold a key down so
# the U-Boot autoboot countdown is interrupted (no Linux boot).
#
#     ./vf2-reset-to-uboot.sh [serial-device] [seconds]
#
# Defaults:
#   serial   /dev/ttyUSB0
#   seconds  10 for RESET=wdt, 15 otherwise
#
# Environment:
#   RESET    wdt | srst | power | halt | run   (default: wdt)
#   CFG      OpenOCD config (default: chosen by RESET, from PROJ)
#   PROJ     directory with the jh7110_*.cfg files (default: this
#            script's own; an Eclipse workspace passes its project dir)
#   OPENOCD  path to openocd
#   BAUD     115200
#   KEY      "\r" (CR; any key stops U-Boot autoboot)
#   WDT_WAIT seconds to wait for the watchdog reset (default: 10)
#
# IMPORTANT, measured on the VisionFive 2 Lite v1.1:
#
#   The RISC-V Debug Module's `reset halt` / `reset run` do NOT reboot the
#   SoC. They halt/resume the U74 harts and leave the loaded image in place.
#   A real board reset needs one of:
#
#     RESET=wdt    soft reset through the JH7110 watchdog, no pin needed.
#                  This is the default. It arms the watchdog from JTAG, lets
#                  it reset the SoC, then tries to disarm it after U-Boot is
#                  back (the WDT is in the always-on domain).
#
#     RESET=srst   the probe's RST/SRST wired to the board's real reset net
#                  (Reset button net / reset test point), NOT pin 36 (GPIO36).
#
#     RESET=power  no reset source: this script just sends the key stream;
#                  you power-cycle the board (or press an external reset).
#
#     RESET=halt   only for testing: reset halt + resume through the RISC-V
#     RESET=run    DM; on this board it does not restart U-Boot.
#
# The key stream starts BEFORE the reset and continues for the whole boot
# (boot ROM -> SPL -> OpenSBI -> U-Boot). U-Boot's countdown is 3 s; a CR
# every 50 ms is plenty.
# -----------------------------------------------------------------------------
set -uo pipefail

SERIAL="${1:-/dev/ttyUSB0}"
RESET="${RESET:-wdt}"
SECS="${2:-}"
if [[ -z "$SECS" ]]; then
  if [[ "$RESET" == wdt || "$RESET" == soft ]]; then SECS=10; else SECS=15; fi
fi
BAUD="${BAUD:-115200}"
KEY="${KEY:-\r}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="${PROJ:-$HERE}"   # the jh7110_*.cfg files sit beside this script
CFG_MAIN="${CFG_MAIN:-$PROJ/jh7110_daplink.cfg}"
CFG_SRST="${CFG_SRST:-$PROJ/jh7110_daplink_srst.cfg}"
CFG_WDT="${CFG_WDT:-$PROJ/jh7110_wdt_reset.cfg}"
CFG_WDT_OFF="${CFG_WDT_OFF:-$PROJ/jh7110_wdt_disable.cfg}"
CFG="${CFG:-}"

case "$RESET" in
  wdt|soft)   [[ -n "$CFG" ]] || CFG="$CFG_WDT" ;;
  srst|run)   [[ -n "$CFG" ]] || CFG="$CFG_SRST" ;;
  halt)       [[ -n "$CFG" ]] || CFG="$CFG_MAIN" ;;
  power|none) ;;
  *) echo "vf2-reset-to-uboot: RESET must be wdt/soft, srst, power, halt or run" >&2; exit 2 ;;
esac

OCD="${OPENOCD:-$(ls -d "$HOME"/.local/xPacks/@xpack-dev-tools/openocd/*/.content/bin/openocd 2>/dev/null | sort -V | tail -1)}"
OCD="${OCD:-openocd}"
SCRIPTS_ARG=()
if [[ "$OCD" == *"/xPacks/"* ]]; then
  SCRIPTS_ARG=(-s "$(dirname "$(dirname "$OCD")")/openocd/scripts")
fi

die () { echo "vf2-reset-to-uboot: $*" >&2; exit 1; }

[[ -e "$SERIAL" ]] || die "no such serial device: $SERIAL (for UART-less operation use $HERE/vf2-jtag-stop-uboot.sh)"
[[ "$SECS" =~ ^[0-9]+$ ]] || die "seconds must be a number"
if [[ "$RESET" != power && "$RESET" != none ]]; then
  [[ -f "$CFG" ]] || die "no such OpenOCD config: $CFG"
  # Fail fast instead of sending 10 s of keys to a probe that is not there.
  if command -v lsusb >/dev/null 2>&1; then
    if ! lsusb | grep -qiE '0d28:0204|d6e7:3507'; then
      die "no CMSIS-DAP probe in lsusb (0d28:0204 / d6e7:3507); replug the DAPLink"
    fi
  fi
fi

if command -v fuser >/dev/null 2>&1; then
  HOLDERS="$(fuser "$SERIAL" 2>/dev/null || true)"
  if [[ -n "${HOLDERS// /}" ]]; then
    echo "note: $SERIAL is also open by PID(s):$HOLDERS"
    echo "      (the key sender can still write; the other program shows output)"
  fi
fi

stty -F "$SERIAL" "$BAUD" raw -echo -echoe -echok -echoctl -echoke \
  clocal -crtscts 2>/dev/null \
  || die "cannot configure $SERIAL (permissions? close the terminal?)"

echo "reset-to-uboot: starting the key stream on $SERIAL"
KEY_STOP="$(mktemp -t vf2-keys.XXXXXX)"
rm -f "$KEY_STOP"
(
  while [[ ! -e "$KEY_STOP" ]]; do
    printf '%b' "$KEY" > "$SERIAL" 2>/dev/null || break
    sleep 0.05
  done
) &
KEY_PID=$!

stop_keys () {
  touch "$KEY_STOP" 2>/dev/null || true
  wait "$KEY_PID" 2>/dev/null || true
  rm -f "$KEY_STOP"
}

sleep 0.2

case "$RESET" in
  wdt|soft)
    echo "reset-to-uboot: watchdog soft reset (no reset pin needed)"
    echo "                cfg $CFG"
    WDT_LOG="$(mktemp -t vf2-wdt.XXXXXX)"
    "$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG" 2>&1 | tee "$WDT_LOG"
    rc=${PIPESTATUS[0]}
    if (( rc == 0 )) && ! grep -q "watchdog armed" "$WDT_LOG"; then
      echo "reset-to-uboot: the watchdog was NOT armed (JTAG problem?)." >&2
      echo "                If Linux owns the JTAG pins, power-cycle first and" >&2
      echo "                use RESET=power for the next attempt." >&2
      rc=1
    fi
    rm -f "$WDT_LOG"
    if (( rc == 0 )); then
      # Keep the keys flowing through the watchdog reset and U-Boot's
      # countdown. Only then stop and disarm the always-on watchdog.
      echo "reset-to-uboot: reset armed; keeping keys for ${WDT_WAIT:-10}s"
      sleep "${WDT_WAIT:-10}"
      stop_keys
      echo "reset-to-uboot: disarming the WDT..."
      for _try in $(seq 1 15); do
        if "$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG_WDT_OFF" >/dev/null 2>&1; then
          echo "reset-to-uboot: WDT disabled"
          break
        fi
        sleep 2
      done
    else
      stop_keys
    fi
    ;;
  srst|run)
    echo "reset-to-uboot: SRST reset (RST must be wired to the board reset net, NOT pin 36)"
    echo "                cfg $CFG"
    "$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG" \
      -c 'init' \
      -c 'reset run' \
      -c 'shutdown'
    rc=$?
    sleep "$SECS"
    stop_keys
    ;;
  halt)
    echo "reset-to-uboot: reset halt + resume through the RISC-V DM"
    echo "                (does not reboot the SoC on this board)"
    "$OCD" "${SCRIPTS_ARG[@]}" -f "$CFG" \
      -c 'init' \
      -c 'reset halt' \
      -c 'resume' \
      -c 'shutdown'
    rc=$?
    sleep "$SECS"
    stop_keys
    ;;
  power|none)
    echo "reset-to-uboot: no OpenOCD reset -- power-cycle / press RESET now."
    echo "                keys are already flowing; you have ${SECS}s."
    rc=0
    sleep "$SECS"
    stop_keys
    ;;
esac

if (( rc != 0 )); then
  echo "vf2-reset-to-uboot: OpenOCD returned $rc" >&2
  exit "$rc"
fi

echo
echo "done. Check the serial terminal for the 'StarFive #' prompt."
if [[ "$RESET" == wdt || "$RESET" == soft ]]; then
  echo "If the board kept resetting: run"
  echo "  openocd -f $CFG_WDT_OFF"
  echo "or power-cycle it."
elif [[ "$RESET" == srst || "$RESET" == run ]]; then
  echo "If Linux still booted: verify RST/SRST is wired to the board reset net (not pin 36), or use"
  echo "  RESET=power $0 $SERIAL $SECS"
fi

#!/bin/bash
# run.sh: every hwd test of the Pico (RP2040), Raspberry Pi Debug Probe: the 18
# automatic ones, then the two USB tests driven from here (Escape ends both):
# cdc-acm through its NEW /dev/ttyACM (picked by vendor id cafe, never by
# name), usb-hid through pyusb HID OUTPUT reports on cafe:4012, EP 0x01.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18g
B=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/cortexm-pico-cmake-gcc-debug
cd $B
ctest -L hwd -E 'smp-test-usb-(cdc-acm|hid)-hwd$' --output-on-failure > $H/pico-batch.txt 2>&1

before=" $(ls /dev/ttyACM* 2>/dev/null | tr '\n' ' ') "
echo "ports before:$before" > $H/cdc-acm-host.txt
ctest -L hwd -R 'smp-test-usb-cdc-acm-hwd$' --output-on-failure > $H/cdc-acm.txt 2>&1 &
CT=$!; P=""
for i in $(seq 1 90); do
  for d in /dev/ttyACM*; do
    case "$before" in *" $d "*) continue;; esac
    [ -e "$d" ] && [ "$(udevadm info -q property -n $d | grep '^ID_VENDOR_ID=' | cut -d= -f2)" = cafe ] && P=$d
  done
  [ -n "$P" ] && break; kill -0 $CT 2>/dev/null || break; sleep 1
done
if [ -n "$P" ]; then
  echo "board port $P ($(udevadm info -q property -n $P | grep '^ID_MODEL=' | cut -d= -f2)) after $i s" >> $H/cdc-acm-host.txt
  # One open of the board's own port (vendor cafe), raw: the board takes input
  # only while a host holds the port open (DTR=1), and a shell redirect opens
  # and closes it per write, with the PC's line discipline echoing in between.
  sleep 2; python3 -s -c "
import os, tty, time
fd = os.open('$P', os.O_RDWR | os.O_NOCTTY); tty.setraw(fd); time.sleep(1)
os.write(fd, b'hello from the host\\r'); time.sleep(1)
os.write(fd, b'\\x1b\\r'); time.sleep(1); os.close(fd)"
  echo "sent a line, then Escape" >> $H/cdc-acm-host.txt
else echo "no board port appeared" >> $H/cdc-acm-host.txt; fi
wait $CT

ctest -L hwd -R 'smp-test-usb-hid-hwd$' --output-on-failure > $H/usb-hid.txt 2>&1 &
CT=$!
for i in $(seq 1 90); do lsusb -d cafe:4012 >/dev/null && break; kill -0 $CT 2>/dev/null || break; sleep 1; done
sleep 2
python3 -s - > $H/usb-hid-host.txt 2>&1 <<'PY'
import usb.core, usb.util, time
dev = usb.core.find(idVendor=0xCAFE, idProduct=0x4012)
if dev is None: raise SystemExit("cafe:4012 not found")
if dev.is_kernel_driver_active(0): dev.detach_kernel_driver(0)
usb.util.claim_interface(dev, 0)
def send(s):
    b = bytes([len(s)]) + s; dev.write(0x01, b + bytes(64 - len(b)), timeout=2000); print("sent", s)
send(b"hello from the host"); time.sleep(1); send(b"\x1b")
usb.util.release_interface(dev, 0)
PY
wait $CT

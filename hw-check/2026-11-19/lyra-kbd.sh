#!/bin/bash
# lyra-kbd.sh: the Lyra's two keyboard tests (USB1 / OTG1), one after another, driven from here.
# smp_test_int3 / int4: the verdict is the host configuring gadget 2207:001B;
#   a few key records ('K', value, code LE16, seq LE32 on the bulk OUT EP) are
#   sent with pyusb so the logging path runs too.
# smp_test7: 256 KiB to gadget 2207:0019 with smp_test6's usb_test_host --big;
#   then the user answers the board's y/n on its UART.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-19
B=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/aarch32-luckfox-lyra-cmake-gcc-debug
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/platforms/aarch32-luckfox-lyra/tests
cd $B
for t in smp_test_int3 smp_test_int4; do
  ctest -L hwd -R "^aarch32-luckfox-lyra-$t-hwd\$" --output-on-failure > $H/lyra-$t.txt 2>&1 &
  CT=$!
  for i in $(seq 1 150); do lsusb -d 2207:001b >/dev/null && break; kill -0 $CT 2>/dev/null || break; sleep 1; done
  echo "gadget 2207:001b after $i s: $(lsusb -d 2207:001b)" > $H/lyra-$t-host.txt
  sleep 3
  python3 -s - >> $H/lyra-$t-host.txt 2>&1 <<'PY'
import usb.core, usb.util, struct, time
dev = usb.core.find(idVendor=0x2207, idProduct=0x001B)
if dev is None: raise SystemExit("2207:001b not found")
for i in range(dev.get_active_configuration().bNumInterfaces):
    try:
        if dev.is_kernel_driver_active(i): dev.detach_kernel_driver(i)
    except Exception: pass
cfg = dev.get_active_configuration(); intf = cfg[(0, 0)]
ep = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
usb.util.claim_interface(dev, intf.bInterfaceNumber)
seq = 0
for code in (30, 48, 46):            # a, b, c
    for value in (1, 0):             # down, up
        seq += 1; ep.write(struct.pack('<cBHI', b'K', value, code, seq), timeout=2000)
        time.sleep(0.2)
print("sent", seq, "key records on EP", hex(ep.bEndpointAddress))
usb.util.release_interface(dev, intf.bInterfaceNumber)
PY
  wait $CT
done

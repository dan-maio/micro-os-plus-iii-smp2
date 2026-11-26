# Reset the VisionFive 2 Lite to the U-Boot prompt

This is the reset/key workflow used with the Eclipse projects in this
workspace. It resets the board and stops U-Boot before Linux starts.

The board’s QSG says: *“When you power on VisionFive 2 Lite, the serial port
will print countdown, usually starting from 3. Hit any key to stop autoboot
before the number decreases to 0.”*

## Files

| file | purpose |
|---|---|
| `vf2-reset-to-uboot.sh` | UART key sender + watchdog/SRST/power reset |
| `vf2-jtag-stop-uboot.sh` | reset + JTAG-only U-Boot stop, no UART cable |
| `jh7110_wdt_reset.cfg` | **software reset** through the JH7110 watchdog |
| `jh7110_wdt_disable.cfg` | stop a watchdog left running after the reset |
| `jh7110_jtag_key.cfg` | inject a U-Boot key through UART0 loopback (JTAG only) |
| `jh7110_daplink_srst.cfg` | SRST enabled — only if wired to the board reset net, **not** pin 36 |
| `jh7110_daplink.cfg` | the normal JTAG config (diagnostics) |
| `vf2-reset-to-uboot-picoprobe.sh`, `vf2-jtag-stop-uboot-picoprobe.sh` | the same scripts for the "JTAG probe on Pico" (2e8a:000c): they use the `*_picoprobe*.cfg` copies below and check `lsusb` for 2e8a:000c |
| `jh7110_picoprobe*.cfg`, `jh7110_*_picoprobe.cfg` | the configs above for the JTAG probe on Pico (CMSIS-DAP v2, USB bulk); only the probe lines differ. With the Pico 2's Debug Probe (same VID:PID) also plugged in, set `PICOPROBE_SERIAL=E4623492931D2230` |
| `vf2-j20.png` | crop of the QSG showing the J20 fastboot pins |

The scripts take the `jh7110_*.cfg` files from their own directory. To use
the copies in an Eclipse project instead, point `PROJ` at it, for example
`PROJ=~/Workspaces/milk-v/mmu-test-vf2 ./vf2-reset-to-uboot.sh /dev/ttyUSB0`.

---

## Method 1 — watchdog soft reset with UART keys (default)

This is the default for `vf2-reset-to-uboot.sh`. It needs the CH340 UART cable
(`/dev/ttyUSB0`).

```bash
cd micro-os-plus-iii-riscv     # the repo root: the scripts and their cfgs
./vf2-reset-to-uboot.sh /dev/ttyUSB0
```

What it does:

1. starts sending `CR` on UART0 every 50 ms;
2. arms the JH7110 watchdog from JTAG (`jh7110_wdt_reset.cfg`);
3. the SoC resets after roughly 1–4 s;
4. keys continue for 10 s after arming, through the boot ROM, SPL, OpenSBI
   and U-Boot;
5. the script tries to attach again and disable the watchdog
   (`jh7110_wdt_disable.cfg`).

U-Boot stops at:

```text
StarFive #
```

The JH7110 watchdog is ARM-SP805-like and locked by default; the config uses
the key from the mainline `starfive-wdt.c` driver:

```text
0x13070000  WDOGLOAD
0x13070008  WDOGCONTROL   bit0 enable, bit1 reset enable
0x1307000C  WDOGINTCLR
0x13070C00  WDOGLOCK      write 0x1ACCE551 to unlock
```

The count is deliberately short (~2 s at 24 MHz, double-timeout behaviour);
the script waits 10 s after arming before disarming.

---

## Method 2 — SRST through the JTAG probe (only with a real reset net)

This is **not** pin 36. On both the VisionFive 2 and the VisionFive 2 Lite,
40-pin header pin 36 is **GPIO36**, not reset. The full VisionFive 2 has a
separate Reset button; the Lite has no reset button.

Wire the probe’s **RST/SRST** to the board’s actual reset net (for example
the full VisionFive 2 Reset-button net, or a documented reset test point).
Then:

```bash
RESET=srst ./vf2-reset-to-uboot.sh /dev/ttyUSB0 15
```

This uses `jh7110_daplink_srst.cfg`:

```tcl
reset_config srst_only srst_nogate
```

OpenOCD asserts SRST (a real board reset), releases it, and the UART key
stream is already running. This is the cleanest method if you can add the
wire.

---

## Method 3 — no reset source: power-cycle while keys are sent

```bash
RESET=power ./vf2-reset-to-uboot.sh /dev/ttyUSB0 15
```

The script starts sending keys and prints:

```text
reset-to-uboot: no OpenOCD reset -- power-cycle / press RESET now.
```

Power-cycle the board during the window. U-Boot sees the keys and stops.

---

## Method 4 — JTAG only: inject the U-Boot key through UART loopback

This method needs **no UART cable and no external key sender**. It is the
fallback when `/dev/ttyUSB0` is missing (as on the current setup, where only
the DAPLink is present).

The JH7110 UART0 is a DesignWare 8250 at `0x10000000` (reg-shift 2):

```text
0x10000000  THR / RBR
0x10000010  MCR   bit 4 = internal loopback
0x10000014  LSR   bit 0 = data ready
```

Setting loopback and writing `0x0D` to THR makes the UART receive that byte
internally. Clearing loopback leaves it in the RX FIFO, so U-Boot’s
`tstc()`/`getc()` sees a key and aborts autoboot.

Measured on the board:

```text
before: LSR = 0x60
after:  LSR = 0x61, RBR = 0x0D
```

Run:

```bash
cd micro-os-plus-iii-riscv     # the repo root: the scripts and their cfgs
./vf2-jtag-stop-uboot.sh
```

It:

1. arms the watchdog to reset the SoC;
2. as soon as JTAG is back, halts the boot hart, injects a CR via the UART
   loopback, and resumes — **5 attempts by default**;
3. stops U-Boot at its prompt;
4. tries to disarm the watchdog.

Override the attempt count with:

```bash
KEY_TRIES=10 ./vf2-jtag-stop-uboot.sh
```

This script needs **no** `/dev/ttyUSB0`. The other script,
`vf2-reset-to-uboot.sh`, does need the CH340 UART because it sends the key
externally; if `/dev/ttyUSB0` is missing, use this JTAG-only script.

The `StarFive #` prompt itself is a UART **output**. Without a UART cable you
cannot see it, but U-Boot is waiting at the prompt and Linux will not start.
Connect UART only if you want to see the prompt; no key is sent over it.

If your goal is simply to stop the boot and load a new image, you can also
halt the U74s via JTAG immediately after the reset and skip the key entirely;
the board is stopped before Linux, which is all the debugger needs.

---

## Why `reset halt` / `reset run` are not enough

Measured on the VisionFive 2 Lite v1.1:

```text
U74 PC before reset: 0x40204f8e
U74 PC after  reset halt + resume: 0x40204f8e
U74 PC after  reset run:           0x40204f8e
```

OpenOCD’s RISC-V Debug Module reset only halts/resumes the U74 harts; it does
**not** reboot the SoC. The JH-7110S Bare-Metal notes say the same: `reset
halt` resets the core logic and halts, ready to load new code — it is not a
board power-on reset.

The real reset sources are:

- the **JH7110 watchdog** — Method 1;
- the **pad reset** — only where a Reset button/reset net exists (full
  VisionFive 2); **not** 40-pin pin 36 (that is GPIO36);
- **power cycling** — Method 3.

---

## Troubleshooting

| symptom | check |
|---|---|
| `no such serial device: /dev/ttyUSB0` | the CH340 UART is missing; use `vf2-jtag-stop-uboot.sh` (JTAG-only) or reconnect the UART cable. |
| `unable to find a matching CMSIS-DAP device` | the DAPLink is not enumerated; unplug/replug it; check `lsusb` for `0d28:0204` or `d6e7:3507`. |
| DAPLink reports `CMSIS-DAP command mismatch` | the probe hung after a SoC reset; unplug/replug the DAPLink. |
| Board keeps rebooting after `wdt` | run `openocd -f .../jh7110_wdt_disable.cfg`, or power-cycle. |
| Linux still boots | keys are too late or the reset source did not fire; use `RESET=power` and power-cycle during the window. |
| No `StarFive #` | the prompt is UART output; connect UART only for output, or use the JTAG-only method. |
| OpenOCD scan-chain warning | the first TAP ID warning (`0x20000c2f` vs `0x07110cfd`) is expected on this board; debugging works. |

---

## After you have the prompt

Once at `StarFive #`, the board is ready for the Eclipse debug launch
(`load_image` from OpenOCD/GDB), exactly as `hw.sh` expects.

---

# Boot structure of the VisionFive 2 Lite

This is why a reset must restart the whole boot chain (BootROM → SPL →
OpenSBI/U-Boot), and why the key must be sent immediately.

## Short version

```text
Power on
   │
   ▼
BootROM (32 KiB, 0x2A00_0000)
   │  selects QSPI / SD / eMMC / UART
   ▼
U-Boot SPL        (SRAM, then DDR init)
   │  loads OpenSBI + U-Boot FIT
   ▼
OpenSBI (M-mode) + U-Boot (S-mode, 0x4020_0000)
   │  reads EEPROM for board info; env from QSPI
   ▼
Linux             (rootfs on SD or eMMC)
```

## The devices

| device | what it is | what it does |
|---|---|---|
| **BootROM** | 32 KiB mask ROM inside the JH7110 | first code after reset; picks the boot medium and loads SPL |
| **QSPI NOR flash** | 16 MiB SPI flash (`gd25lq128` on VF2) | **boot firmware**: SPL, U-Boot environment, OpenSBI + U-Boot FIT; the default boot source |
| **EEPROM** | AT24C04F, 512 bytes, **I²C** | **board identity only**: vendor, serial, PCB/BOM revision, MAC addresses; read by U-Boot, not a boot medium |
| **microSD** | removable SD card | OS/rootfs; in vendor boot modes also SPL/U-Boot; SD boot is deprecated in recent upstream U-Boot |
| **eMMC** | soldered flash on eMMC-equipped Lite models | OS/rootfs; BootROM can load SPL from it in vendor mode; shares the SD controller path, so the TF slot is often omitted |
| **M.2 NVMe SSD** | PCIe 2.0 x1 M-key socket, 2242 | OS/data; not a BootROM device |
| **UART / USB fastboot** | recovery paths | Boot button → UART XMODEM recovery; J20 jumper → USB fastboot for flashing eMMC |
| **DDR / SRAM** | volatile RAM | SPL runs from SRAM/L2 LIM, initializes DDR; OpenSBI/U-Boot/Linux run from DDR |
| **JTAG** | debug port | not a boot medium; used to load/debug images once the board is running |

## QSPI flash — the boot firmware

The layout used by U-Boot:

```text
offset       size        content
0x000000     0x0F0000    u-boot-spl.bin.normal.out   (SPL)
0x0F0000     0x010000    U-Boot environment
0x100000     0xF00000    u-boot.itb                  (OpenSBI + U-Boot)
```

The QSPI flash contains the **boot chain**, not the Linux root filesystem. It
survives power loss and is why a board with a blank SD/eMMC can still reach
the U-Boot prompt.

The SPL is what trains LPDDR4 and sets up clocks; that is why bare-metal/JTAG
workflows depend on the board having booted through SPL at least once.

## EEPROM — not boot storage

The EEPROM is an **I²C** AT24C04F, 512 bytes. U-Boot reads a “StarFive EEPROM
format v2” structure:

- vendor name
- product serial number
- PCB revision
- BOM revision
- Ethernet MAC0 / MAC1 addresses
- board/model information

U-Boot uses this to select the correct device tree / board revision and to
program the MAC addresses. It does **not** contain SPL, U-Boot, OpenSBI or
Linux.

## microSD and eMMC — the OS media

Vendor boot layout on SD uses GPT with special type GUIDs:

```text
partition 1  SPL              type 2E54B353-1271-4842-806F-E436D6AF6985
partition 2  U-Boot/OpenSBI   type BC13C2FF-59E6-4262-A352-B275FD6F7172
partition 3+ rootfs
```

eMMC is the soldered equivalent. A subtle difference:

- **SD boot**: BootROM reads the GPT header and finds the SPL partition by GUID.
- **eMMC boot**: BootROM loads SPL from **sector 0** in vendor mode, not from a
  GPT partition; a GPT on eMMC can fail with a CRC error.

Recent upstream U-Boot has deprecated SD/eMMC boot and recommends QSPI flash
+ UART recovery, but vendor images and the Lite’s factory firmware still use
the SD/eMMC paths.

## Boot mode selection

The JH7110 BootROM chooses the source from the boot-strap pins
`RGPIO2:RGPIO0`:

| RGPIO1 | RGPIO0 | BootROM / SPL source |
|---|---|---|
| 0 | 0 | QSPI flash offset 0 (normal) |
| 0 | 1 | SDIO 3.0 / SD card (vendor, deprecated upstream) |
| 1 | 0 | eMMC 5.0/5.1 (vendor, deprecated upstream) |
| 1 | 1 | UART XMODEM loader (recovery) |

On the **VisionFive 2 Lite** this is exposed as:

- **normal power-on** → QSPI flash boot;
- **hold the Boot button while powering** → UART XMODEM recovery (DDR is not
  initialized in this mode);
- **J20 jumper** → USB fastboot, used to flash the eMMC.

## J20 — the fastboot jumper

**J20 is the 2-pin fastboot header**, not a normal always-fitted jumper.

- Short both pins **before power-on** → BootROM enters **USB fastboot mode**.
- The USB-C port enumerates as **USB Download Gadget**; you can flash the
  onboard **eMMC** from the host.
- Power off, remove the jumper, and boot normally.

It is not the same as the **Boot/Recovery button**:

| control | effect |
|---|---|
| J20 fastboot header | force USB fastboot for eMMC flashing |
| Boot button (separate) | hold at power-on for UART XMODEM recovery |
| neither | normal QSPI boot → SPL → U-Boot |

Location: **top edge of the board, immediately left of the upper USB-A port**,
next to the PoE header area and a mounting hole; silkscreen **J20**. See the
QSG Figure 5-4; a crop is saved at `vf2-j20.png`.

Leave J20 **open** for normal boot and JTAG/debug use. If it is shorted, the
board will not boot QSPI/U-Boot normally.

## Where the JTAG-loaded program fits

The Eclipse/OpenOCD flow does **not** use any storage boot path for the
payload:

```text
board already booted to U-Boot
        │
        ├─ SPL has trained DDR and configured UART0
        ├─ OpenSBI/U-Boot are running
        │
        └─ OpenOCD loads the ELF directly into DDR at 0x4020_0000
           and sets the U74 PC there
```

That is why:

- the board must be **started** (at U-Boot) before JTAG works — SPL/U-Boot
  has set up DDR and the pin mux;
- the QSPI/SD/eMMC images are untouched by the debug session;
- a reset must restart the boot chain from QSPI (or SD/eMMC) — the RISC-V
  Debug Module reset alone does not do that;
- the watchdog/soft reset makes the BootROM run again, which is why keys
  must be sent immediately to stop U-Boot before Linux starts.

---

## DDR, UART and JTAG: which stage brings each one up

| resource | first brought up by | who uses/enables it later |
|---|---|---|
| **DDR** | **U-Boot SPL** | SPL loads OpenSBI + U-Boot into DDR; U-Boot and Linux run from it |
| **UART0** | **BootROM** in UART-recovery mode; otherwise SPL/U-Boot | OpenSBI console; U-Boot console at 115200 8N1 |
| **JTAG** | the SoC debug module, active from reset | not “enabled” by U-Boot; the header pin mux and DDR determine when it is useful |

So:

- **DDR is not initialized by the BootROM, and not by U-Boot proper.** It is
  trained by **U-Boot SPL**.
- **UART is used by the BootROM** for recovery (XMODEM), and by
  **OpenSBI/U-Boot** for the normal console. In normal boot, the 115200
  console is set up by **U-Boot**.
- **JTAG is not “enabled by U-Boot.”** The RISC-V debug module/TAP is part of
  the SoC and is available early; what is missing early is DDR (and sometimes
  the header pin mux). The port’s `hw.sh` attaches at U-Boot because by then
  DDR is trained and UART0 is configured.

### Stage by stage

```text
Power on
   │
   ▼
BootROM (32 KiB mask ROM)
   ├─ selects QSPI / SD / eMMC / UART by boot straps
   ├─ loads SPL into SRAM / L2 LIM
   ├─ UART recovery mode: configures UART0 and speaks XMODEM
   └─ does NOT train DDR

   ▼
U-Boot SPL
   ├─ configures PLLs and the LPDDR4/DDR controller + PHY
   ├─ DDR is now usable at 0x4000_0000 (uncached alias 0x2_4000_0000)
   ├─ loads OpenSBI + U-Boot FIT from QSPI/MMC
   └─ optional debug UART, but normally no console by default

   ▼
OpenSBI (M-mode)
   ├─ runs from DDR at 0x4000_0000
   ├─ UART console / SBI services
   └─ jumps to U-Boot in S-mode at 0x4020_0000

   ▼
U-Boot (S-mode)
   ├─ initializes UART0 console at 115200 8N1
   ├─ reads EEPROM (board info, MACs, DT selection)
   ├─ loads environment from QSPI
   ├─ initializes MMC/USB/network
   └─ autoboot → Linux
```

### DDR

- The BootROM executes from its 32 KiB ROM and loads SPL into on-chip SRAM
  (`0x0800_0000`); it does not need external DDR.
- **SPL trains LPDDR4** and sets up the DDR controller/PHY and PLLs.
- Only then can OpenSBI (at `0x4000_0000`) and U-Boot (at `0x4020_0000`) be
  loaded into DDR.
- That is why the Eclipse/OpenOCD flow says: **board must be at the U-Boot
  prompt** before loading an ELF at `0x4020_0000`. JTAG may be electrically
  alive earlier, but writes to DDR fail until SPL has trained it.

### UART

- **BootROM**: in UART recovery mode (Boot button / `RGPIO1:0 = 3`), it
  configures UART0 and prints the `(C)StarFive`/`CCC` XMODEM prompt.
- **SPL**: can have a debug UART, but the standard VisionFive2 SPL does not
  normally print; U-Boot’s documentation notes that SPL debug UART is not
  enabled by default when U-Boot Main uses the SBI console.
- **OpenSBI**: uses UART0 for its console output.
- **U-Boot**: initializes UART0 as the console (`serial@10000000`, ns16550,
  reg-shift 2) at 115200 8N1.
- The port’s `uart::init()` deliberately does **not** reprogram the divisor;
  it relies on U-Boot having set UART0 up.

### JTAG

- The RISC-V Debug Module/TAP is a SoC block; it is **not “started” by
  U-Boot**.
- The 40-pin header JTAG function and the debug module are normally available
  from reset (unless secure-debug/OTP disables them).
- However, a useful “load and run from DDR” session needs:
  - the JTAG pins muxed to the header,
  - **DDR trained by SPL**,
  - UART0 configured if you want console output.
- That is why the port’s `hw.sh` and Eclipse launches require the board to be
  stopped at **U-Boot**: by then SPL has done DDR and U-Boot has done UART.

So a JTAG attach can succeed earlier, but don’t expect to load a DDR image
until SPL has run. That is also why the JTAG-only key injection
(`vf2-jtag-stop-uboot.sh`) works once SPL/U-Boot is up: the UART and DDR are
already initialized, and JTAG only needs to halt the hart and poke the UART
loopback register.

---

# Installing / updating QSPI, eMMC and M.2

Three different storage devices, three different jobs:

| device | what belongs there | how you install/update it |
|---|---|---|
| **QSPI NOR** | boot firmware only: SPL, U-Boot env, OpenSBI + U-Boot FIT | U-Boot `sf`, Linux `flashcp`, or UART XMODEM recovery |
| **eMMC** | OS image / rootfs | USB fastboot (J20 or `fastboot usb 0`), or `dd` from a running Linux |
| **M.2 NVMe SSD** | OS / data | write the image to `/dev/nvme0n1` from Linux, or flash it in a USB-NVMe adapter |

Do **not** put a Linux image on QSPI. It is only 16 MiB and holds the boot
chain.

## QSPI NOR — update the boot firmware

QSPI layout:

```text
0x000000  SPL              (u-boot-spl.bin.normal.out)
0x0F0000  U-Boot env
0x100000  OpenSBI + U-Boot (u-boot.itb)
```

From U-Boot at `StarFive #`:

```text
sf probe
loady && sf update $loadaddr 0 $filesize
env erase
loady && sf update $loadaddr 100000 $filesize
```

- first `loady`: send `u-boot-spl.bin.normal.out` over YMODEM
- second `loady`: send `u-boot.itb` over YMODEM

From Linux running on the board:

```bash
cat /proc/mtd
# mtd0 "spl", mtd1 "uboot-env", mtd2 "uboot"

flashcp -v u-boot-spl.bin.normal.out /dev/mtd0
flashcp -v --erase-all /dev/zero /dev/mtd1
flashcp -v u-boot.itb /dev/mtd2
```

Recovery if QSPI is broken: hold the **Boot button** while powering on to
enter BootROM UART XMODEM mode, then send SPL with XMODEM-1K and
`u-boot.itb` with YMODEM on UART0.

**Caution:** never `dd` an OS image to `/dev/mtd*`.

## eMMC — install/update the OS

The eMMC version is flashed over **USB-C using fastboot**. Enter fastboot by:

- shorting the **J20** pins before power-on, or
- at the U-Boot prompt:

```text
fastboot usb 0
```

From an Ubuntu/Linux host:

```bash
sudo apt install fastboot android-sdk-libsparse-utils

# sparse image makes the transfer faster and more reliable
img2simg StarFive-...-vf2-lite.img image.simg

fastboot devices -l
fastboot flash mmc0 image.simg
```

Then power off, remove the J20 jumper, and boot normally.

From Windows, use StarFive’s **SFFB_Tool**: install the `usb_driver` from
the package, put the board in fastboot mode, select **Image**, and click
**Run/Start All**. The board appears as **USB Download Gadget**.

Alternative from a running Linux booted from SD:

```bash
lsblk -o NAME,SIZE,REMOVABLE,MODEL
# eMMC: REMOVABLE=0; SD: REMOVABLE=1

sudo dd if=StarFive-...-vf2-lite.img of=/dev/mmcblkX bs=4M \
  status=progress conv=fsync
sync
```

Make sure `/dev/mmcblkX` is the eMMC, not the SD card or a USB stick.

## M.2 NVMe SSD — install/update the OS

The Lite has an **M.2 M-Key, 2242** socket (PCIe 2.0 x1). It is ordinary
NVMe storage, not a BootROM device.

Physical install:

1. power off and unplug;
2. insert a 2242 M.2 NVMe SSD;
3. secure it with the recommended **M2 × 3 mm** (or M2 × 2.5 mm) screw;
4. power on.

Write the image from a running Linux (SD/eMMC boot):

```bash
lsblk
# SSD usually appears as /dev/nvme0n1

sudo dd if=StarFive-...-vf2-lite.img of=/dev/nvme0n1 bs=4M \
  status=progress conv=fsync
sync
```

Or remove the SSD and flash it in a USB-NVMe adapter from your PC.

Booting from NVMe depends on the QSPI U-Boot:

- check for PCIe/NVMe support in U-Boot:
  ```text
  pci enum
  nvme scan
  nvme info
  ```
- if supported, set `boot_targets`/`extlinux` to the NVMe device;
- if not, keep the kernel/bootloader on SD/eMMC and put only the rootfs on
  NVMe, with:
  ```text
  root=PARTUUID=<nvme-root-partition-uuid> rootwait
  ```

If the SSD is not detected, check `lspci`/`nvme list` and U-Boot PCIe
initialization.

## Recommended order

1. make sure QSPI has a valid SPL + U-Boot (otherwise nothing boots);
2. install the OS on **eMMC** or **microSD**;
3. add the **M.2 NVMe** as data or rootfs; only move boot there if U-Boot
   supports NVMe;
4. update QSPI only when you need a new SPL/U-Boot, and keep the Boot-button
   UART recovery path available.

Always double-check the target device before `dd`/`flashcp`; writing to the
wrong one destroys the bootloader or the OS.

---

## Practical summary

- **QSPI NOR**: bootloader storage — SPL + env + OpenSBI/U-Boot. Keep it
  valid; it is the fallback that always boots.
- **EEPROM**: board passport — serial, PCB revision, MACs, DT selection. Not
  boot code.
- **microSD**: removable OS disk (and vendor boot partitions).
- **eMMC**: soldered OS disk on eMMC models; same controller family as SD.
- **M.2 NVMe**: PCIe storage; OS/data, boot only if U-Boot supports NVMe.
- **BootROM**: picks the medium; **UART/USB fastboot**: recovery.
- **JTAG**: debug only; loads your app into DDR after the firmware has
  initialized the board.

# Pico 2 (RP2350): ARM vs RISC-V, OpenOCD, and hardware test 4.4.12

**Board:** Raspberry Pi Pico 2 (RP2350, 2 × Cortex-M33 or 2 × Hazard3 RISC-V)  
**Probe:** Raspberry Pi Debug Probe, CMSIS-DAP v2 (`2e8a:000c`)  
**Test:** `4.4.12` = `micro-os-plus-iii-cortexm` → `cortexm-pico2` → `smp-mat-test-ram`  
**Workspace:** `/home/dan/Work/micro-os-scaffold-workspace`  
**Result:** the ARM test passes on hardware (`PASS`, ~3.1 s).

---

## 1. Summary

The RP2350 has two processor sockets. At reset each socket selects either its
ARM Cortex-M33 or its RISC-V Hazard3 core. The **unselected** cores are held in
reset, so they cannot be examined by a debugger.

Two consequences:

1. A debugger can only see the architecture currently selected by `ARCHSEL`.
2. The OpenOCD build must support that architecture.

On this board the ARM test first failed with:

```text
Info : SWD DPIDR 0x4c013477
Error: Failed to read memory at 0xe000ed00
Error: [rp2350.cm0] Examination failed
Error: [rp2350.cm1] Examination failed
```

The SWD link itself was healthy. The ARM cores were not available. An
**RP2350 rescue reset** brought the ARM cores back, after which the test passed.

---

## 2. The selected test

```
4      micro-os-plus-iii-cortexm
4.4    cortexm-pico2       (Raspberry Pi Pico 2 / RP2350)
4.4.12 smp-mat-test-ram    (parallel block matrix solver, RAM-resident)
```

`4.4.12` has a hardware-only image. There is no QEMU variant for the `-ram`
build, so the CTest case is:

```text
cortexm-pico2-smp-mat-test-ram-hwd
```

---

## 3. Minimal workspace for 4.4.12

The workspace contains only what is needed to build and run this test.

```text
/home/dan/Work/micro-os-scaffold-workspace/
├── README.md
├── scaffold.json
├── micro-os-plus-iii-smp/
│   ├── CMakeLists.txt, package.json
│   ├── cmake/, include/
│   └── src/                         only the kernel sources linked into the test
├── micro-os-plus-iii-cortexm/
│   ├── CMakeLists.txt, package.json
│   ├── include/, include-rp2350/
│   ├── src/
│   ├── soc/rp2350/                  only the RP2350 SoC support
│   └── test/
│       ├── CMakeLists.txt
│       ├── boards/shared/
│       ├── boards/pico2/
│       └── pico2/
│           ├── tests.cmake
│           ├── smp-mat-test/main.cpp      included by the RAM variant
│           └── smp-mat-test-ram/
│               ├── main.cpp
│               ├── linker-ram.ld
│               ├── load-ram.cfg
│               └── hw.sh
└── tests/
    ├── CMakeLists.txt, cmake/
    ├── platforms/cortexm-pico2/
    ├── xpacks/@micro-os-plus/build-helper/
    ├── scripts/env.sh
    ├── scripts/selected-tests.txt
    ├── build.sh, test.sh
    └── build/cortexm-pico2-cmake-gcc-debug/
```

Removed: all other ports, boards and tests, the other SoC families
(STM32F4, RP2040), the host/QEMU runners, documentation not needed for the
build, and the unused xPack libraries.

Two small CMake adaptations keep the helper source
`smp-mat-test/main.cpp` from being built as a second test:

- `micro-os-plus-iii-cortexm/test/CMakeLists.txt` skips `smp-mat-test`;
- `tests/platforms/cortexm-pico2/CMakeLists.txt` skips the same directory.

---

## 4. Hardware setup

### 4.1 Board and probe

- Raspberry Pi Pico 2 (**RP2350**, not Pico 1/RP2040).
- CMSIS-DAP probe: Raspberry Pi Debug Probe, DAPLink, or WCH-Link in ARM mode.
- The Pico 2 must be powered from its own USB port. The 3-pin debug
  connector on the Raspberry Pi Debug Probe carries **SWCLK, GND and SWDIO**;
  it does not power the target.

### 4.2 SWD wiring

| Probe | Pico 2 |
|---|---|
| SWDIO | SWDIO |
| SWCLK | SWCLK |
| GND | GND |
| 3V3 | only if the probe is designed to power the target; otherwise leave it |

### 4.3 UART console (optional)

| USB-UART adapter | Pico 2 |
|---|---|
| RX | GP0 (UART0 TX) |
| TX | GP1 (UART0 RX) |
| GND | GND |

The board console is **UART0, 115200 8N1**. The final `RESULT: PASS/FAIL` is
also sent through semihosting, so CTest can see the verdict without the UART.

---

## 5. Build

```bash
cd /home/dan/Work/micro-os-scaffold-workspace/tests
./build.sh cortexm-pico2-cmake-gcc-debug
```

Toolchain:

| Tool | Version | Path |
|---|---|---|
| arm-none-eabi-gcc/g++ | 15.2.1 | `~/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin/` |
| arm-none-eabi-gdb | 16.3.90 | same xPack |
| OpenOCD | 0.12.0+dev-02228 | `~/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd` |
| CMake | 4.4.4 | `/usr/bin/cmake` |
| Ninja | 1.13.2 | `/usr/bin/ninja` |

Built image:

```text
tests/build/cortexm-pico2-cmake-gcc-debug/platform-bin/port-tests/test/smp-mat-test-ram-hwd.elf
```

---

## 6. First hardware run: the failure

Command:

```bash
cd /home/dan/Work/micro-os-scaffold-workspace/tests
./test.sh cortexm-pico2-cmake-gcc-debug --hwd
```

Relevant output:

```text
Info : Using CMSIS-DAPv2 interface with VID:PID=0x2e8a:0x000c
Info : CMSIS-DAP: SWD supported
Info : CMSIS-DAP: Interface Initialised (SWD)
Info : clock speed 1000 kHz
Info : SWD DPIDR 0x4c013477
Info : SWD DPIDR 0x4c013477
Error: Failed to read memory at 0xe000ed00
Error: [rp2350.cm0] Examination failed
Warn : target rp2350.cm0 examination failed
Error: Failed to read memory at 0xe000ed00
Error: [rp2350.cm1] Examination failed
Warn : target rp2350.cm1 examination failed
Info : [rp2350.cm0] AP write error, reset will not halt
Error: [rp2350.cm0] DP initialisation failed
```

The test reported:

```text
smp-mat-test-ram         OPENOCD DIED
```

### What the log means

| Observation | Meaning |
|---|---|
| `SWD DPIDR 0x4c013477` | The probe, SWD wiring, ground and target DP are working. |
| `Failed to read memory at 0xe000ed00` | The AHB-AP cannot read the Cortex-M33 SCS (`CPUID`). |
| Both `cm0` and `cm1` fail | Both ARM cores are unavailable, not just one. |
| `AP write error, reset will not halt` | The debugger cannot reset the target through the ARM AP. |

`0xe000ed00` is the Cortex-M CPUID register. Being unable to read it while
`DPIDR` reads correctly is the classic signature of **the ARM cores being
held in reset or powered down** — typically because the RP2350 is running in
RISC-V mode, or because a previous run left the chip in a bad state.

This is not a build error, a probe error, or a wrong CTest selection.

---

## 7. Diagnosis: ARM APs exist, but the cores are not running

Low-level AP inspection:

```text
AP # 0x2000
    Peripheral ID 0x04003bb9e3
    Part is 0x9e3, SoC-600 AHB-AP (AHB5 Memory Access Port)
    MEM-AP BASE 0xe00ff003

AP # 0x4000
    Peripheral ID 0x04003bb9e3
    Part is 0x9e3, SoC-600 AHB-AP (AHB5 Memory Access Port)
    MEM-AP BASE 0xe00ff003
```

Both ARM AHB-APs respond, and their ROM tables are readable, but the CPU's
private peripheral bus (`0xe000ed00`) does not. The debug infrastructure is
present; the core domain is not.

---

## 8. The fix: RP2350 rescue reset

The RP2350 has a special rescue AP (`0x80000`). The rescue reset resets
everything except the DP and the rescue AP, and both Cortex-M33 cores stop in
the bootrom.

Create a deferred-examine configuration:

```tcl
# rescue.cfg
source [find interface/cmsis-dap.cfg]
adapter speed 500
set CHIPNAME rp2350
set USE_CORE {cm0 cm1}
set USE_SMP 0
source [find target/rp2350.cfg]
rp2350.cm0 configure -defer-examine
rp2350.cm1 configure -defer-examine
```

Run:

```bash
OCD=~/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/bin/openocd
SCRIPTS=~/.local/xPacks/@xpack-dev-tools/openocd/0.12.0-7.1/.content/openocd/scripts

"$OCD" -s "$SCRIPTS" -f rescue.cfg \
  -c "init" \
  -c "rescue_reset" \
  -c "sleep 300" \
  -c "rp2350.cm0 arp_examine" \
  -c "mdw 0xE000ED00 1" \
  -c "shutdown"
```

Result:

```text
Info : SWD DPIDR 0x4c013477
Info : [rp2350.cm0] Cortex-M33 r1p0 processor detected
Info : [rp2350.cm0] target has 8 breakpoints, 4 watchpoints
0xe000ed00: 411fd210
```

The ARM core is back. The architecture registers then read:

```text
0x40120158: 00000000     # ARCHSEL
0x4012015c: 00000000     # ARCHSEL_STATUS → ARM + ARM
0x40000000: 20004927     # chip ID
```

---

## 9. Successful hardware run

After the rescue reset the normal test works:

```bash
cd /home/dan/Work/micro-os-scaffold-workspace/tests
./test.sh cortexm-pico2-cmake-gcc-debug --hwd
```

Output:

```text
Info : [rp2350.cm1] Examination succeed
Info : [rp2350.cm0] halted due to debug-request
Info : [rp2350.cm1] halted due to debug-request
Info : RP2350 rev 2, QSPI Flash size override = 4096 KiB in 1024 sectors
40652 bytes written at address 0x20000000
downloaded 40652 bytes in 0.599615s
verified 40652 bytes in 0.302817s
semihosting is enabled
semihosting is enabled
0x20062e68 0x20000149
sp (/32): 0x20062e68
pc (/32): 0x20000148
RESULT: PASS

smp-mat-test-ram         PASS  (3s)
Power-cycle the board before the next test.
1/1 Test #1: cortexm-pico2-smp-mat-test-ram-hwd ...   Passed    3.11 sec
```

The test is RAM-resident: it loads at `0x20000000` and does not write QSPI
flash. OpenOCD programs the image, starts core 1, sets core 0's VTOR/SP/PC and
resumes; the verdict arrives through semihosting.

---

## 10. ARM vs RISC-V on the RP2350

### 10.1 Architecture selection

The RP2350 has two processor sockets. Each socket selects ARM Cortex-M33 or
RISC-V Hazard3 at reset, according to `ARCHSEL`.

| Register | Address | Meaning |
|---|---|---|
| `ARCHSEL` | `0x40120158` | architecture selection control |
| `ARCHSEL_STATUS` | `0x4012015c` | architecture sampled at reset |

Observed encoding:

| Value | Architecture |
|---|---|
| `0` | ARM + ARM |
| `3` | RISC-V + RISC-V |

`ARCHSEL` is a writable control register in the OTP controller block, not an
OTP fuse. The bootrom selects the architecture from the boot image: an ARM
image selects ARM, a RISC-V image selects RISC-V. The unselected cores are
held in reset.

### 10.2 OpenOCD builds

The physical probe is the same. The OpenOCD build and target configuration
must match the current architecture.

| Board state | OpenOCD needed | Target configuration |
|---|---|---|
| ARM Cortex-M33 | xpack OpenOCD `0.12.0+dev-02228` works | `USE_CORE {cm0 cm1}` |
| RISC-V Hazard3 | Raspberry Pi OpenOCD fork or Pico SDK OpenOCD | `USE_CORE {rv0 rv1}` |

The xpack OpenOCD used here supports the ARM path, as proven by the passing
test. Its RISC-V target is not usable with the RP2350 configuration:

```text
target/rp2350.cfg:102: Error: Unknown param: -dap
    target create rp2350.rv0 riscv -dap rp2350.dap -ap-num 0xa000 ...
```

For RISC-V use the Raspberry Pi fork of OpenOCD (the one shipped with the
Pico SDK and the VS Code Pico extension). It supports both `cm0/cm1` and
`rv0/rv1` and the architecture-switch logic.

### 10.3 What you see in each state

**ARM state (healthy):**

```text
SWD DPIDR 0x4c013477
[rp2350.cm0] Cortex-M33 r1p0 processor detected
0xe000ed00: 411fd210
```

**RISC-V state (with an ARM-only OpenOCD):**

```text
SWD DPIDR 0x4c013477
Error: Failed to read memory at 0xe000ed00
Error: [rp2350.cm0] Examination failed
Error: [rp2350.cm1] Examination failed
```

The DPIDR line proves the probe and wiring are fine; the ARM examination
failure proves the ARM cores are not selected.

---

## 11. How to check and switch the architecture

### 11.1 Check

With a working ARM debug session:

```bash
mdw 0x40120158 1
mdw 0x4012015c 1
```

`ARCHSEL_STATUS = 0` means ARM; `3` means RISC-V.

### 11.2 Switch to ARM

Preferred, when `picotool` can reach the device:

```bash
picotool reboot -c arm
```

Or force BOOTSEL mode and flash an ARM image:

1. Unplug the Pico 2.
2. Hold **BOOTSEL**.
3. Plug the USB cable.
4. A mass-storage device `RPI-RP2` appears.
5. Copy `flash_nuke.uf2` (or any ARM UF2) to it.
6. Unplug and replug.

The bootrom then selects ARM.

### 11.3 Switch to RISC-V

```bash
picotool reboot -c riscv
```

or boot/flash a RISC-V image. Then use the Raspberry Pi OpenOCD fork with:

```text
set USE_CORE { rv0 rv1 }
```

### 11.4 Rescue reset (ARM recovery without reflashing)

If the board is stuck and the ARM AP cannot examine, the rescue reset described
in section 8 brings both Cortex-M33 cores back to the bootrom without touching
flash. It is a recovery tool: if the flash still contains a RISC-V image, the
next power cycle may select RISC-V again.

---

## 12. All commands used

```bash
# 1. Build
cd /home/dan/Work/micro-os-scaffold-workspace/tests
./build.sh cortexm-pico2-cmake-gcc-debug

# 2. First hardware attempt
./test.sh cortexm-pico2-cmake-gcc-debug --hwd

# 3. Arm-only diagnostic
source scripts/env.sh
"$UOS_TOOL_OPENOCD" \
  -s "$(dirname "$(dirname "$UOS_TOOL_OPENOCD")")/openocd/scripts" \
  -f ../micro-os-plus-iii-cortexm/test/boards/pico2/openocd.cfg \
  -c "adapter speed 500" \
  -c "init" \
  -c "targets" \
  -c "mdw 0xE000ED00 1" \
  -c "shutdown"

# 4. Rescue reset, then examine
"$UOS_TOOL_OPENOCD" \
  -s "$(dirname "$(dirname "$UOS_TOOL_OPENOCD")")/openocd/scripts" \
  -f rescue.cfg \
  -c "init" \
  -c "rescue_reset" \
  -c "sleep 300" \
  -c "rp2350.cm0 arp_examine" \
  -c "mdw 0xE000ED00 1" \
  -c "shutdown"

# 5. Test again
./test.sh cortexm-pico2-cmake-gcc-debug --hwd

# 6. Check architecture after the rescue
#    (inside the OpenOCD session)
mdw 0x40120158 1
mdw 0x4012015c 1
```

---

## 13. Direct hardware runner

Equivalent to what CTest runs:

```bash
cd /home/dan/Work/micro-os-scaffold-workspace

BUILD="$PWD/tests/build/cortexm-pico2-cmake-gcc-debug/platform-bin/port-tests" \
  bash micro-os-plus-iii-cortexm/test/boards/pico2/hw.sh smp-mat-test-ram 600
```

or the RAM-specific runner:

```bash
micro-os-plus-iii-cortexm/test/pico2/smp-mat-test-ram/hw.sh \
  "$PWD/tests/build/cortexm-pico2-cmake-gcc-debug/platform-bin/port-tests" \
  smp-mat-test-ram 600
```

Log:

```text
tests/build/cortexm-pico2-cmake-gcc-debug/platform-bin/port-tests/.hw-logs/smp-mat-test-ram.log
```

---

## 14. Troubleshooting

| Symptom | Check |
|---|---|
| `Failed to read memory at 0xe000ed00` on both cores | Board is in RISC-V mode or ARM cores are held in reset. Use rescue reset, then check `ARCHSEL_STATUS`. |
| `unable to find a matching CMSIS-DAP device` | Probe not connected, USB permissions, or WCH-Link not in ARM mode (`1a86:8011`). |
| `SWD DPIDR` not printed | SWDIO/SWCLK/GND wiring, target power. |
| `DPIDR` printed but no cores | Target architecture mismatch or cores in reset. |
| OpenOCD dies immediately | Probe/cable, target power, another OpenOCD holding the probe. |
| `TIMEOUT` after a long run | UART output only shows progress; semihosting carries the verdict. Check `.hw-logs`. |
| Test passes once, next run fails | Power-cycle the board; use the rescue reset if necessary. |
| RISC-V target fails with `Unknown param: -dap` | The xpack OpenOCD build does not support RP2350 RISC-V. Use the Raspberry Pi fork / Pico SDK OpenOCD. |

---

## 15. Current state

- The board is currently in **ARM mode** (`ARCHSEL_STATUS = 0x00000000`).
- `smp-mat-test-ram` builds and passes on hardware in ~3.1 s.
- The test is RAM-resident and does not modify flash.
- The minimal workspace contains only the files needed for `4.4.12`.
- For RISC-V work, use the Raspberry Pi OpenOCD fork and `USE_CORE {rv0 rv1}`.

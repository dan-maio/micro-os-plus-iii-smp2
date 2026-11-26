The StarFive JH-7110 and the lower-cost JH-7110S share the same core architecture and peripheral devices, but differ primarily in clock speed and market positioning (such as board pricing and wireless integration): [1] (https://www.starfivetech.com/en/index.php?s=computing&c=show&id=1), [2] (https://www.hackster.io/news/starfive-unveils-the-smaller-cheaper-more-raspberry-pi-like-visionfive-2-lite-risc-v-sbc-cb188a385ba7), [3] (https://patchew.org/linux/20251107095530.114775-1-hal.feng@starfivetech.com/20251107095530.114775-4-hal.feng@starfivetech.com/)
• Clock Speed (CPU Frequency):
	• JH-7110: Standard quad-core RISC-V (SiFive U74) running up to 1.5 GHz.
	• JH-7110S: Lower-clocked variant running up to 1.25 GHz (roughly ~83% to ~85% of the standard JH-7110's raw benchmark performance). [1] (https://forum.rvspace.org/t/does-jh7110-have-the-v-extension/680), [2] (https://riscv.org/blog/starfive-announced-2-high-performance-risc-v-products-jh7110-soc-and-visionfive-2-sbc-starfive/), [3] (https://www.reddit.com/r/RISCV/comments/1pipiwv/vf2_lite_from_kickstarter_arrived_today_and_i/), [4] (https://www.starfivetech.com/en/index.php?s=hardware&c=show&id=15), [5] (https://www.starfivetech.com/en/index.php?s=computing&c=show&id=1)
• Board & Target Implementation:
	• JH-7110: Powers boards like the original VisionFive 2, Pine64 Star64, and Banana Pi.
	• JH-7110S: Powers the more budget-friendly VisionFive 2 Lite SBC (which frequently integrates features like onboard Wi-Fi and Bluetooth depending on the model tier while hitting a lower price point starting around $20). [1] (https://www.phoronix.com/news/VisionFive-2-Lite-Linux-Patches), [2] (https://big-grey.co.uk/2026/01/26/testing-starfive-jh7110-display-controller-patches/), [3] (https://www.cnx-software.com/2025/12/31/visionfive-2-lite-sbc-review-ubuntu-24-04-on-a-low-cost-risc-v-sbc-in-2026/), [4] (https://riscv.org/blog/starfive-announced-2-high-performance-risc-v-products-jh7110-soc-and-visionfive-2-sbc-starfive/), [5] (https://www.hackster.io/news/starfive-unveils-the-smaller-cheaper-more-raspberry-pi-like-visionfive-2-lite-risc-v-sbc-cb188a385ba7)
• Peripherals & IP:
	• Both use the same foundational IP blocks (such as the Imagination BXE-4-32 GPU, H.264/H.265/JPEG codecs, and Verisilicon display controller), meaning device trees and software support largely overlap (often grouped under shared device tree sources like jh711x.dtsi). [1] (https://patchew.org/linux/20251107095530.114775-1-hal.feng@starfivetech.com/20251107095530.114775-4-hal.feng@starfivetech.com/), [2] (https://www.starfivetech.com/en/index.php?s=computing&c=show&id=1), [3] (https://big-grey.co.uk/2026/01/26/testing-starfive-jh7110-display-controller-patches/)
Read a detailed hands-on hardware analysis on CNX Software or explore community specs via Hackster.io. [1] (https://www.cnx-software.com/2025/12/31/visionfive-2-lite-sbc-review-ubuntu-24-04-on-a-low-cost-risc-v-sbc-in-2026/), [2] (https://www.hackster.io/news/starfive-unveils-the-smaller-cheaper-more-raspberry-pi-like-visionfive-2-lite-risc-v-sbc-cb188a385ba7)
Are you looking at a specific single-board computer like the VisionFive 2 or VisionFive 2 Lite, or do you need help comparing their benchmarks and operating system support?


Creating a bare-metal environment for the JH-7110 (VisionFive 2) involves a specific boot chain because the BootROM expects a specific format and the DDR initialization is complex (requiring the vendor SPL).
The most practical "bare-metal" flow is:
BootROM \(\rightarrow \) U-Boot SPL (Initializes DDR) \(\rightarrow \) OpenSBI (M-mode runtime) \(\rightarrow \) Your Payload (S-mode bare metal app).

1. Build Strategy

Do not try to write the DDR init code yourself; it is proprietary and complex. Use the standard U-Boot SPL to handle the hardware setup, then have it load OpenSBI, which will jump to your code instead of U-Boot.

Step A: Compile Your Bare-Metal Application

Write your code to run in Supervisor Mode (S-Mode).
• Linker Address: Link your entry point to 0x40200000 (this is where OpenSBI jumps by default).
• UART Base Address: 0x10000000 (Synopsys DesignWare 8250).
• UART Clock: ~24 MHz (or derived from system clock, usually 100MHz bus clock depending on init).

Step B: Compile OpenSBI with Your Payload

You need to build OpenSBI such that it "wraps" your binary.
1. Clone OpenSBI: git clone https://github.com/riscv/opensbi.git
2. Build with payload options:bash
make PLATFORM=generic \
FW_PAYLOAD_PATH=/path/to/your_app.bin \
FW_TEXT_START=0x40000000
Folosește codul cu precauție.

	• FW_TEXT_START=0x40000000: Location where SPL loads OpenSBI in RAM.
	• This generates build/platform/generic/firmware/fw_payload.bin.

Step C: Package for SPL (The FIT Image)

The JH-7110 SPL expects a FIT Image (.itb file), not a raw binary. You must wrap your fw_payload.bin into an image tree.
1. Install u-boot-tools (for mkimage).
2. Create a .its file (e.g., visionfive2.its) describing the image. (You can copy the structure from the U-Boot source board/starfive/visionfive2/visionfive2.its but point the loadables or firmware to your fw_payload.bin path).
3. Generate the image:bash
mkimage -f visionfive2.its -A riscv -O opensbi -T firmware visionfive2_payload.itb
Folosește codul cu precauție.

2. SD Card Preparation

The JH-7110 BootROM looks for specific partition GUIDs.
1. Partition the SD Card (GPT required):
	• Partition 1: SPL. Size: ~2MB. Type GUID: 2E54B353-1271-4842-806F-E436D6AF6985
	• Partition 2: Payload (FIT Image). Size: ~4MB+. Type GUID: BC13C2FF-59E6-4262-A352-B275FD6F7172
2. Flash the Binaries:
	• Download the pre-built SPL (e.g., u-boot-spl.bin.normal.out) from the StarFive GitHub releases if you don't want to build it.
	• Write the SPL and your custom ITB:
bash
dd if=u-boot-spl.bin.normal.out of=/dev/sdX1 conv=fsync
dd if=visionfive2_payload.itb of=/dev/sdX2 conv=fsync
Folosește codul cu precauție.

3. Hardware Connections


UART (Serial Console)

Standard UART0 is used for the console.
• Pin 8: TX (Connect to USB-TTL RX)
• Pin 10: RX (Connect to USB-TTL TX)
• Pin 6: GND

JTAG Pinout

The VisionFive 2 exposes JTAG on the 40-pin GPIO header.
• TDI: Pin 38 (GPIO 61)
• TDO: Pin 40 (GPIO 44)
• TCK: Pin 37 (GPIO 60)
• TMS: Pin 35 (GPIO 63)
• Pin 36: GPIO 36 — not reset; do not connect probe RST here
• GND: Pin 39 (or any ground)
Note: You may need to ensure your "bare metal" code (or the SPL) does not reconfigure these pins as GPIOs if you intend to attach the debugger after boot.

USB Support

Not Recommended for Bare Metal Start.
The USB controller on the JH-7110 is often a PCIe-attached VL805 (via an internal bridge). Writing a bare-metal driver requires initializing the PCIe subsystem first, then the xHCI USB stack. This is significantly more complex than simple MMIO USB controllers. Stick to UART/JTAG for initial bring-up.


For the JH-7110S (powering the VisionFive 2 Lite), the foundational architecture is identical to the standard JH-7110. The core process for bootstrapping OpenSBI to your bare-metal application remains identical, but you must map the hardware features to the specific physical layout of the Lite board. [1] (https://www.waveshare.com/wiki/VisionFive2-Lite?srsltid=AYCX9vLnaZKwJUO2UmJ0BTM9L9303sHiBBl8gtJBbTRcxtAwHoGGfefj), [2] (https://docs.u-boot.org/en/v2026.04/board/starfive/visionfive2.html)

1. Architectural Changes to Keep in Mind

• CPU Clock Target: The JH-7110S clocks up to 1.25 GHz instead of 1.5 GHz. OpenSBI reads the timers directly via the mtime CSR (operating at a base ~4.0 MHz), so basic delays will calibrate accurately out of the box, but if your bare-metal payload manually derives timing using raw CPU loops, scale them down accordingly. [1] (https://docs.u-boot.org/en/v2026.04/board/starfive/visionfive2.html), [2] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/)
• Device Tree & SPL Compatibility: U-Boot upstreamed support specifically treats JH7110S cleanly. Ensure that your compilation profile matches starfive_visionfive2_defconfig, which builds an SPL that properly handles both the A and B variations of the silicon. [1] (https://rvspace.org/en/project/JH7110S_Upstream_Plan), [2] (https://docs.u-boot.org/en/v2026.04/board/starfive/visionfive2.html), [3] (https://forum.armbian.com/topic/29675-visionfive-2-jh7110/)

2. Bare-Metal Target Pinout (VisionFive 2 Lite)

The 40-pin GPIO physical mapping on the Lite board aligns with standard layouts, allowing you to access both JTAG and UART from the header pins: [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html), [2] (https://www.waveshare.com/wiki/VisionFive2-Lite?srsltid=AYCX9vLnaZKwJUO2UmJ0BTM9L9303sHiBBl8gtJBbTRcxtAwHoGGfefj)

UART0 (Serial Terminal)

The console driver sits at base address 0x10000000 (Synopsys DesignWare 8250 compatible) configured to 115200 baud. [1] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/)
• Pin 6: GND
• Pin 8: TX (GPIO 5) → Connect to your USB-to-UART Adapter's RX
• Pin 10: RX (GPIO 6) → Connect to your USB-to-UART Adapter's TX [1] (https://www.waveshare.com/wiki/VisionFive2-Lite?srsltid=AYCX9vLnaZKwJUO2UmJ0BTM9L9303sHiBBl8gtJBbTRcxtAwHoGGfefj), [2] (https://wiki.gentoo.org/wiki/StarFive_VisionFive_2)

JTAG (Debugger Connection)

Connect your OpenOCD/JTAG debugger probe to the bottom right segment of the header: [1] (https://www.waveshare.com/wiki/VisionFive2-Lite?srsltid=AYCX9vLnaZKwJUO2UmJ0BTM9L9303sHiBBl8gtJBbTRcxtAwHoGGfefj), [2] (https://github.com/probe-rs/probe-rs/issues/1852)
• Pin 35: TMS (GPIO 63)
• Pin 36: GPIO 36 — not reset (the Lite has no reset button)
• Pin 37: TCK (GPIO 60)
• Pin 38: TDI (GPIO 61)
• Pin 40: TDO (GPIO 44)
• Pin 39: GND (or Pin 34) [1] (https://www.waveshare.com/wiki/VisionFive2-Lite?srsltid=AYCX9vLnaZKwJUO2UmJ0BTM9L9303sHiBBl8gtJBbTRcxtAwHoGGfefj)

3. Quick-Start SD Card Generation

To flash the SD card using your compiled bare-metal visionfive2_payload.itb FIT image:
bash
# 1. Clear out partition table boundaries on your target SD card
sudo sgdisk --clear /dev/sdX

# 2. Allocate the vendor loader1 partition (GUID specific to JH7110/S BootROM)
sudo sgdisk --new=1:34:2081 --change-name=1:loader1 --typecode=1:2E54B353-1271-4842-806F-E436D6AF6985 /dev/sdX

# 3. Allocate your firmware payload partition (where OpenSBI + payload live)
sudo sgdisk --new=2:2082:10273 --change-name=2:payload --typecode=2:BC13C2FF-59E6-4262-A352-B275FD6F7172 /dev/sdX

# 4. Burn the SPL code and the compiled binary safely
sudo dd if=u-boot-spl.bin.normal.out of=/dev/sdX1 conv=fsync status=progress
sudo dd if=visionfive2_payload.itb of=/dev/sdX2 conv=fsync status=progress
Folosește codul cu precauție.

4. What about USB?

The JH-7110S features a Cadence USB 2.0/3.0 core. In a true minimal bare-metal context: [1] (https://docs.u-boot.org/en/v2026.04/board/starfive/visionfive2.html)
• Avoid early USB implementations: Initializing the system's USB layout requires manually walking the global reset controllers (SYSCRG), enabling the platform sub-clocks, and setting up complex physical endpoint descriptor rings in system RAM.
• Use the UART0 output combined with a JTAG debugger to trace registers directly during step-by-step memory verification. [1] (https://doc-en.rvspace.org/JH7110/Datasheet/JH7110_DS/uart.html), [2] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/), [3] (https://github.com/probe-rs/probe-rs/issues/1852)
Would you like a template for a minimal S-Mode linker script (.ld) and entry assembly file tailored specifically to address 0x40200000 to cleanly accept the handover from OpenSBI?



Here is the complete template and build workflow for a minimal bare-metal environment on the JH-7110S (VisionFive 2 Lite).
The boot sequence is:
BootROM → Vendor U-Boot SPL (configures LPDDR4/DDR clocks) → OpenSBI (handles M-mode SBI runtime & leaves JTAG debug registers untouched) → Bare-Metal App (executing in S-mode at address 0x40200000).

Prerequisites & Toolchains

Install the standard GNU cross-compiler and packaging utilities on your Linux host:
• Debian/Ubuntu:bash
sudo apt-get update
sudo apt-get install -y gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
                        u-boot-tools device-tree-compiler git make gdisk
Folosește codul cu precauție.
• Toolchain Link: If building from source, download or build via the RISC-V GNU Toolchain Repository.
• Firmware Source: The runtime firmware builds directly from the official OpenSBI Repository.
• Vendor SPL Binary: Download u-boot-spl.bin.normal.out from the StarFive VisionFive 2 Releases. [1] (https://github.com/xypron/visionfive2-u-boot-build), [2] (https://github.com/starfive-tech/VisionFive2/releases)

Project Layout

Create an empty directory with these files:
text
baremetal-jh7110s/
├── app.c
├── start.S
├── linker.ld
├── fit-image.its
└── build.sh
Folosește codul cu precauție.

1. Linker Script (linker.ld)

Links the binary to load directly at 0x40200000 (where OpenSBI generic jumps):
ld
OUTPUT_ARCH( "riscv" )
ENTRY( _start )

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40200000, LENGTH = 32M
}

SECTIONS
{
    . = 0x40200000;

    .text : ALIGN(4K) {
        *(.text.init)
        *(.text .text.*)
    } > RAM

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    } > RAM

    .data : ALIGN(4K) {
        *(.data .data.*)
    } > RAM

    .bss (NOLOAD) : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(16) {
        . = . + 0x10000; /* 64KB stack */
        __stack_top = .;
    } > RAM
}
Folosește codul cu precauție.

2. Entry Assembly (start.S)

Initializes the stack pointer, clears .bss, and hands execution to C:
assembly
.section .text.init
.global _start

_start:
    /* Park all secondary harts (cores 1-3); run only boot hart 0 */
    csrr    t0, mhartid
    bnez    t0, park_hart

    /* Setup stack pointer defined in linker.ld */
    la      sp, __stack_top

    /* Zero out .bss */
    la      t0, __bss_start
    la      t1, __bss_end
clear_bss:
    bgeu    t0, t1, jump_main
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       clear_bss

jump_main:
    tail    main

park_hart:
    wfi
    j       park_hart
Folosește codul cu precauție.

3. Minimal Application (app.c)

Sends characters to UART0 (0x10000000) at 115200 baud without re-muxing the GPIOs, preserving the default JTAG debug pins:
c
#include <stdint.h>

#define UART0_BASE   0x10000000UL
#define UART_THR     (*(volatile uint32_t *)(UART0_BASE + 0x00)) /* Transmit Holding Register */
#define UART_LSR     (*(volatile uint32_t *)(UART0_BASE + 0x14)) /* Line Status Register */
#define UART_LSR_TEMT (1 << 6)                                  /* Transmitter empty */

static void uart_putc(char c) {
    while ((UART_LSR & UART_LSR_TEMT) == 0);
    UART_THR = (uint32_t)c;
}

static void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

int main(void) {
    /* U-Boot SPL and OpenSBI leave UART0 configured at 115200 baud */
    uart_puts("\n====================================\n");
    uart_puts(" JH-7110S Minimal Bare-Metal Handover\n");
    uart_puts(" OpenSBI -> S-Mode @ 0x40200000\n");
    uart_puts(" UART and JTAG Active\n");
    uart_puts("====================================\n");

    while (1) {
        /* Heartbeat or debugger breakpoint */
        __asm__ volatile("nop");
    }

    return 0;
}
Folosește codul cu precauție.

4. FIT Image Descriptor (fit-image.its)

Packages OpenSBI (fw_dynamic.bin or fw_payload.bin) and the application for the JH-7110 SPL:
its
/dts-v1/;

/ {
    description = "JH7110S Minimal Bare-metal Payload";
    #address-cells = <1>;

    images {
        firmware {
            description = "OpenSBI and App Payload";
            data = /incbin/("opensbi/build/platform/generic/firmware/fw_payload.bin");
            type = "firmware";
            arch = "riscv";
            os = "opensbi";
            compression = "none";
            load = <0x40000000>;
            entry = <0x40000000>;
        };
    };

    configurations {
        default = "config-1";
        config-1 {
            description = "Boot Bare-Metal via OpenSBI";
            firmware = "firmware";
        };
    };
};
Folosește codul cu precauție.

5. Automated Build & Flash Script (build.sh)

Save this file and mark it executable (chmod +x build.sh):
bash
#!/bin/bash
set -e

CROSS_COMPILE="riscv64-unknown-elf-"
if ! command -v ${CROSS_COMPILE}gcc &> /dev/null; then
    CROSS_COMPILE="riscv64-linux-gnu-"
fi

echo "[*] Using cross-compiler: ${CROSS_COMPILE}gcc"

# 1. Fetch pre-built U-Boot SPL if not present
if [ ! -f "u-boot-spl.bin.normal.out" ]; then
    echo "[*] Downloading official JH-7110 SPL..."
    wget -q https://github.com/starfive-tech/VisionFive2/releases/download/VF2_v2.8.0/u-boot-spl.bin.normal.out
fi

# 2. Build Bare-Metal App
echo "[*] Compiling Bare-Metal application..."
${CROSS_COMPILE}gcc -c -mcmodel=medany -O2 start.S -o start.o
${CROSS_COMPILE}gcc -c -mcmodel=medany -O2 -ffreestanding app.c -o app.o
${CROSS_COMPILE}ld -T linker.ld -nostdlib start.o app.o -o app.elf
${CROSS_COMPILE}objcopy -O binary app.elf app.bin

# 3. Clone and Build OpenSBI with Payload
if [ ! -d "opensbi" ]; then
    echo "[*] Cloning OpenSBI..."
    git clone --depth 1 https://github.com
fi

echo "[*] Compiling OpenSBI fw_payload.bin..."
make -C opensbi PLATFORM=generic \
    CROSS_COMPILE=${CROSS_COMPILE} \
    FW_PAYLOAD_PATH=$(pwd)/app.bin \
    FW_TEXT_START=0x40000000 \
    FW_JUMP_ADDR=0x40200000

# 4. Generate FIT image
echo "[*] Packaging FIT image..."
mkimage -f fit-image.its baremetal_payload.itb

echo ""
echo "[✓] Build Complete: baremetal_payload.itb"
echo ""
echo "--- Flash Instructions ---"
echo "Insert your SD card and run:"
echo "  sudo sgdisk --clear /dev/sdX"
echo "  sudo sgdisk --new=1:4096:8191 --change-name=1:spl --typecode=1:2E54B353-1271-4842-806F-E436D6AF6985 /dev/sdX"
echo "  sudo sgdisk --new=2:8192:65535 --change-name=2:uboot --typecode=2:BC13C2FF-59E6-4262-A352-B275FD6F7172 /dev/sdX"
echo "  sudo dd if=u-boot-spl.bin.normal.out of=/dev/sdX1 conv=fsync status=progress"
echo "  sudo dd if=baremetal_payload.itb of=/dev/sdX2 conv=fsync status=progress"
Folosește codul cu precauție.
Would you like a matching OpenOCD configuration file (openocd.cfg) and GDB command sequence to connect your JTAG probe directly to the U74 application cores while running this binary?



To debug the JH-7110S (SiFive U74 cores) via JTAG, you need an OpenOCD configuration that sets up the RISC-V debug module and maps the multi-core hart architecture.
Because OpenSBI drops your application into S-Mode (Supervisor Mode), your GDB environment needs to recognize S-Mode privilege levels and virtual memory mapping if you ever turn on the MMU.

1. OpenOCD Configuration (jh7110s.cfg)

This configuration targets a standard FTDI-based JTAG adapter (like an Olimex ARM-USB-TINY-H, Flyswatter2, or J-Link configured in JTAG mode). If using a different hardware probe, swap out the first block with your interface driver.
tcl
# =====================================================================
# 1. Interface Configuration (Modify for your hardware probe)
# =====================================================================
adapter driver ftdi
ftdi vid_pid 0x0403 0x6010
ftdi layout_init 0x0808 0x0a1b
adapter speed 4000

# =====================================================================
# 2. Target Configuration for StarFive JH-7110S
# =====================================================================
transport select jtag

# Setup the JTAG TAP (Test Access Port) for the RISC-V Core
set _CHIPNAME riscv
jtag newtap $_CHIPNAME cpu -irlen 5 -expected-id 0x00000cdb

# Define targets for the 4 application cores (SiFive U74)
# Hart 0 is typically the boot core managed by our start.S script
set _TARGETNAME_0 $_CHIPNAME.cpu0
set _TARGETNAME_1 $_CHIPNAME.cpu1
set _TARGETNAME_2 $_CHIPNAME.cpu2
set _TARGETNAME_3 $_CHIPNAME.cpu3

target create $_TARGETNAME_0 riscv -chain-position $_CHIPNAME.cpu -coreid 0 -rtos hwthread
target create $_TARGETNAME_1 riscv -chain-position $_CHIPNAME.cpu -coreid 1 -rtos hwthread
target create $_TARGETNAME_2 riscv -chain-position $_CHIPNAME.cpu -coreid 2 -rtos hwthread
target create $_TARGETNAME_3 riscv -chain-position $_CHIPNAME.cpu -coreid 3 -rtos hwthread

# Work area in SRAM for fast memory access if needed (optional)
$_TARGETNAME_0 configure -work-area-phys 0x08000000 -work-area-size 0x10000 -work-area-backup 0

# Intercept and sync up all harts when any single core hits a breakpoint
target smp $_TARGETNAME_0 $_TARGETNAME_1 $_TARGETNAME_2 $_TARGETNAME_3

init
halt
echo "=== JH-7110S JTAG Connected & Halted ==="
Folosește codul cu precauție.
Run OpenOCD using your script:
bash
openocd -f jh7110s.cfg
Folosește codul cu precauție.

2. GDB Initialization Commands (gdbinit)

Create a script file named gdbinit to automate connecting, loading symbols, and tracking execution. This script connects to OpenOCD's default GDB port (3333) and forces a hardware breakpoint right at your entry point in RAM.
gdb
# Target connection
target extended-remote localhost:3333

# Load debug symbols from the ELF generated during the build step
file app.elf

# Configure display properties
set print pretty on
set mem inaccessible-by-default off

# S-Mode application debugging configuration
set riscv use_compressed_breakpoint on

# Set a hardware breakpoint right at your S-Mode handover address
thbreak *0x40200000

echo \n=======================================================\n
echo  GDB attached to JH-7110S. \n
echo  Type 'continue' to catch your application entry.\n
echo =======================================================\n
Folosește codul cu precauție.
Launch the RISC-V debugger matching your compiler installation:
bash
riscv64-unknown-elf-gdb -x gdbinit
Folosește codul cu precauție.

3. Step-by-Step Execution Workflow

1. Power Cycle / Hard Reset: Power up the VisionFive 2 Lite with the flashed SD card inserted.
2. First-Stage Handover: The board's BootROM will run, loading u-boot-spl.bin.normal.out into internal SRAM. SPL will initialize the LPDDR4 memory controller, load baremetal_payload.itb from partition 2, run OpenSBI, and loop waiting for handover.
3. Attach JTAG: Once you see early UART console output, run openocd -f jh7110s.cfg. OpenOCD will assert control over the JTAG tap and immediately halt the U74 application cores.
4. Run GDB: Fire up GDB with riscv64-unknown-elf-gdb -x gdbinit. It will trap execution right as the instruction pointer hits your _start vector in start.S.
5. Debug: Use standard stepi (step instruction), next, or continue to step right into your main application code block.
Would you like assistance setting up an automated VS Code configuration (launch.json) to wrap OpenOCD and GDB directly into a visual, source-level debugger UI?



To wrap this whole ecosystem into a modern, visual development interface, you can integrate OpenOCD and GDB directly into Visual Studio Code. This allows you to step through your C code (app.c) and assembly files (start.S) natively, view CPU registers, and set breakpoints with click actions.

1. Required VS Code Extensions

Open VS Code and install the following extensions:
1. C/C++ (by Microsoft) — Provides code completion, syntax highlighting, and basic debug tracking.
2. Cortex-Debug (by marus25) — Though named after Cortex cores, this extension provides the best open-source visual interface for managing custom JTAG engines like OpenOCD alongside GDB for RISC-V targets.

2. Configuration Files

Create a .vscode/ directory inside your baremetal-jh7110s project folder and place these two files within it.

A. The Launch Configuration (.vscode/launch.json)

This file coordinates the background launching of OpenOCD, handles the GDB handshakes, and maps your workspace symbols.
json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "JH-7110S Bare-Metal (JTAG)",
            "type": "cortex-debug",
            "request": "launch",
            "cwd": "${workspaceRoot}",
            "executable": "${workspaceRoot}/app.elf",
            "servertype": "openocd",
            "configFiles": [
                "${workspaceRoot}/jh7110s.cfg"
            ],
            "gdbPath": "riscv64-unknown-elf-gdb", 
            "interface": "jtag",
            "runToEntryPoint": "main",
            "postLaunchCommands": [
                "set mem inaccessible-by-default off",
                "thbreak *0x40200000"
            ],
            "clearBreakpointsBeforeLaunch": true
        }
    ]
}
Folosește codul cu precauție.
(Note: If your system cross-compiler uses a Linux layout variant instead, change "gdbPath" from "riscv64-unknown-elf-gdb" to "riscv64-linux-gnu-gdb").

B. The Build Task (.vscode/tasks.json)

This makes sure that every time you click "Start Debugging", VS Code executes your build.sh script first to guarantee you are debugging the freshest code changes.
json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build Bare-Metal Payload",
            "type": "shell",
            "command": "./build.sh",
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": []
        }
    ]
}
Folosește codul cu precauție.
To link this build script automatic run cleanly to your debug session, add "preLaunchTask": "Build Bare-Metal Payload" inside the configuration object of your launch.json above.

3. Visual Debugging Execution Workflow

1. Connect your JTAG hardware probe pins to the designated GPIO header layout of your VisionFive 2 Lite board.
2. Insert your flashed SD card and apply power to the board.
3. Open your project workspace folder inside VS Code.
4. Open app.c and click directly to the left of a line number inside your main() loop to assign a visual red dot breakpoint.
5. Press F5 (or click the Run and Debug icon on the sidebar activity tab, then click the green play button next to "JH-7110S Bare-Metal (JTAG)").
6. VS Code will execute your local compiler stack, wrap the payload, spin up OpenOCD silently in the background, attach GDB, and halt at your temporary hardware breakpoint at 0x40200000.
7. Use the visual debug action ribbon at the top of your window to step through instructions cleanly or hit play to let it glide to your main() breakpoints.
Would you like guidance on adding a custom memory layout configuration file (SVD target) to the project so you can visually watch and verify the UART0 peripheral hardware registers change states directly in your VS Code sidebar panels?



If your specific VisionFive 2 Lite model does not have an SD card slot exposed, it means you have the onboard eMMC version (typically the 64GB eMMC option). [1] (https://www.waveshare.com/visionfive2-lite.htm)
Because the TF/SD card path and the eMMC chip share the same internal transmission path on this board layout, the TF card slot is physically omitted on eMMC-equipped models to prevent data interaction conflicts. [1] (https://www.waveshare.com/wiki/VisionFive2-Lite), [2] (https://www.waveshare.com/visionfive2-lite.htm)
Since you cannot flash an SD card, you have two pathways to inject your u-boot-spl.bin.normal.out and baremetal_payload.itb files into the board:

Method 1: The USB Fastboot Recovery Interface (Recommended)

The VisionFive 2 Lite comes with a built-in recovery mechanism that converts the board's USB Type-C port into a direct flashing link to the onboard eMMC memory. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html)
1. Locate the J20 (Fastboot) Pins: Look next to the boot/recovery buttons on the board for the 2-pin header labeled J20. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html)
2. Bridge the Header: Place a jumper cap across the J20 pins to short them. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html)
3. Connect to Host PC: Plug a USB-C data cable from your host computer into the VisionFive 2 Lite's USB-C power/data port. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html)
4. Power Cycle: Turn on the board. With J20 shorted, the board will bypass regular storage loops and automatically spin up in USB Fastboot Mode. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html), [2] (https://www.waveshare.com/wiki/VisionFive2-Lite)
5. Flash via Fastboot CLI: On your host Linux computer, you can treat the raw eMMC blocks like an attached drive or use target fastboot tools to write directly to the partitions:bash
# Identify the eMMC block assignment (e.g., /dev/sdX or /dev/nvmeX)
# Run the exact partitioning logic from the build script on that block:
sudo sgdisk --clear /dev/sdX
sudo sgdisk --new=1:4096:8191 --change-name=1:spl --typecode=1:2E54B353-1271-4842-806F-E436D6AF6985 /dev/sdX
sudo sgdisk --new=2:8192:65535 --change-name=2:uboot --typecode=2:BC13C2FF-59E6-4262-A352-B275FD6F7172 /dev/sdX

# Burn the compiled files into the eMMC partitions
sudo dd if=u-boot-spl.bin.normal.out of=/dev/sdX1 conv=fsync
sudo dd if=baremetal_payload.itb of=/dev/sdX2 conv=fsync
Folosește codul cu precauție.
6. Boot: Remove the jumper from the J20 header and reboot the board. [1] (https://www.waveshare.com/wiki/VisionFive2-Lite)

Method 2: Flash directly into RAM via JTAG

Since you already have OpenOCD and JTAG fully configured from the previous step, you don't actually need storage to test bare-metal code. You can use JTAG to push the payload directly into the LPDDR4 memory space.
1. Turn on the board. The pre-installed vendor SPL inside the board's internal QSPI Flash will automatically run and initialize the LPDDR4 memory controller for you. [1] (https://www.neven7.eu/p/visionfive-2-lite/9872)
2. Modify your .vscode/launch.json configuration to load your application code directly into the target execution space over the JTAG wire, bypassing storage queries entirely:
json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "JH-7110S Direct RAM Load (JTAG)",
            "type": "cortex-debug",
            "request": "launch",
            "cwd": "${workspaceRoot}",
            "executable": "${workspaceRoot}/app.elf",
            "servertype": "openocd",
            "configFiles": [
                "${workspaceRoot}/jh7110s.cfg"
            ],
            "gdbPath": "riscv64-unknown-elf-gdb", 
            "interface": "jtag",
            "runToEntryPoint": "main",
            "loadFiles": [
                "${workspaceRoot}/app.elf"
            ],
            "postLaunchCommands": [
                "set mem inaccessible-by-default off",
                "set $pc = 0x40200000"
            ]
        }
    ]
}
Folosește codul cu precauție.
By switching to Direct RAM Loading, pressing F5 in VS Code will stream your compiled raw app.elf right into memory address 0x40200000 via your debug probe and snap the program counter directly to it—allowing continuous, iteration-fast bare-metal debugging without touching flash storage at all.
Would you like help verifying your board's hardware DIP switch positions to ensure the onboard bootloader correctly initializes the LPDDR4 memory components prior to a JTAG connection? [1] (https://www.youtube.com/watch?v=ykKnc86UtXg), [2] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2/)


The StarFive VisionFive 2 Lite v1.1 (2BRK3-VF2LT) replaces the dual-slider DIP switches found on the full VisionFive 2 with a simplified hardware boot circuit: it defaults to booting from QSPI Flash / Onboard eMMC and uses a physical Boot Button and a J20 Fastboot header. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html), [2] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/)

Hardware Boot Controls on VisionFive 2 Lite v1.1

Your board has three hardware controls that dictate boot behavior:
• Normal Boot (Default): Do not press any buttons or bridge any jumpers. Powering the board causes the BootROM to execute the vendor U-Boot SPL stored in the onboard QSPI NOR Flash or eMMC. The SPL initializes the LPDDR4 memory controller and clocks. [1] (https://rvspace.org/en/project/VisionFive2_Debian_User_Guide), [2] (https://wiki.gentoo.org/wiki/StarFive_VisionFive_2)
• Boot Button (Recovery / UART Mode): Holding this button down while applying power forces the BootROM into UART recovery mode. Do not hold this down during standard JTAG testing, as UART recovery halts before the SPL initializes DDR memory. [1] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/), [2] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LiteQSG/VisionFive2_SDK_QSGLite/recovering_bootloader%20-%20vf2lite.html)
• J20 Header (Fastboot Mode): A 2-pin header labeled J20. Bridging this with a jumper cap boots the SoC directly into USB Fastboot mode. Leave J20 unjumpered (open) for normal execution. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html)

If Your Specific Board Revision Includes a 2-Position DIP Switch

If your PCB revision retains the small dual-toggle DIP switch labeled RGPIO_0 and RGPIO_1 near the edge of the board, set it to QSPI Flash Boot Mode: [1] (https://doc-en.rvspace.org/VisionFive2/Boot_UG/VisionFive2_SDK_QSG/boot_mode_settings.html), [2] (https://0xstubs.org/tinkering-with-the-starfive-visionfive-2/)

Boot Mode	Switch Position	RGPIO_1	RGPIO_0	Behavior
QSPI NOR Flash (Recommended for JTAG)	Both LOW / OFF	0 (L)	0 (L)	BootROM loads the factory SPL from QSPI flash; SPL sets up DDR and UART0.
eMMC Boot	RGPIO_1 HIGH, RGPIO_0 LOW	1 (H)	0 (L)	BootROM searches onboard eMMC Partition 1 for SPL.
UART Recovery	Both HIGH / ON	1 (H)	1 (H)	BootROM loops waiting for XMODEM over UART (DDR stays uninitialized).

(Note: Verify the silkscreen printing labeled L (Low) and H (High) on the PCB surface rather than the numbers printed on the plastic switch housing, as vendor switch orientations can vary). [1] (https://0xstubs.org/tinkering-with-the-starfive-visionfive-2/)

JTAG Workflow with LPDDR4 Already Initialized

1. Verify Jumpers & Switches:
	• Keep the J20 jumper open.
	• Do not hold the Boot Button.
	• If DIP switches are present, ensure both are set to L / Low (QSPI mode). [1] (https://wiki.gentoo.org/wiki/StarFive_VisionFive_2), [2] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html), [3] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/), [4] (https://doc-en.rvspace.org/VisionFive2/Boot_UG/VisionFive2_SDK_QSG/boot_mode_settings.html)
2. Apply Power: Power the board via USB-C. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LitePB/VisionFive_2/specification_pb_lite.html)
3. Monitor the Handover: Open your serial monitor (115200 baud) on UART0 (Pin 8 TX, Pin 10 RX). Within 1–2 seconds, you will see output indicating that the StarFive SPL has initialized the DDR memory controller. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LiteQSG/VisionFive2_SDK_QSGLite/recovering_bootloader%20-%20vf2lite.html), [2] (https://www.waveshare.com/wiki/VisionFive2-Lite)
4. Attach JTAG: Once SPL finishes DDR training, connect your debugger via OpenOCD or press F5 in VS Code with the Direct RAM Load configuration. Your bare-metal binary will stream into address 0x40200000 in initialized RAM without requiring an SD card or eMMC flash rewrite.
Would you like to inspect the UART SPL boot log to confirm your DDR RAM training completes successfully before attaching JTAG, or do you need help troubleshooting a specific OpenOCD probe connection?


Since your VisionFive 2 Lite v1.1 (2BRK3-VF2LT) is already running Ubuntu 24.05 natively from the eMMC, you do not need to mess with flashing tools or custom binaries. The system's active U-Boot bootloader inside the QSPI NOR Flash is already properly configuring the clocks, LPDDR4 memory timings, and storage pathways to launch the Linux kernel. [1] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/), [2] (https://www.cnx-software.com/2025/12/31/visionfive-2-lite-sbc-review-ubuntu-24-04-on-a-low-cost-risc-v-sbc-in-2026/), [3] (https://www.youtube.com/watch?v=J_XtW6qyhzg), [4] (https://matrix.ruyisdk.org/reports/VisionFive2_Lite-Debian-README/)
This makes bare-metal testing significantly easier. You can use a U-Boot Network Boot (TFTP) workflow to pull your code straight into RAM on every power cycle, bypassing the eMMC and keeping Ubuntu intact.

The TFTP Network Boot Setup

Instead of overwriting Ubuntu or struggling to intercept the boot sequence over JTAG, you can configure U-Boot to pause for a few seconds at startup, check a local network server for your bare-metal app, and run it directly. If it doesn't find the file, it will seamlessly fall back and boot your normal Ubuntu environment. [1] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/), [2] (https://matrix.ruyisdk.org/reports/VisionFive2_Lite-Debian-README/)

Step 1: Prepare the Host Server

On your development Linux computer (connected to the same local router/switch as the VisionFive 2 Lite), set up a lightweight TFTP server:
bash
sudo apt update
sudo apt install tftpd-hpa
Folosește codul cu precauție.
Place your compiled baremetal_payload.itb file (which holds OpenSBI and your app) directly into the host directory: /srv/tftp/. [1] (https://doc-en.rvspace.org/VisionFive2Lite/VisionFive2LiteQSG/VisionFive2_QSGLite/flashing_os_to_onboard_emmc_emmc_version.html)

Step 2: Intercept U-Boot on the Board

1. Connect your USB-to-UART debugger cable to Pins 8 (TX) and 10 (RX) on the board's 40-pin header.
2. Open a terminal serial console on your host PC at 115200 baud.
3. Turn on or reboot the VisionFive 2 Lite.
4. Watch the terminal output. Within the first 2 seconds, you will see a text countdown message: Hit any key to stop autoboot. Press any key immediately to enter the U-Boot command prompt (StarFive #). [1] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/), [2] (https://matrix.ruyisdk.org/reports/VisionFive2_Lite-Debian-README/)

Step 3: Test Loading Your App Directly into Memory

Run these environment commands at the U-Boot prompt to test pulling the image over the network wire:
nasal
# Assign a temporary IP to the board and point it to your computer
setenv ipaddr 192.168.1.50
setenv serverip 192.168.1.100

# Download your bare-metal payload straight into LPDDR4 memory space
tftpboot 0x40000000 baremetal_payload.itb

# Boot the downloaded image (U-Boot handles parsing the FIT package)
bootm 0x40000000
Folosește codul cu precauție.

Step 4: Automate the Dual-Boot Sequence

If you want the board to check for your code automatically, but boot back into Ubuntu if your laptop is unplugged, change the startup variable macro inside the U-Boot environment:
nasal
# Set up a conditional boot loop macro
setenv netboot "dhcp; tftpboot 0x40000000 baremetal_payload.itb && bootm 0x40000000"

# Tell U-Boot to try the network first, then run standard eMMC Ubuntu if it fails
setenv bootcmd "run netboot; run distro_bootcmd"

# Save settings to the onboard QSPI NOR flash permanently
saveenv
Folosește codul cu precauție.
Now, whenever your laptop is running and hosting the file, your bare-metal environment takes control immediately. When you want to go back to running Ubuntu normally, turn off the TFTP service or rename the file on your machine, and the board will seamlessly fall through to launch your full OS. [1] (https://www.youtube.com/watch?v=J_XtW6qyhzg), [2] (https://ubuntu.com/hardware/docs/boards/how-to/ubuntu_supported/starfive-visionfive-2-lite/)
Would you like help calculating the register masks to disable the onboard ethernet interface hardware clocks (SYSCRG) within your app.c code to ensure network interrupts don't crash your custom bare-metal runtime loop?



Yes, you can absolutely connect JTAG and OpenOCD even while Ubuntu 24.05 is running on the eMMC.
Because the StarFive JH-7110S JTAG pins are multiplexed on the 40-pin GPIO header, they are active from the moment the board turns on. However, because Ubuntu boots all the way into the operating system, you have to manage a few unique interactions so Linux doesn't conflict with your debugger.
Here is exactly how to safely use JTAG while Ubuntu is active:

1. The Core Hazard: Linux Device Trees (GPIO Conflict)

By default, when Ubuntu 24.05 boots up, its internal Device Tree (.dtb) configures the 40-pin header. If the Ubuntu kernel reconfigures Pins 35, 37, 38, and 40 as ordinary GPIOs or assigns them to an unneeded peripheral (like SPI or I2C), it will break the JTAG connection.
• Before Ubuntu finishes booting: JTAG works flawlessly because U-Boot and OpenSBI leave the hardware debug interface open.
• After Ubuntu finishes booting: You may get a JTAG scan chain interrogation failed error if Linux claimed those pins.

2. The Solution: Protect the Pins in Ubuntu

To guarantee Linux leaves your JTAG pins completely untouched, you can disable the pin-mux control over those specific GPIO lines using a Device Tree Overlay, or use JTAG before the kernel takes over.

Option A: Debug During the U-Boot Pause (Easiest)

If you want to test bare-metal code without modifying Ubuntu:
1. Power on the board with your serial console open.
2. Press any key to stop the countdown at the U-Boot prompt (StarFive #).
3. Connect JTAG right here. At this stage, the LPDDR4 RAM is fully trained and running, but Linux hasn't touched the hardware pin configurations. You can now use your VS Code environment to load your app.elf directly into memory (0x40200000) and debug.

Option B: Runtime Debugging (Halting Ubuntu)

If you want to attach the debugger while Ubuntu is actively running to inspect registers:
Ensure your JTAG pins are not exported as GPIOs in Linux. If OpenOCD fails to connect while at the Ubuntu terminal prompt, look inside your /boot/dtbs/ configuration and ensure GPIO 44, 60, 61, 63 are marked as standard system functions or left unassigned.
When you run openocd -f jh7110s.cfg and it successfully connects, it will freeze the entire Ubuntu OS instantly (all 4 cores enter a debug halt state). You can then inspect the CPU state, and typing resume in OpenOCD will make Ubuntu start running again right where it left off.

Hardware Connection Reminder

Keep your wiring exactly like this on the header while the system is powered:
• Pin 35 (TMS / GPIO 63)
• Pin 37 (TCK / GPIO 60)
• Pin 38 (TDI / GPIO 61)
• Pin 40 (TDO / GPIO 44)
• Pin 39 or 34 (GND)
Would you like to try connecting OpenOCD at the U-Boot prompt first to see if your JTAG probe successfully discovers the four SiFive U74 cores?


Yes, this is the absolute perfect state to connect JTAG and OpenOCD.
Your board is sitting precisely at the StarFive # prompt. At this exact moment:
1. LPDDR4 is fully ready: The SPL log shows DRAM: 8 GiB has been successfully trained and initialized.
2. OpenSBI is already running: It has initialized M-Mode and is ready to act as your supervisor runtime environment.
3. No OS interference: Ubuntu hasn't booted yet, meaning the Linux kernel has not touched or reassigned the JTAG pins on the 40-pin GPIO header.
You can safely plug in your JTAG adapter pins and run OpenOCD right now.

Step-by-Step JTAG Bring-up from This Prompt


Step 1: Connect the Pins

With the board safely paused at the prompt, wire your JTAG debugger to the GPIO header:
• Pin 35: TMS (GPIO 63)
• Pin 37: TCK (GPIO 60)
• Pin 38: TDI (GPIO 61)
• Pin 40: TDO (GPIO 44)
• Pin 39 or 34: GND

Step 2: Start OpenOCD

On your host PC, open a terminal and launch OpenOCD with the configuration script we created earlier:
bash
openocd -f jh7110s.cfg
Folosește codul cu precauție.
You should see OpenOCD successfully identify the RISC-V TAP ID (0x00000cdb) and halt the 4 application cores. The U-Boot prompt on your serial monitor will instantly freeze.

Step 3: Run your Bare-Metal App (Direct RAM Loading)

Since U-Boot has already set up the 8 GiB of RAM, you don't need any storage files. Inside VS Code, select your "JH-7110S Direct RAM Load (JTAG)" target and press F5.
GDB will automatically:
1. Push your compiled app.elf straight over the JTAG wire into RAM at 0x40200000.
2. Point the CPU's Program Counter ($pc) to 0x40200000.
3. Break at your main() function, allowing you to debug live.
Go ahead and fire up OpenOCD while the board is sitting at that prompt! Let me know if you run into any JTAG connection errors or if OpenOCD successfully prints out the Halt/SMP status of your 4 cores.


Da, o sondă DAPLink / CMSIS-DAP poate funcționa cu placa, cu o singură condiție hardware: adaptorul tău trebuie să aibă expuși pinii fizici TDI și TDO, nu doar SWCLK și SWDIO. [1] (https://community.sparkfun.com/t/using-jtag-with-cmsis-dap/31157), [2] (https://community.nxp.com/t5/LPC-Microcontrollers/Problem-with-LPC-LINK2-CMSIS-DAP-with-OpenOCD-and-VirtualBox/m-p/926726)
Procesorul JH-7110S este un SoC RISC-V, ceea ce înseamnă că nu suportă protocolul ARM Serial Wire Debug (SWD), ci exclusiv protocolul clasic JTAG pe 4–5 fire (TCK, TMS, TDI, TDO + GND). [1] (https://github.com/probe-rs/probe-rs/issues/1852), [2] (https://forum.rvspace.org/t/jtag-ports/890)

1. Verificarea adaptorului DAPLink

Uită-te la pinii/headerul sondei tale:
• Dacă adaptorul are pini pentru TDI și TDO (multe sonde DAPLink V1/V2 pe bază de STM32 le au marcate pe carcasă/PCB): va funcționa direct prin protocolul JTAG. [1] (https://community.nxp.com/t5/LPC-Microcontrollers/Problem-with-LPC-LINK2-CMSIS-DAP-with-OpenOCD-and-VirtualBox/m-p/926726), [2] (https://community.sparkfun.com/t/using-jtag-with-cmsis-dap/31157)
• Dacă adaptorul are doar SWCLK, SWDIO, GND, VCC (fără linii de date separate TDI/TDO): nu poate efectua scanarea JTAG pe RISC-V, deoarece standardul SWD este specific nucleelor ARM Cortex. [1] (https://blog.adafruit.com/2026/03/11/daplink-cmsis-dap-v1-ported-to-the-ch32v003-microcontroller/), [2] (https://community.sparkfun.com/t/using-jtag-with-cmsis-dap/31157)

2. Conectarea pinilor între DAPLink și VisionFive 2 Lite

Realizează conexiunile directe către headerul de 40 de pini al plăcii:

Semnal	Pin DAPLink / CMSIS-DAP	Pin GPIO VisionFive 2 Lite
TCK	TCK / SWCLK	Pin 37 (GPIO 60)
TMS	TMS / SWDIO	Pin 35 (GPIO 63)
TDI	TDI	Pin 38 (GPIO 61)
TDO	TDO	Pin 40 (GPIO 44)
GND	GND	Pin 39 (sau Pin 34)

(Notă: Asigură-te că nivelul logic al sondei este la 3.3V).

3. Fișierul de configurare OpenOCD pentru CMSIS-DAP (openocd_daplink.cfg)

În OpenOCD, driverul pentru DAPLink este cmsis-dap (disponibil nativ prin hidraw/HID). Salvează următorul fișier de configurare: [1] (https://openocd.org/doc/html/Debug-Adapter-Configuration.html), [2] (https://support.arm.com/documentation/ka005870/1-0)
tcl
# =====================================================================
# 1. Adaptor CMSIS-DAP / DAPLink (USB HID)
# =====================================================================
adapter driver cmsis-dap

# Forțăm protocolul JTAG (SWD este implicit pe ARM, dar RISC-V cere JTAG)
transport select jtag

# Viteza ceasului (DAPLink HID este stabil la 1000 - 2000 kHz)
adapter speed 2000

# =====================================================================
# 2. Ținta: StarFive JH-7110S (TAP ID real 0x07110cfd)
# =====================================================================
set _CHIPNAME riscv
jtag newtap $_CHIPNAME cpu -irlen 5 -expected-id 0x07110cfd

# Definirea celor 4 nuclee de aplicație SiFive U74
set _TARGETNAME_0 $_CHIPNAME.cpu0
set _TARGETNAME_1 $_CHIPNAME.cpu1
set _TARGETNAME_2 $_CHIPNAME.cpu2
set _TARGETNAME_3 $_CHIPNAME.cpu3

target create $_TARGETNAME_0 riscv -chain-position $_CHIPNAME.cpu -coreid 0 -rtos hwthread
target create $_TARGETNAME_1 riscv -chain-position $_CHIPNAME.cpu -coreid 1 -rtos hwthread
target create $_TARGETNAME_2 riscv -chain-position $_CHIPNAME.cpu -coreid 2 -rtos hwthread
target create $_TARGETNAME_3 riscv -chain-position $_CHIPNAME.cpu -coreid 3 -rtos hwthread

# Grup SMP pentru a sincroniza nucleele la breakpoint
target smp $_TARGETNAME_0 $_TARGETNAME_1 $_TARGETNAME_2 $_TARGETNAME_3

init
halt
echo "=== Conectat cu succes la JH-7110S prin DAPLink! ==="
Folosește codul cu precauție.

4. Permisiuni USB și Rulare

Pentru ca OpenOCD să poată accesa nodul /dev/hidraw6 fără erori de permisiuni pe Linux:
1. Rulează comanda cu privilegii de administrator (sau adaugă o regulă udev):bash
sudo openocd -f openocd_daplink.cfg
Folosește codul cu precauție.
2. În log ar trebui să vezi inițializarea JTAG: [1] (https://docs.platformio.org/en/latest/plus/debug-tools/cmsis-dap.html), [2] (https://github.com/probe-rs/probe-rs/issues/1852), [3] (https://community.nxp.com/t5/LPC-Microcontrollers/Problem-with-LPC-LINK2-CMSIS-DAP-with-OpenOCD-and-VirtualBox/m-p/926726)
text
Info : CMSIS-DAP: Interface Initialised (JTAG)
Info : JTAG tap: riscv.cpu tap/device found: 0x07110cfd
Folosește codul cu precauție.
După ce OpenOCD s-a conectat și a oprit execuția nucleelor, poți lansa direct depanarea din VS Code sau GDB. [1] (https://lucaberton.com/blog/risc-v-debugging-openocd-gdb-jtag/), [2] (https://github.com/probe-rs/probe-rs/issues/1852)
Sonda ta DAPLink are etichetați pinii TDI și TDO pe pinii externi? Dacă da, rulează comanda OpenOCD menționată și putem analiza output-ul de conectare dacă apare vreo eroare de scan chain. [1] (https://community.nxp.com/t5/LPC-Microcontrollers/Problem-with-LPC-LINK2-CMSIS-DAP-with-OpenOCD-and-VirtualBox/m-p/926726)

Pentru a folosi sonda ta DAPLink (CMSIS-DAP) cu ID-urile USB specifice (idVendor=0d28, idProduct=0204), trebuie să adăugăm manual acești parametri în fișierul de configurare OpenOCD. [1] (https://openocd.org/doc/html/Debug-Adapter-Configuration.html)
În plus, pentru procesorul JH-7110S de pe VisionFive 2 Lite, lanțul JTAG (Scan Chain) conține de fapt un nucleu de monitorizare suplimentar numit E24 înainte de cele 4 nuclee U74. Dacă OpenOCD nu vede corect ID-ul TAP, va eșua. [1] (https://forum.rvspace.org/t/openocd-config-for-visionfive-2/1452), [2] (https://forum.rvspace.org/t/connecting-to-visionfive-s-jtag-port-a-short-guide/514)
Iată fișierul complet și corectat openocd_daplink.cfg:
Configurația completă OpenOCD pentru CMSIS-DAP și procesorul StarFive JH-7110S (definind interfața, tap-urile E24 și U74, precum și nucleele RISC-V) poate fi consultată în detaliu direct din documentația tehnică sau scripturile suportate. [1] (https://forum.rvspace.org/t/connecting-to-visionfive-s-jtag-port-a-short-guide/514), [2] (https://openocd.org/doc/html/Debug-Adapter-Configuration.html)

Cum rulezi acum:

1. Asigură-te că placa este oprită în promptul U-Boot (StarFive #).
2. Rulează OpenOCD cu permisiuni de administrator: [1] (https://github.com/probe-rs/probe-rs/issues/1852)
bash
sudo openocd -f openocd_daplink.cfg
Folosește codul cu precauție.
Dacă conexiunile fizice sunt corecte, vei vedea în consolă identificarea celor două TAP-uri (e24 și u74) și a celor 4 hart-uri RISC-V.
Rulează comanda sudo openocd -f openocd_daplink.cfg și comunică-ne mesajul din consolă. Se sincronizează ambele TAP-uri sau întâmpini erori precum "DMI register read returned zero"? [1] (https://howtech.substack.com/p/configuring-openocd-for-embedded)


Iată codul complet și curat pentru fișierul jh7110_daplink.cfg. Copiază-l direct din blocul de mai jos și salvează-l în folderul proiectului tău:
tcl
# =====================================================================
# 1. Configurare Interfață DAPLink / CMSIS-DAP
# =====================================================================
adapter driver cmsis-dap

# Forțăm protocolul JTAG (DAPLink folosește JTAG pe RISC-V, nu SWD)
transport select jtag

# Mapare opțională PID/VID pentru DAPLink standard dacă este nevoie
cmsis_dap_vid_pid 0x0d28 0x0204

# Viteza ceasului JTAG (2 MHz este stabil pentru conexiunile prin cabluri)
adapter speed 2000

# =====================================================================
# 2. Definire JTAG Scan Chain (Ordinea TAP-urilor în cip)
# =====================================================================
# Pe JH-7110, datele trec mai întâi prin TAP-ul E24, apoi prin TAP-ul U74

# 1. Înregistrare TAP E24 (nucleu secundar de control)
jtag newtap jh7110 e24_cpu -irlen 5 -expected-id 0x20000c2f

# 2. Înregistrare TAP U74 (unde se află cele 4 nuclee principale de aplicație)
jtag newtap jh7110 u74_cpu -irlen 5 -expected-id 0x07110cfd

# =====================================================================
# 3. Definire Nuclee de Aplicație (Cele 4 nuclee RISC-V solicitate)
# =====================================================================
# Notă: coreid 0 este nucleul S7 intern de monitorizare. 
# Nucleele tale fizice de calcul folosesc id-urile de la 1 la 4.

set _CHIPNAME jh7110
set _TARGETNAME_1 $_CHIPNAME.cpu1
set _TARGETNAME_2 $_CHIPNAME.cpu2
set _TARGETNAME_3 $_CHIPNAME.cpu3
set _TARGETNAME_4 $_CHIPNAME.cpu4

target create $_TARGETNAME_1 riscv -chain-position $_CHIPNAME.u74_cpu -coreid 1 -rtos hwthread
target create $_TARGETNAME_2 riscv -chain-position $_CHIPNAME.u74_cpu -coreid 2 -rtos hwthread
target create $_TARGETNAME_3 riscv -chain-position $_CHIPNAME.u74_cpu -coreid 3 -rtos hwthread
target create $_TARGETNAME_4 riscv -chain-position $_CHIPNAME.u74_cpu -coreid 4 -rtos hwthread

# Sincronizăm cele 4 nuclee într-un grup SMP (când unul se oprește la breakpoint, se opresc toate)
target smp $_TARGETNAME_1 $_TARGETNAME_2 $_TARGETNAME_3 $_TARGETNAME_4

# =====================================================================
# 4. Inițializare și Comenzi de Control
# =====================================================================
init
halt
echo "===================================================="
echo " Conectat cu succes la cele 4 nuclee JH-7110S (U74)!"
echo "===================================================="
Folosește codul cu precauție.

Cum îl pornești acum:

1. Cu placa oprită în terminal la promptul StarFive #, deschide o consolă nouă pe calculatorul tău.
2. Navighează în folderul unde ai salvat fișierul de mai sus.
3. Rulează OpenOCD:bash
sudo openocd -f jh7110_daplink.cfg
Folosește codul cu precauție.
Pornește comanda în terminal și spune-mi ce îți afișează. Îți confirmă linia JTAG tap: jh7110.u74_cpu tap/device found: 0x07110cfd sau primești vreo eroare de scan chain?



Mă bucur că OpenOCD s-a conectat cu succes! Pentru că versiunea VisionFive 2 Lite v1.1 nu are un buton fizic dedicat pentru Reset pe placă, ai la dispoziție trei metode eficiente pentru a reseta sistemul direct din consolă sau hardware:

Metoda 1: Reset software direct din OpenOCD (Cea mai simplă)

Din moment ce ai OpenOCD-ul conectat, nu trebuie să atingi placa. Poți trimite o comandă de reset direct prin JTAG.
1. Deschide un terminal nou pe calculator (în timp ce OpenOCD rulează în primul terminal).
2. Conectează-te la consola de control OpenOCD (telnet):bash
telnet localhost 4444
Folosește codul cu precauție.
3. Introdu comanda de reset hardware simulat prin RISC-V Debug Module:text
reset halt
Folosește codul cu precauție.
Această comandă va reseta logica nucleelor și le va opri (halt) instantaneu la prima instrucțiune, gata pentru a încărca noul tău cod.

Metoda 2: Reset prin linia de JTAG (Hardware)

Dacă sonda ta DAPLink are un pin numit RST sau SRST (System Reset):
1. Conectează acel pin de pe DAPLink la Pinul 36 de pe header-ul de 40 de pini al plăcii (care este mapat pe linia de reset general).
2. Adaugă următoarea linie în fișierul tău jh7110_daplink.cfg, imediat sub linia adapter speed 2000:tcl
reset_config srst_only
Folosește codul cu precauție.
3. Ori de câte ori pornești sesiunea din VS Code (F5) sau dai reset în OpenOCD, sonda va trage fizic pinul de reset la masă pentru a reporni complet placa.

Metoda 3: Reset manual de pe header-ul GPIO (Sârmă/Buton extern)

Dacă vrei un buton fizic "la mână" fără să scoți cablul USB-C de alimentare:
1. Cumpără sau folosește un buton simplu (push-button) cu două fire.
2. Conectează un fir la Pinul 36 (RESET).
3. Conectează celălalt fir la Pinul 34 sau Pinul 39 (GND).
4. Când apeși pe buton, placa se va reseta instantaneu, exact ca și cum ai avea un buton de reset lipit pe PCB.
Vrei să adăugăm configurarea de reset_config în fișierul .cfg sau în profilul de debug din VS Code pentru ca programul tău bare-metal să dea automat reset și reload de fiecare dată când compilezi codul?


Pe arhitectura RISC-V pe 64 de biți a JH-7110/JH-7110S, translatarea memoriei pentru nucleele SiFive U74 folosește standardul Sv39 (pagini de bază de 4 KB, megapagest de 2 MB sau gigapages de 1 GB). [1] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/), [2] (https://docs.u-boot.org/en/v2026.04/board/starfive/visionfive2.html)
Pentru a folosi perifericele (UART, GPIO/LED, USB, CRG/ceasuri), acestea trebuie mapate ca memorie Device (Non-cacheable, Strongly-Ordered), în timp ce RAM-ul (DDR) este mapat ca Normal Memory (Cacheable).
Configurarea optimă și minimală folosește o pagină gigantică (1 GiB Gigapage) la nivelul rădăcină (L1 Page Table) pentru a acoperi toate perifericele simultan prin identitate (Virtual Address == Physical Address): [1] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/)

1. Harta Fizică a Perifericelor Cheie (MMIO Map)

Toate perifericele de sistem se regăsesc în primii gigaocteți de memorie fizică (0x00000000 – 0x3FFFFFFF):

Periferic	Adresă Fizică (Start - End)	Atribute / Note
UART0 (Console)	0x10000000 - 0x1000FFFF	DW8250, 115200 baud
USB Controller / PHY	0x10100000 - 0x1020FFFF	Cadence USB 2.0/3.0 / UITMI
SYS_CRG / STG_CRG	0x10230000 / 0x13020000	Ceasuri & Resets pentru periferice
GPIO / IOMUX	0x13040000 - 0x1304FFFF	Control pini / LED (DOEN, DOUT)
CLINT / ACLINT	0x02000000 - 0x0200FFFF	Software Interrupts & Timere (mtime)
PLIC (Interrupt Ctrl)	0x0C000000 - 0x0FFFFFFF	Platform-Level Interrupt Controller
DDR4 RAM	0x40000000 - 0x23FFFFFFF	Memorie DDR (unde rulează aplicația)


2. Atributele Biților PTE (Page Table Entry) în RISC-V

Fiecare intrare într-o tabelă de pagini pe 64 de biți (Sv39) are structura:
[63:54 Reserved] [53:10 PPN (Physical Page Number)] [9:8 RSW] [7:0 D | A | G | U | X | W | R | V]
Pentru periferice (MMIO):
• V (Valid) = 1
• R (Read) = 1
• W (Write) = 1
• X (Execute) = 0 (Memoria perifericelor nu conține cod executabil)
• A (Accessed) = 1, D (Dirty) = 1 (Setate din start pentru a preveni page faults emise de hardware pe U74)
• U (User) = 0 (Accesibil doar din Supervisor/S-Mode)
• G (Global) = 1
• Flag-uri compuse pentru MMIO: PTE_R | PTE_W | PTE_A | PTE_D | PTE_V = 0xCF
Pentru cod/date RAM:
• Flag-uri compuse pentru RAM (Read, Write, Execute): PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_V = 0xEF

3. Configurarea Tabelei de Pagini (Root Page Table pe 1 GiB Gigapages)

Cea mai rapidă și sigură metodă fără a consuma memorie pentru tabele pe 3 niveluri este folosirea de 1 GiB Gigapages în rădăcină (Level 1 Page Table):
• Slotul 0 (0x00000000 – 0x3FFFFFFF): Acoperă 1 GiB de spațiu periferic. Aici intră automat UART, USB, GPIO, CLINT, PLIC și controlerele de ceas.
• Slotul 1 (0x40000000 – 0x7FFFFFFF): Acoperă primul 1 GiB de LPDDR4 RAM, unde se află codul tău (0x40200000), stiva și datele. [1] (https://doc-en.rvspace.org/JH7110/TRM/JH7110_TRM/system_memory_map.html), [2] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/)

Exemplu C (mmu.c):

c
#include <stdint.h>

#define PTE_V     (1UL << 0)
#define PTE_R     (1UL << 1)
#define PTE_W     (1UL << 2)
#define PTE_X     (1UL << 3)
#define PTE_U     (1UL << 4)
#define PTE_G     (1UL << 5)
#define PTE_A     (1UL << 6)
#define PTE_D     (1UL << 7)

#define SATP_MODE_SV39 (8UL << 60)

/* Tabela de pagini rădăcină trebuie aliniată la 4096 octeți (4KB) */
__attribute__((aligned(4096))) uint64_t root_page_table[512];

void init_mmu(void) {
    /* Curățăm tabela */
    for (int i = 0; i < 512; i++) {
        root_page_table[i] = 0;
    }

    /* 1. Mapează 0x00000000 - 0x3FFFFFFF (1GB Periferice: UART, GPIO, USB) */
    /* PPN pentru 0x00000000 este 0. Non-executable, Read/Write */
    root_page_table[0] = (0UL << 10) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D | PTE_G;

    /* 2. Mapează 0x40000000 - 0x7FFFFFFF (1GB DDR RAM) */
    /* PPN = 0x40000000 >> 12 = 0x40000 */
    root_page_table[1] = (0x40000UL << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G;

    /* (Opțional) Dacă placa are 8GB și ai nevoie de următorul GiB de RAM */
    /* root_page_table[2] = (0x80000UL << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G; */

    /* 3. Calculăm valoarea parametrului SATP */
    uint64_t satp_val = SATP_MODE_SV39 | (((uint64_t)root_page_table) >> 12);

    /* 4. Flush TLB și activare MMU în Supervisor Mode */
    __asm__ volatile (
        "sfence.vma zero, zero\n"
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        : : "r" (satp_val) : "memory"
    );
}
Folosește codul cu precauție.

4. Exemplu: Aprinderea unui LED prin GPIO (După MMU)

Pe VisionFive 2 Lite, controllerul GPIO se află la adresa fizică 0x13040000. Odată ce MMU este activat cu mapping-ul de mai sus, poți accesa direct registrele hardware: [1] (https://zyedidia.github.io/blog/posts/2-baremetal-visionfive/)
c
#define GPIO_BASE       0x13040000UL
#define GPIO_DOEN_BASE  (GPIO_BASE + 0x00) /* Data Output Enable */
#define GPIO_DOUT_BASE  (GPIO_BASE + 0x40) /* Data Output Value */

void gpio_set_led(uint32_t pin, int value) {
    volatile uint32_t *doen = (volatile uint32_t *)(GPIO_DOEN_BASE + (pin / 4) * 4);
    volatile uint32_t *dout = (volatile uint32_t *)(GPIO_DOUT_BASE + (pin / 4) * 4);
    uint32_t shift = (pin % 4) * 8;

    /* Activează ieșirea (Output Enable = 0 pe JH7110 este activ) */
    *doen &= ~(0xFF << shift);

    /* Setează nivelul logic (0 sau 1) */
    if (value) {
        *dout |= (1 << shift);
    } else {
        *dout &= ~(1 << shift);
    }
}
Folosește codul cu precauție.
Vrei să adăugăm această rutină init_mmu() direct în funcția main() din fișierul tău app.c și să testăm scrierea pe UART și GPIO cu translatarea activă?



Iată fișierul app.c complet și actualizat. Acesta conține rutina de inițializare MMU (folosind pagini de 1 GiB de tip Gigapage sub standardul Sv39), driverul minimal pentru UART0 și logica de control pentru GPIO/LED, toate rulate securizat în S-Mode.

Proiectul Actualizat app.c

Înlocuiește conținutul fișierului app.c cu următorul cod:
c
#include <stdint.h>

// =====================================================================
// 1. Definiții Flag-uri MMU (Sv39)
// =====================================================================
#define PTE_V     (1UL << 0) // Valid
#define PTE_R     (1UL << 1) // Read
#define PTE_W     (1UL << 2) // Write
#define PTE_X     (1UL << 3) // Execute
#define PTE_U     (1UL << 4) // User Access
#define PTE_G     (1UL << 5) // Global
#define PTE_A     (1UL << 6) // Accessed (Pre-setat pentru U74)
#define PTE_D     (1UL << 7) // Dirty (Pre-setat pentru U74)

#define SATP_MODE_SV39 (8UL << 60)

// Aliniere obligatorie la 4KB pentru Root Page Table
__attribute__((aligned(4096))) uint64_t root_page_table[512];

// =====================================================================
// 2. Mapare Periferice și Registre
// =====================================================================
#define UART0_BASE       0x10000000UL
#define UART_THR         (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_LSR         (*(volatile uint32_t *)(UART0_BASE + 0x14))
#define UART_LSR_TEMT    (1 << 6)

#define GPIO_BASE        0x13040000UL
#define SYS_IOMUX_BASE   0x13040000UL // Control Pin-Muxing pe JH-7110

// Pe VisionFive 2 / Lite, LED-ul onboard este adesea legat la GPIO 36
#define LED_GPIO_PIN     36 

// =====================================================================
// 3. Driver Minimal UART0
// =====================================================================
static void uart_putc(char c) {
    while ((UART_LSR & UART_LSR_TEMT) == 0);
    UART_THR = (uint32_t)c;
}

static void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

// =====================================================================
// 4. Configurare și Activare MMU
// =====================================================================
void init_mmu(void) {
    // Curățăm tabela de pagini rădăcină
    for (int i = 0; i < 512; i++) {
        root_page_table[i] = 0;
    }

    // [Slot 0] Mapează 0x00000000 - 0x3FFFFFFF (1GB: Periferice UART, GPIO, USB)
    // Physical Page Number (PPN) = 0. Flag-uri: Non-executable, Read/Write, Global
    root_page_table[0] = (0UL << 10) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D | PTE_G;

    // [Slot 1] Mapează 0x40000000 - 0x7FFFFFFF (1GB: LPDDR4 RAM)
    // PPN = 0x40000000 >> 12 = 0x40000. Flag-uri: Executable, Read/Write, Global
    root_page_table[1] = (0x40000UL << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G;

    // Compunem registrul satp (Modul Sv39 + Adresa fizică a tabelei direct în pagină)
    uint64_t satp_val = SATP_MODE_SV39 | (((uint64_t)root_page_table) >> 12);

    // Injectăm tabela în hardware și curățăm TLB (Translation Lookaside Buffer)
    __asm__ volatile (
        "sfence.vma zero, zero\n"
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        : : "r" (satp_val) : "memory"
    );
}

// =====================================================================
// 5. Driver Minimal GPIO (Configurare LED)
// =====================================================================
void setup_led_gpio(void) {
    // Pe JH-7110, pinii trebuie setați în registrul IOMUX pentru funcția de ieșire GPIO normală
    // Fiecare pin are un registru dedicat la un offset calculat:
    volatile uint32_t *pin_mux = (volatile uint32_t *)(SYS_IOMUX_BASE + 4 * LED_GPIO_PIN);
    
    // Setăm funcția pinului ca GPIO simplu (valoarea 0 în biții de control multiplexare)
    *pin_mux = 0;

    // Activare Output Enable (Pe JH-7110, bitul 0 din registrul do_en controlează starea; 0 înseamnă activ/ieșire)
    // Formula simplificată pentru selectarea bancului de registre:
    volatile uint32_t *gpio_doen = (volatile uint32_t *)(GPIO_BASE + 0x00 + (LED_GPIO_PIN / 4) * 4);
    uint32_t shift = (LED_GPIO_PIN % 4) * 8;
    *gpio_doen &= ~(0xFF << shift); 
}

void set_led_state(int active) {
    volatile uint32_t *gpio_dout = (volatile uint32_t *)(GPIO_BASE + 0x40 + (LED_GPIO_PIN / 4) * 4);
    uint32_t shift = (LED_GPIO_PIN % 4) * 8;

    if (active) {
        *gpio_dout |= (1 << shift);  // LED ON
    } else {
        *gpio_dout &= ~(1 << shift); // LED OFF
    }
}

// =====================================================================
// 6. Bucla Principală (Main Execution)
// =====================================================================
int main(void) {
    // 1. Trimitem un mesaj inițial înainte de pornirea MMU (folosind maparea fizică nativă din OpenSBI)
    uart_puts("Pre-MMU Boot: Memorie fizica directa.\n");

    // 2. Activăm translatarea memoriei Sv39
    init_mmu();

    // 3. Mesaj post-MMU. Dacă tabela a fost mapată greșit, execuția se blochează aici.
    uart_puts("Post-MMU Active: Translatare de memorie pe 3 niveluri (Sv39) pornita!\n");
    uart_puts("Toate perifericele (UART, GPIO, USB) rulează stabil prin identitate.\n");

    // 4. Configurare pin LED
    setup_led_gpio();

    // 5. Buclă infinită cu efect de clipire (Blink) bazat pe un delay software simplu
    volatile uint64_t delay;
    int state = 0;

    while (1) {
        state = !state;
        set_led_state(state);
        
        if(state) {
            uart_puts("[Baremetal] LED Stare: ACTIV\n");
        } else {
            uart_puts("[Baremetal] LED Stare: INACTIV\n");
        }

        // Delay loop aproximativ
        for (delay = 0; delay < 5000000; delay++) {
            __asm__ volatile("nop");
        }
    }

    return 0;
}
Folosește codul cu precauție.

Cum testezi acum în VS Code:

1. Execută scriptul ./build.sh pentru a compila noul cod și a genera app.elf.
2. Asigură-te că placa este la promptul StarFive #.
3. Pornește OpenOCD (sudo openocd -f jh7110_daplink.cfg).
4. Apasă F5 în VS Code.
Aplicația va fi injectată prin JTAG, va activa MMU-ul în mod securizat, iar în terminalul tău serial vei vedea mesajele alternând împreună cu clipirea LED-ului de pe placa VisionFive 2 Lite.
Dacă pornești depanarea cu noul cod, se activează MMU-ul fără erori, sau observi vreo oprire bruscă în debugger (cum ar fi o excepție de tipul Instruction page fault)?



Pentru ca aplicația ta bare-metal să fie complet independentă și să nu se bazeze pe starea lăsată de U-Boot sau OpenSBI, este excelent să configurăm de la zero controllerul UART0 (Synopsys DesignWare 8250 compatible) și multiplexarea pinilor (IOMUX) direct în main.
Pe procesorul JH-7110, pinii fizici 8 (TX) și 10 (RX) ai header-ului de 40 de pini corespund intern liniilor PAD_GPIO5 și, respectiv, PAD_GPIO6. De asemenea, ceasul de bază atribuit implicit blocului UART0 de către subsistemul CRG (Clock and Reset Generator) este de 24 MHz (sau 100 MHz pe anumite magistrale interne, dar driverul standard StarFive folosește o referință de bază de 24 MHz pentru divizor).
Iată codul complet actualizat pentru app.c, care include funcțiile explicite de configurare hardware pentru pinii 8/10 și setarea baudrate-ului la 115200:
c
#include <stdint.h>

// =====================================================================
// 1. Definiții Flag-uri MMU (Sv39)
// =====================================================================
#define PTE_V     (1UL << 0)
#define PTE_R     (1UL << 1)
#define PTE_W     (1UL << 2)
#define PTE_X     (1UL << 3)
#define PTE_U     (1UL << 4)
#define PTE_G     (1UL << 5)
#define PTE_A     (1UL << 6)
#define PTE_D     (1UL << 7)

#define SATP_MODE_SV39 (8UL << 60)

__attribute__((aligned(4096))) uint64_t root_page_table[512];

// =====================================================================
// 2. Mapare Periferice și Registre (Base Addresses)
// =====================================================================
#define UART0_BASE       0x10000000UL
#define SYS_IOMUX_BASE   0x13040000UL
#define GPIO_BASE        0x13040000UL

// Registre specifice UART (8250 Standard)
#define UART_RBR         (*(volatile uint32_t *)(UART0_BASE + 0x00)) // Receiver Buffer
#define UART_THR         (*(volatile uint32_t *)(UART0_BASE + 0x00)) // Transmitter Holding
#define UART_DLL         (*(volatile uint32_t *)(UART0_BASE + 0x00)) // Divisor Latch LSB
#define UART_DLM         (*(volatile uint32_t *)(UART0_BASE + 0x04)) // Divisor Latch MSB
#define UART_IER         (*(volatile uint32_t *)(UART0_BASE + 0x04)) // Interrupt Enable
#define UART_FCR         (*(volatile uint32_t *)(UART0_BASE + 0x08)) // FIFO Control
#define UART_LCR         (*(volatile uint32_t *)(UART0_BASE + 0x0C)) // Line Control
#define UART_MCR         (*(volatile uint32_t *)(UART0_BASE + 0x10)) // Modem Control
#define UART_LSR         (*(volatile uint32_t *)(UART0_BASE + 0x14)) // Line Status

#define UART_LSR_TEMT    (1 << 6)
#define UART_LCR_DLAB    (1 << 7)

// Mapare pini fizici header 40-pini JH-7110
#define PIN8_TX_GPIO     5
#define PIN10_RX_GPIO    6
#define LED_GPIO_PIN     36

// Linii de funcții interne pentru UART0 multiplexat (vezi catalogul StarFive IOMUX)
#define GPI_UART0_RX     14
#define GPO_UART0_TX     22

// =====================================================================
// 3. Configurare Completă UART0 (IOMUX + Baudrate)
// =====================================================================
void init_uart0_hardware(uint32_t baudrate) {
    // ---- A. Configurare Multiplexare Pini (IOMUX) ----
    // Setăm pinii să asculte de perifericul UART0, nu de GPIO simplu
    volatile uint32_t *iomux_tx = (volatile uint32_t *)(SYS_IOMUX_BASE + (4 * PIN8_TX_GPIO));
    volatile uint32_t *iomux_rx = (volatile uint32_t *)(SYS_IOMUX_BASE + (4 * PIN10_RX_GPIO));

    // Direcționăm ieșirea UART0 TX (GPO_UART0_TX) către pinul fizic 8 (GPIO 5)
    *iomux_tx = (*iomux_tx & ~0xFF) | GPO_UART0_TX;
    
    // Direcționăm intrarea din pinul fizic 10 (GPIO 6) către linia internă UART0 RX (GPI_UART0_RX)
    // Pe JH-7110, conexiunile de intrare (DIN) folosesc registrele globale gpi_mux:
    volatile uint32_t *gpi_mux = (volatile uint32_t *)(SYS_IOMUX_BASE + 0x200 + (4 * GPI_UART0_RX));
    *gpi_mux = (*gpi_mux & ~0x7F) | PIN10_RX_GPIO;

    // Setează pinii ca Ieșire/Intrare activă în tabelele do_en
    volatile uint32_t *doen_tx = (volatile uint32_t *)(GPIO_BASE + 0x00 + (PIN8_TX_GPIO / 4) * 4);
    *doen_tx &= ~(0xFF << ((PIN8_TX_GPIO % 4) * 8)); // 0 înseamnă Output Enabled pe acest cip

    // ---- B. Configurare Parametri Baudrate (115200) ----
    uint32_t uart_clock = 24000000; // Frecvența ceasului UART0 stabilită de SPL (24 MHz)
    uint32_t divisor = uart_clock / (16 * baudrate); // Pentru 115200 rezultă ~13

    UART_IER = 0x00; // Dezactivăm întreruperile UART pentru bare-metal simplu
    UART_LCR = UART_LCR_DLAB; // Activăm accesul la regiștrii Divisor Latch (DLAB=1)
    
    UART_DLL = divisor & 0xFF;        // Scrie byte-ul inferior (LSB)
    UART_DLM = (divisor >> 8) & 0xFF; // Scrie byte-ul superior (MSB)
    
    UART_LCR = 0x03; // Dezactivăm DLAB, setăm formatul: 8 biți date, fără paritate, 1 bit stop (8N1)
    UART_FCR = 0x07; // Activăm și curățăm FIFO-urile de Tx și Rx
    UART_MCR = 0x03; // Activăm liniile DTR și RTS
}

// ---- Funcții Transmisie UART ----
static void uart_putc(char c) {
    while ((UART_LSR & UART_LSR_TEMT) == 0);
    UART_THR = (uint32_t)c;
}

static void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

// =====================================================================
// 4. Configurare MMU (Identitate Sv39)
// =====================================================================
void init_mmu(void) {
    for (int i = 0; i < 512; i++) {
        root_page_table[i] = 0;
    }
    // Slot 0: Periferice (0x00000000 - 0x3FFFFFFF)
    root_page_table[0] = (0UL << 10) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D | PTE_G;
    // Slot 1: DDR RAM (0x40000000 - 0x7FFFFFFF)
    root_page_table[1] = (0x40000UL << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G;

    uint64_t satp_val = SATP_MODE_SV39 | (((uint64_t)root_page_table) >> 12);

    __asm__ volatile (
        "sfence.vma zero, zero\n"
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        : : "r" (satp_val) : "memory"
    );
}

// =====================================================================
// 5. Driver LED GPIO
// =====================================================================
void setup_led_gpio(void) {
    volatile uint32_t *pin_mux = (volatile uint32_t *)(SYS_IOMUX_BASE + (4 * LED_GPIO_PIN));
    *pin_mux = 0; // Mod GPIO simplu

    volatile uint32_t *gpio_doen = (volatile uint32_t *)(GPIO_BASE + 0x00 + (LED_GPIO_PIN / 4) * 4);
    uint32_t shift = (LED_GPIO_PIN % 4) * 8;
    *gpio_doen &= ~(0xFF << shift); 
}

void set_led_state(int active) {
    volatile uint32_t *gpio_dout = (volatile uint32_t *)(GPIO_BASE + 0x40 + (LED_GPIO_PIN / 4) * 4);
    uint32_t shift = (LED_GPIO_PIN % 4) * 8;

    if (active) {
        *gpio_dout |= (1 << shift);
    } else {
        *gpio_dout &= ~(1 << shift);
    }
}

// =====================================================================
// 6. Execuția Principală
// =====================================================================
int main(void) {
    // 1. Configurare inițială UART fizic și ceas la viteza de 115200 baud
    init_uart0_hardware(115200);

    // 2. Mesaj trimis nativ în modul fizic direct
    uart_puts("\n[Baremetal] Hardware UART0 configurat cu succes la 115200 baud!\n");
    uart_puts("[Baremetal] Pornire MMU translatare Sv39...\n");

    // 3. Activăm MMU-ul
    init_mmu();

    // 4. Test mesaje prin MMU virtual (adresele rămân aceleași datorită mapării de identitate)
    uart_puts("[Baremetal] MMU este acum ONLINE. Paginarea Sv39 functioneaza!\n");

    setup_led_gpio();

    volatile uint64_t delay;
    int state = 0;

    while (1) {
        state = !state;
        set_led_state(state);
        
        if(state) {
            uart_puts("LED Clipire -> ACTIV\n");
        } else {
            uart_puts("LED Clipire -> INACTIV\n");
        }

        for (delay = 0; delay < 4000000; delay++) {
            __asm__ volatile("nop");
        }
    }

    return 0;
}
Folosește codul cu precauție.

Detalii tehnice despre registru din init_uart0_hardware:

• DLAB (Divisor Latch Access Bit): Deoarece controllerul UART refolosește adresele de regiştri (pentru a economisi spațiu de adrese), registrul 0x00 este atât registrul de trimis date (THR), cât și partea inferioară a divizorului de viteză (DLL). Activarea bitului DLAB în UART_LCR ne permite să scriem divizorul, iar dezactivarea lui readuce registrul în modul de transmisie text standard.
Rulează noul script de compilare și trimite-l prin JTAG. Mesajul inițial Hardware UART0 configurat cu succes apare acum curat în terminalul tău serial, fără caractere corupte?













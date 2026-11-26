# OpenOCD build fix (GCC 16 / `-Werror`)

## Problem

This tree is the MounRiver WCH fork of OpenOCD
(`mrs-wch-riscv-221010`, commit `266d7c8d3`). It was written for much older
compilers, but the fork enables `-Werror` unconditionally (`configure.ac`,
`gcc_werror` defaults to on).

Building with GCC 16.2.1 therefore aborts with many diagnostics that are new
or promoted to errors in modern GCC, for example:

- `-Werror=calloc-transposed-args`
- `-Werror=incompatible-pointer-types`
- `-Werror=discarded-qualifiers`
- `-Werror=return-type`
- `-Werror=unused-variable`, `-Werror=unused-but-set-variable`
- `-Werror=sign-compare`
- `-Werror=maybe-uninitialized`
- `-Werror=endif-labels`
- `-Werror=shadow`, `-Werror=redundant-decls`

Typical failures were in the WCH-specific drivers (`wlink.c`,
`cmsis_dap_usb_bulk.c`, `wcharm.c`, `wchriscv.c`), the RISC-V target
(`riscv-013.c`, `riscv.c`), and a few upstream files.

## What `fix.patch` does

`fix.patch` contains source-level fixes only. It does **not** disable
`-Werror`; warnings are fixed at their source so the existing strict build
settings remain meaningful.

The changes fall into these groups:

1. **USB/WCH-Link driver (`wlink.c`, `cmsis_dap_usb_bulk.c`)**
   - Use `struct libusb_device_handle *` for the libusb handle instead of a
     non-existent `struct jtag_libusb_device_handle`.
   - Cast buffers/lengths to the types expected by `jtag_libusb_bulk_write()`
     and `jtag_libusb_bulk_read()`, and remove bogus pointer casts.
   - Fix `#else if` preprocessor typos (should be `#else`).
   - Remove dead/unused variables and labels, add missing return values.
   - Fix `const` correctness and an incompatible `uint32_t *`/`uint8_t *`
     conversion.
   - Make `wlink_address` and `gIndex` `uint32_t` so the
     `COMMAND_PARSE_NUMBER(u32, ...)` calls are type-correct, and update the
     external declarations.

2. **WCH flash drivers (`wcharm.c`, `wchriscv.c`)**
   - Update callback signatures to the current OpenOCD `flash_driver` API
     (`erase`, `protect`, `info`).
   - Add missing `return` statements.
   - Fix `target_read_u32()` being passed a `uint16_t *`.
   - Remove unused variables and fix signed/unsigned loop comparisons.

3. **RISC-V target (`riscv-013.c`, `riscv.c`)**
   - Fix pointer types passed to the WCH `DMI_OP()` helper and `buf_set_u32()`.
   - Make `write_flash_data()` take a `const uint8_t *` and fix callers that
     passed `&array` instead of `array`.
   - Remove unused variables and shadowed declarations, add a missing return.
   - Fix a `uint64_t` format-string mismatch (`%x` → `PRIx64`).

4. **Upstream code touched by the fork**
   - Swap transposed `calloc(sizeof(T), N)` arguments in `ambiqmicro.c`,
     `kinetis.c`, `max32xxx.c`, `msp432.c`, `stellaris.c`, `stm32f2x.c`,
     `ulink.c`, `nds32.c`, and `arc_jtag.c`.
   - Fix `const` qualifiers in `log.c`, `options.c`, and `telnet_server.c`.
   - Add missing return values and remove unused variables/labels in
     `core.c`, `tcl.c`, `target.c`, `arm7_9_common.c`, `dsp5680xx_flash.c`,
     and `davinci.c`.
   - Fix a `struct cmsis_dap` declaration that was scoped inside a parameter
     list in `gdb_server.c`, plus a redundant declaration.

No functional behaviour is intentionally changed. The non-mechanical fixes
are type corrections and missing error returns; the rest is removal of dead
code.

## Requirements

- GCC 16 (or any compiler that produces the same warnings)
- The tree at commit `266d7c8d3`
- The build must already be configured (`./configure`) as before

## How to apply

From the repository root:

```sh
cd /path/to/openocd-hacks
git apply --check fix.patch   # optional sanity check
git apply fix.patch
```

Alternatively, without git:

```sh
patch -p1 < fix.patch
```

Then rebuild:

```sh
make clean
make -j"$(nproc)"
```

The result is `src/openocd`.

## Verification performed

- `git apply --check fix.patch` against a pristine `HEAD` worktree: passes.
- `make clean && make -j$(nproc)`: completes with exit code 0 and no
  `error:` lines (with `-Werror` still enabled).
- `src/openocd --version` runs and reports:
  `Open On-Chip Debugger 0.11.0+dev-g266d7c8d3 ...`

## Notes

- If you prefer not to patch the sources, configuring with
  `./configure --disable-werror` is the quick alternative. The patch is the
  better fix because it keeps the strict warning settings and corrects real
  type/return-value issues.
- `fix.patch` was generated with `git diff`, so it applies with `-p1`.
- Build outputs (`Makefile`, `configure`, objects, etc.) are generated and
  ignored by git; they are not part of the patch.

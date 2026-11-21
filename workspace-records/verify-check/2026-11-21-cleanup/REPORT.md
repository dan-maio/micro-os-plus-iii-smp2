# Cleanup of the findings in `agy-analyze.md`

Date: 2026-11-21
Scope: the actionable findings of `agy-analyze.md`, plus an accuracy check of
the report itself.

## 1. What the report got right, and what was wrong

| Finding | Verdict | Cause |
|---|---|---|
| §5.1 missing `micro-os-plus::iii-cortexm` / `::iii-posix-arch` aliases | real | the SMP CMake rewrite renamed the primary aliases; the four upstream board files still link the old names |
| §5.2 `link-deps-all` duplicate and omissions | real | the duplicate is inherited verbatim from upstream; the 20 missing configurations are local, added board-by-board and never appended to the list |
| §5.3 `qemu-system-aarch64` bare command | real, but environmental | the build was configured by a direct `cmake` without `xpacks/.bin` on `PATH`, so `find_program` cached `NOTFOUND` |
| §5.4.1 Lyra's own `mutex-stress` | real | Lyra predates `UOS_TESTS_SUITES` |
| §5.4.2 dead `platform-support` / `QEMU_BUILD` | real | leftovers from before the shared `board-tests` builder |
| §5.4.3 unused aarch32 dependency in the aarch64 configs | **false** | no such line, symlink or dependency list entry exists; `git log -S` finds no commit that ever added it |
| §5.4.4 unused `ENABLE_HW_TESTS` | real | a leftover hardware-only flag |
| §5.4.5 native validator asymmetry | partly | deliberate: it needs `TRACE` + `OS_USE_TRACE_POSIX_STDOUT` and warning suppression; the builder itself supports a `host` suite |

The report also had count/toolchain errors: 22 platforms and 478 cases (actual
24 platform folders, 23 built configurations, 512 registered cases),
`aarch64-rpi4b` 26/25 (actual 22 QEMU + 22 hwd, plus fixtures), and
toolchains 14.2.1 (actual 15.2.1-1.1.1).

## 2. Changes

### 2.1 Ports — upstream aliases restored

- `cortexm/micro-os-plus-iii-cortexm/CMakeLists.txt`
  `add_library (micro-os-plus::iii-cortexm ALIAS micro-os-plus-iii-cortexm-interface)`
- `posix-arch/micro-os-plus-iii-posix-arch/CMakeLists.txt`
  `add_library (micro-os-plus::iii-posix-arch ALIAS micro-os-plus-iii-posix-arch-interface)`

The variants (`::cortexm`, `::cortexm-rp2350`, `::cortexm-rp2040`) remain.

### 2.2 Kernel — `tests/package.json`

- `link-deps-all`: the duplicate `native-cmake-gcc13-debug` became
  `native-cmake-gcc14-debug`; 20 entries added:
  `aarch64-rpi4b`, `2xcortex-m33`, `pico2-1cpu`, `cortexm-pico2`,
  `cortexm-pico`, `cortexm-pico2-pizero`,
  `cortexm-pico2-rp2350b-psram`, `cortexm-nucleof411`, `cortexm-weactf411`,
  `cortexm-weactf412` (debug and release each).
- Lyra's `commandCMakeReconfigure` override dropped, removing the unused
  `-D ENABLE_HW_TESTS=ON`.

The action now covers all 74 toolchain configurations (90 non-hidden
configurations minus the 16 compatibility aliases that inherit a `-gcc-`
configuration).

### 2.3 Kernel — Lyra uses the shared builder

`platforms/aarch32-luckfox-lyra/CMakeLists.txt` now sets
`UOS_TESTS_SUITES mutex-stress` and the bespoke `add_harness_suite()` is gone.
The image is `mutex-stress-hwd` and the CTest case
`aarch32-luckfox-lyra-mutex-stress-hwd` (21 hwd tests, 42 label entries with
the reset fixtures), the same shape as every other board.

### 2.4 Kernel — dead AArch targets and defines

Removed from the six AArch platform libraries
(`aarch32-rpi-zero-2w`, `aarch32-rpi3b`, `aarch32-luckfox-lyra`,
`aarch64-rpi-zero-2w`, `aarch64-rpi3b`, `aarch64-rpi4b`):

- `micro-os-plus::platform-support` (and its `<platform>-support-interface`);
- the unconditional `QEMU_BUILD` compile definition on `::platform`.

`tests/board-tests/CMakeLists.txt` already compiles
`platforms/<P>/src/platform-support.cpp` into `<platform>-test-startup` and
adds `QEMU_BUILD` only to the `qemu` variants.

### 2.5 Kernel — QEMU discovery

Every `find_program (_qemu qemu-system-arm|qemu-system-aarch64)` now carries
`HINTS "${CMAKE_BINARY_DIR}/xpacks/.bin"` (14 platform files). A direct
`cmake` configure therefore resolves the configuration's pinned emulator even
without `xpm`'s `PATH`.

## 3. Verification

### 3.1 Builds

All 23 configurations with build folders rebuilt:

Cortex-M0/M3/M4F/M7F, `2xcortex-m33`, `pico2-1cpu`, Pico, Pico 2, Pi-Zero
RP2350B, PSRAM, the three STM32F4 boards, AArch32 Zero 2 W/Pi 3 B/Lyra,
AArch64 Zero 2 W/Pi 3 B/Pi 4 B, native system (debug/release) and native
clang 19.

`native-cmake-clang19-debug` needed a clean reinstall: its old cache pointed
at `/usr/bin/clang++`, which cannot compile the tree against libc++ 20. After
`install`/`link-deps`/`prepare` with the xPack clang 19 it builds and passes.

`xpm run link-deps-all --dry-run` completes (exit 0) with no duplicate.

### 3.2 QEMU / host matrix

| Configuration | Result |
|---|---|
| `2xcortex-m33` | 4/4 |
| `aarch32-rpi-zero-2w` | 26/26 entries |
| `aarch32-rpi3b` | 24/24 entries (pipeline timed out once, passed on rerun) |
| `aarch64-rpi-zero-2w` | 26/26 |
| `aarch64-rpi3b` | 24/24 |
| `aarch64-rpi4b` | 32/32 (validator failed once, passed on rerun; 3 PC-driven cases SKIP) |
| `cortexm-pico` | 6/6 |
| `cortexm-pico2` | 6/6 |
| `cortexm-pico2-rp2350b-psram` | 3/3 |
| `pico2-1cpu` | 4/4 |
| `qemu-cortex-m0/m3/m4f/m7f` + m7f alias | 3/3 each |
| `native-cmake-sys-debug/release` | 20/20 each |
| `native-cmake-clang19-debug` | 20/20 |

The two failures are the documented flaky cases; both passed on rerun.

### 3.3 Not run

Hardware. No debug probe is currently connected (`lsusb` shows none), so the
Lyra `mutex-stress` hardware run in particular could not be executed; its
build and CTest registration were verified instead (OpenOCD reset fixture
failed with “unable to find a matching CMSIS-DAP device”).

## 4. What remains

The four upstream hardware platforms (`nucleo-f411re`, `nucleo-f767zi`,
`nucleo-h743zi`, `raspberrypi-pico`) still do not configure: their
`dependencies-folders.cmake` does not name the kernel, so the `::iii-*`
targets are absent. The compatibility aliases remove one of their two
blockers; making them build would require pointing those platforms at the
kernel, which is a separate change.

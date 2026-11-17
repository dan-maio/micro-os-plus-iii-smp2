---
title: "The SMP build and test system compared with the upstream µOS++ IIIe / xPack way, and how to bring it back"
subtitle: "micro-os-plus-iii (branch smp) compared with micro-os-plus/micro-os-plus-iii (branch xpack-development)"
date: 2026-11-16
---

## 0. About this document

This document compares the build and test system of the `smp` branch (this
workspace: `micro-os-plus-iii` and the ports `-aarch32`, `-aarch64`,
`-cortexm`, `-posix-arch`) with the upstream single-core project
<https://github.com/micro-os-plus/micro-os-plus-iii.git>, branch
`xpack-development`, and with the xPack way that upstream follows.

- **Part A** states the upstream / xPack principles (from their own
  documentation), shows what `smp` does differently, and judges each
  difference against those principles.
- **Part B** is a plan to bring `smp` back to the upstream way, step by step.

**Sources.** Every principle is taken from a document, with its location:

- upstream `xpack-development`: `README-DEVELOPER.md`,
  `README-MAINTAINER.md`, `tests/README.md`, and the code;
- the xpm documentation, <https://xpack.github.io/xpm/> (CLI pages for
  `link`, `install`, `run`, `uninstall`; metadata pages; the CMake
  integration guide; the folders page);
- the xPack site, <https://xpack.github.io/>;
- the µOS++ site, <https://micro-os-plus.github.io/> (the v7.0.0 release
  post, the "modular projects" page);
- for the `smp` side: the commit messages of the harness commits,
  `docs/smp-integration/xpack-dev-smp.md`, and the code.

**Correction of the first version of this document.** The first version
explained the `smp` deviations mostly in their own terms and presented them
as sound choices. Several of them contradict the upstream / xPack
principles. This version judges each one against those principles. Where a
question is not answered by any document, it says so.

---

# Part A: The principles, and where `smp` differs

## 1. The upstream / xPack principles

| # | Principle | Source |
|---|---|---|
| P1 | **Every component is a separate package with its own version.** Consumers install released, read-only copies. | xpm metadata pages; µOS++ "modular projects": components are "stored as one or more files in separate packages", each with "metadata, to define its own identity and a list of dependencies" |
| P2 | **Writable working copies are used through `xpm link`**, in two steps: `xpm link` in the working copy (creates `.link` in the global store), then `xpm link <package>` in the project. `-c/--config` links *"into an existing configuration build folder"*. This is the tool for developing several packages at the same time. | <https://xpack.github.io/xpm/docs/cli/xpm/link/>; upstream `README-DEVELOPER.md` ("Use development writable packages") |
| P3 | **Tools are binary xPacks, installed once per version in a shared store** and linked into each project / build configuration (`xpacks/.bin`, `build/<config>/xpacks/.bin`). Several versions can coexist; each configuration pins its versions. | xpm folders page: *"installing a single read-only instance in a central folder and creating links from the project to that folder"*; `install --config`, `run --config` pages |
| P4 | **One build configuration per platform / toolchain / build type**, each with its own build folder (`buildFolderRelativePath`, default `build/<config>`) and its own dependencies. | xpm `buildConfigurations` and `properties` pages |
| P5 | **The library is added by its tests with `add_subdirectory(".." "top-bin")`; other packages come from `xpacks/…`.** Plain CMake, INTERFACE targets. | xpm CMake integration guide; upstream `tests/cmake/tests-main.cmake` |
| P6 | **Each package tests itself, in its own `tests/` folder**, a separate xpm project with its own `package.json`. | xpm user guide: *"move all test-related metadata to a separate tests folder, with its own package.json"* |
| P7 | **The same test sources run unchanged on all platforms, with many toolchains.** | upstream `tests/README.md`: *"Exactly the same source files are used on all platforms, without changes"*; *"compile the sources with as many toolchains as possible, and run them on as many platforms as possible"* |
| P8 | **Portability: Windows, macOS, GNU/Linux.** Actions are shell commands, but must be portable. | xPack home: *"across multiple platforms (Windows, macOS, GNU/Linux)"*; upstream `tests/README.md`: *"The tests are performed on GNU/Linux, macOS and Windows"*; xpm actions page: *"many Unix shell commands do not have direct equivalents on Windows"* → use portable tools |
| P9 | **Board support is layered: architecture, device, board.** | µOS++ "modular projects": *"not monolithic, but modular, with three explicit layers"*: board, device, architecture. Upstream IIIe: the port (`-cortexm`) is architecture only; chip files are in the test platform's `device/` folder or in third-party xPacks (`arm-cmsis`, `raspberrypi-pico-sdk-xpack`) |

Note on IIIe vs IVe: the µOS++ v7.0.0 post says v7.0.0 *"will hopefully be
the final release of µOS++ in the current monolithic structure (IIIe)"*; IVe
moves to *"a modular structure, based on xPacks"*. So in IIIe the kernel is
one package, but the ports, the tools and third-party code are already
separate xPacks, and P1–P9 apply to them.

### 1.1 Upstream as it is today (reviewed 2026-11-17)

Checked directly against GitHub `micro-os-plus/<repo>`, branch
`xpack-development`, fetched into the repositories here without adding a
remote: kernel `43ef532f` (2026-10-08), `-cortexm` `687e975`,
`-posix-arch` `49282e6`.

- **Since the copy this plan was first built on** (`7f1ce5ca`), upstream
  took about 30 kernel commits, mostly single-core fixes that came out of
  the SMP work (rtos, estd, posix-io, semihosting, ARMv8-M exception
  handlers). In the **build and test system** only three QEMU device files
  changed (two MPS2 linker-script comments, the FPU enable in
  `system-cortexm.c`). Everything below still holds.
- **`tests/package.json`** (`minimumXpmRequired` 0.20.8):
  - project-level `properties` build every command from Liquid pieces:
    `buildFolderRelativePath` (`build/<config>`), `commandCMakeReconfigure`
    (`cmake -S . -B … -G Ninja -D CMAKE_BUILD_TYPE -D PLATFORM_NAME`),
    `commandCMakePrepareWithToolchain` (adds
    `-D CMAKE_TOOLCHAIN_FILE=xpacks/@micro-os-plus/build-helper/cmake/toolchains/<file>`),
    `commandCMakeBuild`, `commandCMakePerformTests` (`cd <build> && ctest -V`);
  - project-level `devDependencies`: `cmake` 3.26.5, `ninja-build` 1.11.1,
    `build-helper` ^2.17.0, `arm-cmsis-rtos-validator`, `chan-fatfs` (the
    last three `"local": "link"`);
  - **hidden blocks** that real configurations inherit: `cmake-actions`
    (prepare / build / test / clean), `native-actions` and `cortexm-actions`
    (`install` = `xpm install --config`, `link-deps` = `xpm link <port> --config`),
    `native-dependencies`, `cortexm-dependencies`, `arm-cmsis-dependencies`,
    one block per tool version (`gcc11`–`gcc14`, `clang13`–`clang19`,
    `arm-none-eabi-gcc` 14.2.1, `qemu-arm` 8.2.6, `openocd` 0.12.0),
    `short-win-paths-properties` (short build folder names on Windows);
  - **a real configuration** only inherits blocks and sets `buildType`,
    `platformName`, `toolchainFileName`, `shortConfigurationName`, e.g.
    `qemu-cortex-m7f-cmake-gcc-debug`; the `-release` one inherits the debug
    one and changes `buildType` (`MinSizeRel` on Cortex-M);
  - **actions**: `test-<platform>-cmake` = prepare, build, test for debug
    and release; aggregates `test-native-cmake`, `test-cortex-cmake`,
    `test-all`; `install-ci` / `test-ci` choose by host with
    `{% if os.platform == … %}`; `deep-clean` uses `del-cli` (portable).
- **CMake chain**: `tests/CMakeLists.txt` (C11, C++20, `enable_testing`,
  no in-source builds, loads `build-helper`) → `cmake/global-definitions.cmake`
  (`ENABLE_RTOS_APIS_TEST`, `ENABLE_MUTEX_STRESS_TEST`,
  `ENABLE_CMSIS_OS_VALIDATOR_TEST`) → `cmake/tests-main.cmake`:
  1. `common-options.cmake`, then the platform's `definitions.cmake` and
     `dependencies-folders.cmake`;
  2. **`xpack_add_dependencies_subdirectories(…)` first**: the suites
     (`sources/<suite>`), the port and chip packages from
     `build/<config>/xpacks/…`, the device folder, and the project-level
     `xpacks/…` (validator, FatFs);
  3. **then** `add_subdirectory(".." "top-bin")`, the kernel;
  4. then `add_subdirectory("platforms/${PLATFORM_NAME}" "platform-bin")`.
  All targets are INTERFACE libraries, so the port can be added before the
  kernel: links by name are resolved when CMake generates.
- **A platform**: `definitions.cmake` (platform and device compile
  definitions, linker script name); `dependencies-folders.cmake`;
  `platform-library.cmake` (`micro-os-plus::platform`: `-mcpu`, float ABI,
  `-Werror`, links `micro-os-plus::iii-<port>` + `micro-os-plus::device`);
  `CMakeLists.txt` (adds `device/` if the platform has one, then one
  `add_test_executable` + `add_test` per enabled suite, the `add_test`
  calling `qemu-system-arm …`, `openocd … program … reset`, or the
  program).
- **Device**: `micro-os-plus::device` comes from `tests/device-qemu-cortexm/`
  for the QEMU machines, from the platform's own `device/` for NUCLEO and
  Pico, plus chip xPacks (`arm-cmsis`, `raspberrypi-pico-sdk`, SEGGER). The
  NUCLEO `CMakeLists.txt` has *"TODO: update to separate
  device-stm32f4-extras, when ready"*: upstream itself plans separate device
  packages (D3).
- **Ports**: `CMakeLists.txt` is a plain INTERFACE library
  (`micro-os-plus::iii-cortexm`, `::iii-posix-arch`) with `include/` and
  `src/rtos/os-core.cpp`; `package.json` has no dependencies; `test/` holds
  only `.gitkeep`.
- **CI**: three GitHub workflows run `npm --prefix tests ci`, then
  `xpm run install-ci|install-all -C tests` and
  `xpm run test-ci|test-all -C tests`, with xpm 0.20.8, on Ubuntu x64 and
  arm64, macOS Intel and Apple Silicon, and Windows.
- **An upstream inconsistency to fix in step 2**: `native-dependencies`
  declares `@micro-os-plus/posix-arch`, but the package's own name, the
  `native-actions` `link-deps` and the native `dependencies-folders.cmake`
  all use `@micro-os-plus/micro-os-plus-iii-posix-arch`.

## 2. What is the same, and what differs

**The same.** The outer mechanism: xpm configurations in
`tests/package.json` (P4), tools from the shared store (P3), CMake + Ninja
to build, CTest to test, and the same commands
(`xpm run install --config <C>`, `xpm run test-<platform>-cmake`). The
harness folder `micro-os-plus-iii/tests/` has every upstream file, and all
24 platforms have the same five upstream platform files.

**What differs**, in short:

| | Upstream | `smp` |
|---|---|---|
| Finding the ports | xpm dependency + `xpm link` | sibling folder, path computed by CMake |
| Who adds the kernel | the tests (`add_subdirectory("..")`) | the port |
| Chip and driver code | platform `device/` or separate xPacks | inside the ports (`soc/`, `drivers/`); the `devices` repository was dissolved |
| Board facts | the platform folder | the port's `test/boards/<b>/board.cmake` **and** the platform folder; the port library itself reads `board.cmake` |
| Tests | portable suites in the kernel's `tests/` | the same, **plus** board tests in `<port>/test/<board>/<app>/`, registered into the kernel's harness |
| Running a test | CTest runs `qemu` / `openocd` / the program | the same for upstream suites and Cortex-M QEMU; bash scripts for AArch QEMU, native board tests and hardware |
| Verdict | exit code | `RESULT:` line, plus exit code |

### 2.1 Where bash is used

Building uses **no bash**: the build steps run only the compiler,
`objcopy` and `cmake -E`. The one script in a build is upstream's own
`build-helper/dev-scripts/get-libraries-paths.sh` (native platform).

Running uses bash for the test kinds upstream does not have:

| Test kind | Upstream: CTest runs | `smp`: CTest runs |
|---|---|---|
| Upstream suites on QEMU (`qemu-cortex-m*`, `2xcortex-m33`, `pico2-1cpu`) | `qemu-system-arm …` | the same |
| Cortex-M board tests on QEMU | (none) | `qemu-system-arm …` directly |
| Native validator | the program | the same |
| Native board tests | (none) | `bash run-host.sh` |
| AArch32 / AArch64 board tests on QEMU (`raspi3b`) | (none) | `bash run-qemu.sh` |
| Hardware tests | `openocd -c "program …" -c reset` | `bash <board>/hw.sh` → `bash run-hw.sh` |

## 3. Each difference, the reason given at the time, and the verdict

The reasons are the ones written down when the change was made (commit
messages, `xpack-dev-smp.md`, script headers). The verdict compares them
with §1.

### 3.1 Sibling folders instead of `xpm link` — **against P1, P2**

*Reason given:* the kernel and the ports change together, so a released
port never matches the kernel under development; and several copies of
the same repositories existed on the machine, while the `.link` registry
holds one path per package.

*Verdict:* the first reason is exactly the case `xpm link` exists for:
developing several packages at once from writable working copies (P2). The
second is not an xPack scenario. The xpm docs show one development copy
per package and do not discuss several (not covered). Several copies of the
same package is a situation this project created, not a limit of xPack. The
reason does not hold.

### 3.2 The port adds the kernel — **against P5**

*Reason given (commit K44):* "the harness added the kernel one level up and
each port as an xpack dependency, so a port could not bring its own kernel,
devices or board."

*Verdict:* P5 is the documented pattern: the tests add the library, the
other packages are dependencies. A port that brings the kernel turns the
dependency upside down. The ports only add the kernel `if (NOT TARGET …)`
already, so going back is small (§8). Does not hold.

### 3.3 Chip and driver code moved into the ports; `devices` dissolved — **against P9**

*Reason given (`xpack-dev-smp.md` §3.1):* register definitions for one chip
were split between two repositories, and a change needed matching commits
in both.

*Verdict:* P9 asks for separate architecture, device and board layers. The
fix for "one chip split over two repositories" is one package **per chip
family**, not folding all chips into the architecture port. The two-repository
commit is what `xpm link` handles (P2). Does not hold.

### 3.4 Board facts in the port (`board.cmake`) — **against P9**

The port library reads `test/boards/${BOARD}/board.cmake` to know its SoC
and CPU count, and the platform folder reads it again. Example, the Pi 4 B:
SoC, `-mcpu`, CPU count, linker script, LED pin and SD address are in
`micro-os-plus-iii-aarch64/test/boards/rpi4b/board.cmake`; `BOARD=rpi4b`
is in the platform's `definitions.cmake`; `platform-library.cmake` uses the
CPU count and linker script from the port, and sets some defines the port's
test builder sets again. A board is described in three files in two
repositories, and the architecture package knows boards. Does not hold.

### 3.5 Board tests in the ports, registered into the kernel's harness — **against P6, P7**

*Reason given:* SMP bugs depend on the chip (secondary core start-up,
inter-core interrupts, caches with DMA, PSRAM…), and a test belongs next to
the code it tests.

*Verdict:* the **need** is real: SMP must be tested on real chips. The
**place** is wrong. The kernel's `tests/` must run the same sources on all
platforms (P7), and chip-specific programs break that. Under P6, a test of
the architecture port belongs in the port's own `tests/` project, a driver
test in the device package's `tests/`. The kernel's harness should not
register them.

### 3.6 Tests found by folder instead of listed by hand — **compatible**

The xpm guide says of the tests folder: *"The name of the folder and the
hierarchy below it are all at author's discretion."* How CMake registers
tests is a CMake detail. With more than 200 programs, a folder scan is
reasonable. It stays, but inside each package's own `tests/` (§10).

### 3.7 One source built for QEMU, hardware and host — **compatible**

Running the same sources on many platforms is P7 itself.

### 3.8 Bash runner scripts — **against P8**

*Reason given:* QEMU boot shim, SD card image, timeouts; on hardware: halt
all cores, load, start core 0, resume parked cores, separate reset
sessions. `run-hw.sh` replaced 48 nearly identical copies.

*Verdict:* the multi-core steps are needed, but bash scripts do not run on
Windows (P8). xpm allows shell commands in actions if they are portable;
these scripts are not. They can be replaced by CTest settings and OpenOCD
Tcl (§11).

### 3.9 The `RESULT:` line — **compatible**

An extra, human-readable verdict; CTest can read it itself
(`PASS_REGULAR_EXPRESSION`…). The exit code stays. Note: the comment in
`hw_result.hpp` saying QEMU builds have no `SEMIHOST` is out of date; the
AArch builds define it and QEMU runs with semihosting on.

### 3.10 Hardware tests opt-in (`hwd` label) — **compatible**

`xpm run test-all` must work with no board connected.

### 3.11 Upstream platforms kept; more configurations — **compatible**

The upstream platforms still run the upstream suites (single-core
regression), and the new platforms are configurations of the same kind
(P4). There are 90 non-hidden configurations (upstream: 44).

### 3.12 Summary

| Difference | Verdict |
|---|---|
| Sibling folders instead of `xpm link` | against P1, P2 |
| Port adds the kernel | against P5 |
| Chip/driver code in the ports (and FatFs copies instead of the `chan-fatfs` xPack) | against P9, P1 |
| Board facts in the port | against P9 |
| Board tests registered into the kernel harness | against P6, P7 |
| Bash runners | against P8 |
| Folder scan, QEMU/HW/host variants, `RESULT:` line, `hwd` label, more configurations | compatible |

---

# Part B: How to bring it back

Nothing below has been applied. Each step is one or a few commits, and
keeps the tests green.

## 4. The target, in one picture

```
micro-os-plus-iii                 kernel (IIIe: one package)
  tests/                          portable suites only, same sources on all platforms (P6, P7)
    platforms/<P>/                one place per board: flags, linker, CPU count, device link (P9)

micro-os-plus-iii-<arch>          architecture port only (P9): context switch, spinlocks,
  CMakeLists.txt                  IPIs, MMU/cache, secondary-core start-up; a leaf (P5)
  tests/                          its own xpm project: architecture-level SMP tests (P6)

<device package per chip family>  e.g. BCM283x/BCM2711, RP2040/RP2350, RK3506, STM32F4 (P9)
  tests/                          its own xpm project: driver tests (SD, USB, Ethernet…) (P6)

test support package              hw_result.hpp, the folder-scan CMake helper, OpenOCD Tcl
                                  runner; a dependency of every tests/ project
```

All of them are xpm dependencies of each other with released versions;
working copies are plugged in with `xpm link` (P1, P2). Tools come from the
shared store (P3). Everything runs on Windows, macOS and Linux (P8).

## 5. The rules, and where `smp` stands

| Rule | Upstream / xPack | `smp` today | Section |
|---|---|---|---|
| R1 port is a leaf | port defines its target, never adds the kernel | adds the kernel (guarded) | §6 |
| R2 tests add their library | `add_subdirectory(".." "top-bin")` | the port is added, it adds the kernel | §6 |
| R3 dependencies are xpm packages | `specifier` + `"local": "link"` | ports not declared | §7 |
| R4 working copies via `xpm link` | two steps, `--config` per build folder | sibling path | §7 |
| R5 CMake reads dependencies from the build folder | `build/<config>/xpacks/@scope/name` | sibling path | §6, §7 |
| R6 three layers | architecture / device / board | chips in ports, board facts in ports | §8, §9 |
| R7 each package tests itself | own `tests/` project | board tests in ports, registered into the kernel harness | §10 |
| R8 same sources everywhere in the kernel's tests | portable suites | plus chip-specific programs | §10 |
| R9 portable running | CTest runs the tools; portable actions | bash runners | §11 |

## 6. Step 1: the port is a leaf; the tests add the kernel (R1, R2, R5)

`tests/cmake/tests-main.cmake`, exactly in upstream's order (§1.1):
dependencies first, then the kernel, then the platform.

```cmake
include ("cmake/common-options.cmake")
include ("platforms/${PLATFORM_NAME}/cmake/definitions.cmake")
include ("platforms/${PLATFORM_NAME}/cmake/dependencies-folders.cmake")
xpack_add_dependencies_subdirectories (                 # suites, the port, chip
  "${xpack_dependencies_folders}" "xpacks-bin")         # packages: build/<config>/xpacks/
add_subdirectory (".." "top-bin")                       # the kernel
add_subdirectory ("platforms/${PLATFORM_NAME}" "platform-bin")
```

(`xpack_add_dependencies_subdirectories` comes from upstream's
`@micro-os-plus/build-helper`, not from xpm; xpm itself uses plain
`add_subdirectory`.)

Each port platform's `dependencies-folders.cmake` lists the port, as
upstream does for `micro-os-plus-iii-cortexm`:

```cmake
"${CMAKE_BINARY_DIR}/xpacks/@micro-os-plus/micro-os-plus-iii-aarch64"
```

Each port's `CMakeLists.txt` loses its "find the kernel next to me" block
and becomes a plain INTERFACE library like upstream's `-cortexm`. It must
**not** test whether the kernel target exists (`if (NOT TARGET …)`),
because the port is added before the kernel; it links kernel targets by
name, which CMake resolves when it generates. The aarch ports' current
`if (NOT TARGET micro-os-plus::iii-core)` guards go for the same reason.

*Check:* `prepare` + `build` of all platforms, then `xpm run test-all`.

## 7. Step 2: dependencies as xpm packages, working copies via `xpm link` (R3, R4, R5)

**Versions.** Tag each package with a pre-release version, as
`README-MAINTAINER.md` describes for the kernel (`7.1.0-pre.1` style). For
example `micro-os-plus-iii-cortexm` `v2.0.0-pre.1` (a major bump: the
CMake shape and the kernel it needs changed), `-aarch32` / `-aarch64`
`v0.1.0-pre.1`.

**Declaration**, in `tests/package.json`, the same shape as upstream's
`cortexm-dependencies`:

```json
"aarch64-dependencies": {
  "hidden": true,
  "devDependencies": {
    "@micro-os-plus/micro-os-plus-iii-aarch64": {
      "specifier": "github:dan-maio/micro-os-plus-iii-aarch64#v0.1.0-pre.1",
      "local": "link",
      "platforms": "all"
    }
  }
}
```

The POSIX port uses glibc's ucontext by choice, so upstream's
`@xpack-3rd-party/libucontext` dependency does not come back.

**Working copies, exactly as upstream.** Once per machine, in each working
copy: `xpm link` (step 1). Per configuration, in the tests:
`xpm link @micro-os-plus/micro-os-plus-iii-aarch64 --config <C>` (step 2,
upstream's `link-deps` action). Upstream's layout is one clone per
package, side by side in one folder, each linked once.

**One working copy per package, all in this workspace (D1).** The store
holds one `.link` per package name. Every `.link` of these packages points
into `/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/`, and to no other
folder.

**Hardware scripts**, while they still exist: reached through
`build/<config>/xpacks/…`, `hw.sh` cannot find the kernel from its own
path; CTest passes `UOS_SMP_DIR=${CMAKE_SOURCE_DIR}/..` in the test's
`ENVIRONMENT`. This goes away with §11.

*Check:* the `prepare` log shows the port under
`build/<config>/xpacks/@micro-os-plus/…`; the same tests pass.

## 8. Step 3: device packages (R6)

Move each chip family's code out of the ports into its own xPack,
following P9 and upstream's use of chip xPacks (`arm-cmsis`,
`raspberrypi-pico-sdk-xpack`). The names below are examples:

| Today in | Moves to (example name) |
|---|---|
| `-aarch32/soc/bcm2837`, `-aarch64/soc/bcm2837`, `soc/bcm2711`, their `drivers/` | `micro-os-plus-iii-devices-bcm283x` |
| `-aarch32/soc/rk3506`, Lyra drivers | `micro-os-plus-iii-devices-rk3506` |
| `-cortexm/soc/rp2040`, `soc/rp2350` | `micro-os-plus-iii-devices-rp2xxx` |
| `-cortexm/soc/stm32f4xx` | `micro-os-plus-iii-devices-stm32f4` |
| `-posix-arch/soc/native`, host file block device | stays in `-posix-arch` (it is the host "device") or a small host package |
| `drivers/fatfs/` in `-aarch32`, `-aarch64`, `-posix-arch` (a copy of ChaN FatFs: `ff.c`, `ff.h`, `ffconf.h`, plus a `diskio` glue) | the FatFs sources come from upstream's `@xpacks/chan-fatfs` dependency (P1); only the `diskio` glue stays with the device package |

Each one exports its `micro-os-plus::soc-<chip>` targets, unchanged, so
consumers do not change. Use `git subtree split` / `filter-repo` to keep the
history, as the dissolution did in the other direction. The ports keep the
architecture only.

The duplicate BCM2837 code in `-aarch32` and `-aarch64` (32- and 64-bit
builds of the same chip) is a point to settle when moving: one package
with both, or the shared parts only.

*Check:* the compile and link commands (`compile_commands.json`) of every
configuration are identical before and after; the same tests pass.

## 9. Step 4: one place per board (R6)

Board facts move from `<port>/test/boards/<b>/board.cmake` into the platform
folder, where upstream keeps them:

| Board fact | Goes to |
|---|---|
| CPU count, `-mcpu`, defines, LED pin, addresses | `tests/platforms/<P>/cmake/platform-library.cmake` |
| Linker scripts | `tests/platforms/<P>/device/linker-scripts/` (as upstream NUCLEO and Pico) |
| Which chip | `target_link_libraries(… micro-os-plus::soc-<chip>)` in `platform-library.cmake` |
| OpenOCD configs, reset and run Tcl | `tests/platforms/<P>/openocd/` |

The port library stops reading `board.cmake`, and `BOARD` disappears from
the port. Each package's `tests/` (§10) has its own `platforms/<P>/` for
the boards it runs on, in the same shape.

*Check, per board:* identical `compile_commands.json` and `ctest -N` list
before and after.

## 10. Step 5: each package tests itself (R7, R8)

| Tests | Package | Runs on |
|---|---|---|
| `rtos-apis`, `mutex-stress`, `cmsis-os-validator`, `fp-switch` (portable, same sources) | kernel `tests/` | all platforms: native, QEMU Cortex-M, `2xcortex-m33`, `raspi3b` AArch32/64, the boards |
| Architecture SMP tests: secondary start-up, spinlocks, inter-core interrupts, per-core ticks, caches (`smp_test0`–`smp_test4`, `smp-mat-test`, `smp-num-test`, `smp-pipeline-test`, `smp-pro-cons-test`) | port `tests/` (new xpm project) | QEMU machines and boards of that architecture |
| Driver tests: SD, USB, Ethernet, Wi-Fi, PSRAM, LED/GPIO | device package `tests/` (new xpm project) | boards with that chip |

Each new `tests/` project has the upstream shape (`package.json` with its
configurations, `CMakeLists.txt`, `cmake/tests-main.cmake`, `platforms/<P>/`),
adds its own package with `add_subdirectory(".." "top-bin")`, and gets the
kernel and the other packages as xpm dependencies, linked with `xpm link`
during development.

The shared pieces (`hw_result.hpp`, the folder-scan CMake helper, the
OpenOCD Tcl runner) become one small **test support xPack**,
`micro-os-plus-iii-test-support` (D4), a dependency of
every `tests/` project, instead of `test_smpl/` inside the kernel.

The kernel's `tests/package.json` keeps only configurations for the
portable suites; the board-test configurations move with the tests.

## 11. Step 6: portable running (R9)

**QEMU and host tests: CTest runs the tool; `run-qemu.sh` and `run-host.sh`
go.**

| What the script does | Portable replacement |
|---|---|
| Pick one test | one `add_test` per program |
| Timeout per test | `set_tests_properties(… TIMEOUT <s>)` |
| Verdict from `RESULT:` | `PASS_REGULAR_EXPRESSION`, `FAIL_REGULAR_EXPRESSION`, `SKIP_REGULAR_EXPRESSION` (CMake ≥ 3.16; with a pass regex set, CTest ignores the exit code) |
| AArch32 shim | already plain QEMU arguments: `-kernel shim8.img -device loader,file=<image>,addr=<address>` |
| Exit code | already there: `SEMIHOST` builds + `-semihosting-config enable=on,target=native` |
| SD card image | a CTest fixture (`FIXTURES_SETUP`) running `cmake -E` and the existing `flatfs_tool.py` |
| A log per test | `ctest --output-log`, `--output-junit` |

```cmake
add_test (NAME "${PLATFORM_NAME}-${_app}-qemu"
  COMMAND "${_qemu}" -M raspi3b -smp 4 -nographic -serial none
          -semihosting-config enable=on,target=native
          -kernel "${_bin}/${_app}-qemu.bin")
set_tests_properties ("${PLATFORM_NAME}-${_app}-qemu" PROPERTIES
  LABELS "qemu" TIMEOUT 300
  PASS_REGULAR_EXPRESSION "RESULT: PASS"
  FAIL_REGULAR_EXPRESSION "RESULT: FAIL"
  SKIP_REGULAR_EXPRESSION "RESULT: SKIP")
```

**Hardware tests: CTest runs `openocd` with a Tcl script; `hw.sh` and
`run-hw.sh` go.** OpenOCD contains a Tcl interpreter and runs on all three
systems:

```cmake
add_test (NAME "${PLATFORM_NAME}-${_app}-hwd"
  COMMAND openocd -f "${_platform_dir}/openocd/board.cfg"
                  -f "${_support_dir}/run-test.tcl"
                  -c "uos_run_test ${_bin}/${_app}-hwd.elf")
set_tests_properties ("${PLATFORM_NAME}-${_app}-hwd" PROPERTIES
  LABELS "hwd" TIMEOUT 600 RUN_SERIAL TRUE
  PASS_REGULAR_EXPRESSION "RESULT: PASS" FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
```

| `run-hw.sh` job | OpenOCD Tcl |
|---|---|
| halt all cores | `foreach t [target names] { $t arp_halt }` |
| load | `load_image <elf>` |
| clear the secondary start slots | `mww` at the `__smp_spin` address, passed from CMake |
| start core 0, resume parked cores | `resume <entry>` per target |
| semihosting on some cores only | `arm semihosting enable` per target |
| Pi 4 B watchdog / Lyra CRU reset in a separate session | `<board>-reset.tcl` run by a CTest fixture |
| verdict | semihosting `SYS_EXIT` ends OpenOCD; CTest reads `RESULT:` |

The UART is never opened, as today. Boards with a reset line (NUCLEO,
WeAct) can use upstream's plain `openocd … program … reset`.

**Actions** in every `package.json` stay shell commands, as xpm intends,
but portable ones: `cmake -E`, `del-cli`, Liquid `{{ path.sep }}`, no `rm`,
`ln`, `bash`.

*Confidence:* QEMU part high (every piece already exists). Hardware part
medium: each board's sequence, especially resets that drop the debug link,
must be checked on the board, one test at a time; each board keeps its
`hw.sh` until its Tcl version passes.

## 12. Order, effort, risk

| Step | What | Effort | Risk |
|---|---|---|---|
| 1 (§6) | leaf ports, tests add the kernel | small | low; `prepare` shows problems at once |
| 2 (§7) | tags, xpm dependencies, `xpm link` | medium | medium: decide which tree is the development copy of each package |
| 3 (§11, QEMU/host) | CTest runs QEMU and host tests; delete two scripts | small–medium | low: compare pass lists |
| 4 (§9) | one place per board | 14 boards, small each | low: identical `compile_commands.json` and `ctest -N` |
| 5 (§8) | device packages out of the ports | medium–large | medium: history-preserving split; identical compile commands |
| 6 (§10) | each package tests itself; test support xPack | large | medium: many configurations move |
| 7 (§11, hardware) | OpenOCD Tcl, board by board | medium | medium: hardware, one test at a time |

Steps 1–4 are mechanical and can be checked exactly. Steps 5 and 6 change
the package structure; the decisions they need are in §14, and every step
has its file list and check in §15. All work is local (D5).

## 13. The end state, file by file

| Upstream | After the plan |
|---|---|
| kernel `tests/package.json`, `CMakeLists.txt`, `cmake/tests-main.cmake` | same files, same role, portable suites only |
| `tests/platforms/<P>/` five files + `device/` | same, holding all board facts |
| port: `CMakeLists.txt` (leaf), `include/`, `src/` | same, architecture only |
| port `test/.gitkeep` | port `tests/`, an xpm project of the upstream shape |
| chip xPacks (`arm-cmsis`, `raspberrypi-pico-sdk-xpack`) | device xPacks per chip family, each with `tests/` |
| `devDependencies` with `"local": "link"`, `xpm link` | same |
| tools in the shared store, `build/<config>/xpacks/.bin` | same |
| CTest runs `qemu` / `openocd` / the program | same; OpenOCD with a Tcl script on multi-core boards |
| (none) | test support xPack: `hw_result.hpp`, folder-scan helper, Tcl runner |
| (none) | `tests/platforms/<P>/openocd/` |

## 14. Decisions (agreed 2026-11-16)

| # | Question | Decision |
|---|---|---|
| D1 | Where are the working copies? | **Only in this workspace**, `/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/`. Every `xpm link`, every `.link` and every path in the build points here and to no other folder. New packages (D3, D4) are created here too. |
| D2 | Version tags | Upstream scheme (`README-MAINTAINER.md`), pre-release field `-pre.N`: kernel `v7.2.0-pre.1` (today `7.1.0`, tagged); `-cortexm` `v2.0.0-pre.1` and `-posix-arch` `v2.0.0-pre.1` (major bump: CMake shape and required kernel changed); `-aarch32` and `-aarch64` `v0.1.0-pre.1`. Tags are created **locally** during implementation (D5). |
| D3 | Device packages | **One xPack per chip family**: `micro-os-plus-iii-devices-bcm283x` (BCM2837 and BCM2711, 32- and 64-bit together), `-devices-rk3506`, `-devices-rp2xxx` (RP2040, RP2350), `-devices-stm32f4`. FatFs comes from `@xpacks/chan-fatfs`, as upstream. |
| D5 | Where the work happens | **Everything local**: local branches, commits and tags in this workspace; new packages are new local repositories here. No push, no new GitHub repositories, no tag on any remote, until the user asks. |
| D4 | Shared test code (`hw_result.hpp`, folder-scan helper, OpenOCD Tcl runner) | **Its own xPack, `micro-os-plus-iii-test-support`.** Reason, from upstream: shared CMake helpers are their own xPack (`@micro-os-plus/build-helper`), and the kernel's `.npmignore` excludes `test*/`, so `tests/` and `test_smpl/` are never part of a released kernel; other packages' tests could only reach them through a linked working copy. |

**State of the xpm register:** on 2026-11-16 none of the five `~/.local/xPacks/@micro-os-plus/<name>/.link` entries pointed to this workspace; since step 2 (2026-11-17) all five do (D1).

**Local backup (2026-11-17, asked by the user):** every repository here, and the workspace itself (this document, the baseline, the step checks), is pushed to a local bare repository under `~/Downloads/GIT/micro-os-plu-iii-smp-pr-local/`, branch `align/smp-upstream`, remote name `local`. It is a local folder, not a remote service, so D5 still holds.

## 15. Implementation checklist

**All work is local (D5).** No `git push`, no remote tags, no GitHub
repositories, until the user asks.

Each step: one local branch from `smp`, one or a few commits, each commit with the
problem / change / scope message style already used, then the check. Do not
start the next step until the check passes. Hardware: one test at a time.

### Step 1 — the port is a leaf; the tests add the kernel (§6)

Files:

- `micro-os-plus-iii/tests/cmake/tests-main.cmake`: remove the sibling
  search (`_uos_siblings`, `UOS_*_DIR`, the `if (PLATFORM_NAME MATCHES …)`
  chain); restore upstream's order: `xpack_add_dependencies_subdirectories(...)`
  first, then `add_subdirectory(".." "top-bin")`, then the platform (§1.1).
  During steps 1–2 keep a temporary fallback to the sibling folder when
  `build/<C>/xpacks/@micro-os-plus/<port>` does not exist; remove it at the
  end of step 2.
- Port `CMakeLists.txt`: remove the kernel search, the `add_subdirectory` of the kernel and every `if (NOT TARGET <kernel target>)` guard; no check for the kernel (it is added after the port):
  - `micro-os-plus-iii-aarch32/CMakeLists.txt` (block from line 14)
  - `micro-os-plus-iii-aarch64/CMakeLists.txt` (block from line 14)
  - `micro-os-plus-iii-cortexm/CMakeLists.txt` (block from line 53)
  - `micro-os-plus-iii-posix-arch/CMakeLists.txt` (block from line 35)
- `dependencies-folders.cmake` of the 16 port platforms: add
  `"${CMAKE_BINARY_DIR}/xpacks/@micro-os-plus/micro-os-plus-iii-<port>"`,
  as the upstream platforms `nucleo-f411re`, `nucleo-f767zi`,
  `nucleo-h743zi`, `raspberrypi-pico` already do:
  `aarch32-luckfox-lyra`, `aarch32-rpi3b`, `aarch32-rpi-zero-2w`,
  `aarch64-rpi3b`, `aarch64-rpi4b`, `aarch64-rpi-zero-2w`,
  `cortexm-nucleof411`, `cortexm-pico`, `cortexm-pico2`,
  `cortexm-pico2-pizero`, `cortexm-pico2-rp2350b-psram`,
  `cortexm-weactf411`, `cortexm-weactf412`, `native`, `2xcortex-m33`,
  `pico2-1cpu` (and `qemu-cortex-m0/m3/m4f/m7f`, which use the Cortex-M
  port).

Check: `xpm run prepare` + `build` of every configuration; `xpm run test-all`;
the same pass list as before the step.

**Done 2026-11-17** (branch `align/step1-leaf-ports`; kernel `b8e05d0d`,
ports `5a61279` / `93637dd` / `8eba5a4` / `a86e854`). Check in `step1-check/`:
40/40 configurations built, the same pass list as the baseline
(`baseline-2026-11-17/`) except two existing flaky tests; for the Pi 4 B
release build all 2648 compile commands and the link are identical to `smp`.

### Step 2 — dependencies as xpm packages, `xpm link` (§7)

1. In each of the five repositories here, bump `package.json` `version`
   to the D2 value, commit, tag **locally** (no push, D5).
   The `specifier` names the future GitHub tag
   (`github:dan-maio/<name>#v…`), which does not exist remotely yet. With
   `"local": "link"` and the `.link` in place, the working copy is used
   instead; whether `xpm install` still tries to fetch the specifier is not
   documented (§16), so check it first. If it does, use a local specifier
   for the local phase, e.g. `file:../../micro-os-plus-iii-aarch64`, and
   switch to `github:…` when publishing.
2. `tests/package.json`: add the ports to `aarch32-dependencies`,
   `aarch64-dependencies`, `cortexm-dependencies` (already there for
   `@micro-os-plus/micro-os-plus-iii-cortexm`, update the `specifier`),
   `native-dependencies`, each `"local": "link"`. In `native-dependencies`
   use the package's real name, `@micro-os-plus/micro-os-plus-iii-posix-arch`
   (upstream declares `@micro-os-plus/posix-arch` there but links and reads
   the real name, §1.1). The POSIX port uses glibc ucontext, so
   `@xpack-3rd-party/libucontext` is not added (and not linked or listed in
   the native platform files).
3. `tests/package.json` actions: `link-deps` in upstream's shape (the
   hidden `*-actions` blocks). Upstream's `git-clone-deps`, `git-status-deps`
   and `git-pull-deps` clone into `${HOME}/Work/…`; here they are not used
   (D1, D5: the clones already exist in this workspace, and nothing is
   cloned from a remote). `link-deps` per configuration:
   `xpm link @micro-os-plus/micro-os-plus-iii-<port> --config {{ configuration.name }}`.
   Remove the leftover `xpm link` in the aarch32/aarch64 `install` actions
   (it moves to `link-deps`).
4. Once, by hand, in each of the five repositories here: `xpm link`
   (replaces the `.link` entries listed in §14).
5. Each board's CTest `hwd` case gets `UOS_SMP_DIR=${CMAKE_SOURCE_DIR}/..`
   in `ENVIRONMENT` (until step 7 removes the scripts).
6. Remove the sibling fallback from `tests-main.cmake`.

Check: `rm build/<C>/CMakeCache.txt`; `xpm run install` + `link-deps` +
`prepare`; the log shows the port under `build/<C>/xpacks/@micro-os-plus/`;
same pass list.

**Done 2026-11-17**, with one change agreed during the step:
**"link only, declare at publish".** During the local phase the ports are
*not* declared in `tests/package.json` (item 2 is postponed to publishing):
a declared `specifier` makes `xpm install` fetch a manifest for it, and the
`github:dan-maio/…#v…-pre.1` tags do not exist remotely (D5). The released
upstream declarations (`-cortexm` v1.1.0, `posix-arch` v1.0.1) are removed,
so nothing downloads a released port any more; `link-deps` alone puts the
working copy into `build/<C>/xpacks/@micro-os-plus/`.

- Kernel `84661c9f`: `tests/package.json` (`link-deps` links only the port;
  the aarch `install` is `xpm install` only), `tests-main.cmake` (no
  fallback; it stops with "Run: xpm run link-deps --config <configuration>"
  when the port is not linked, and sets `UOS_<PORT>_DIR` to the real folder
  behind the link until step 4).
- Kernel `d26b3455`: item 5, `UOS_SMP_DIR=${CMAKE_SOURCE_DIR}/..` on the
  hwd cases of the six AArch platforms (the Cortex-M board scripts do not
  look for the kernel).
- Item 1: `package.json` versions and local tags `v7.2.0-pre.1` (kernel),
  `v2.0.0-pre.1` (`-cortexm`, `-posix-arch`), `v0.1.0-pre.1` (`-aarch32`,
  `-aarch64`). Note: in semver `0.1.0-pre.1` sorts *before* `0.1.0`.
- Item 4 by hand: `xpm link` in the five repositories, and
  `xpm run link-deps --config <C>` for all 40 configurations.

Check in `step2-check/`: 40/40 built, 280 passed, 4 failed. Each failure
was measured against `smp` built from git worktrees (`wt-smp/`), 12 runs
each, with the compile commands compared (`step2-check/prove/`): all
identical, and the failures happen on `smp` too: `2xcortex-m33`
mutex-stress (an SMP race in the kernel, 1 in 12 on `smp`) and the
CMSIS-RTOS validator on the AArch boards. Both are listed as flaky in
`baseline-2026-11-17/BASELINE.md`.

### Step 3 — QEMU and host tests run by CTest (§11, first half)

- Platforms whose `add_test` calls a script today:
  `aarch32-rpi3b`, `aarch32-rpi-zero-2w`, `aarch32-luckfox-lyra`,
  `aarch64-rpi3b`, `aarch64-rpi4b`, `aarch64-rpi-zero-2w` (`run-qemu.sh`),
  `native` (`run-host.sh`). Replace with direct `add_test` + the CTest
  properties of §11; per-test timeouts from `run-qemu.sh`'s
  `timeout_for` table move into each board's test list.
- SD tests: a CTest fixture replaces the SD image code in `run-qemu.sh`
  and the three `sd_test/verify.sh` (aarch32: `luckfox-lyra`, `rpi3b`,
  `rpi-zero-2w`; aarch64: `rpi3b`, `rpi4b`, `rpi-zero-2w`).
- Delete: `micro-os-plus-iii/test_smpl/run-qemu.sh`, `run-host.sh`;
  `-aarch32/test/qemu.sh`, `test/boards/*/qemu.sh`;
  `-aarch64/test/qemu.sh`, `test/boards/*/qemu.sh`; `-cortexm/test/qemu.sh`;
  `-posix-arch/test/run.sh`, `test/boards/native/run.sh`.

Check: `ctest -N` lists the same tests; the same pass list as with the
scripts.

**Done 2026-11-17** (kernel `3e7903ac`; aarch32 `5b184d1`, aarch64
`4596049`, cortexm `2201ccb`, posix-arch `768e5ee`). The CTest properties
live in one helper, `tests/cmake/uos-tests-run.cmake`
(`uos_tests_add_qemu()`, `uos_tests_add_host()`, the timeout table), used
by the six AArch platforms and `native`; the timeout table stays in that
one file rather than in each board's list, since it is the same table for
every board. QEMU runs with `-monitor none`. The SD image is a fixture:
`<test>-sd-make` (Python 3: `flatfs_tool.py make-seed` for `sd_test`, a
sparse 4 GiB file for the others) and `<test>-sd-remove`
(`cmake -E rm`).

Check in `step3-check/`: `ctest -N` lists the same tests, plus the 8
fixture tests per AArch configuration (`step3-check/ctest-N/`); 40/40
built; the same 284 tests: 262 passed, **22 skipped**, 0 failed. The 22
are the tests that print `RESULT: SKIP` under QEMU (`usb_test`,
`usb_big_test`, the Wi-Fi and Ethernet tests): the scripts counted them
as passed, CTest reports them as skipped. The 4 step 2 failures (flaky)
passed this time.

### Step 4 — one place per board (§9)

The 14 `board.cmake` files and where each one goes:

| `board.cmake` | Platform folder(s) |
|---|---|
| `-aarch32/test/boards/luckfox-lyra/` | `aarch32-luckfox-lyra` |
| `-aarch32/test/boards/rpi3b/` | `aarch32-rpi3b` |
| `-aarch32/test/boards/rpi-zero-2w/` | `aarch32-rpi-zero-2w` |
| `-aarch64/test/boards/rpi3b/` | `aarch64-rpi3b` |
| `-aarch64/test/boards/rpi4b/` | `aarch64-rpi4b` |
| `-aarch64/test/boards/rpi-zero-2w/` | `aarch64-rpi-zero-2w` |
| `-cortexm/test/boards/nucleof411/` | `cortexm-nucleof411`; also read today by `qemu-cortex-m0/m3/m4f/m7f` (they set `BOARD=nucleof411` only because the port reads `board.cmake`) |
| `-cortexm/test/boards/pico2/` | `cortexm-pico2`; also read today by `2xcortex-m33`, `pico2-1cpu` (same reason) |
| `-cortexm/test/boards/pico2-pizero/` | `cortexm-pico2-pizero` |
| `-cortexm/test/boards/pico2-rp2350b-psram/` | `cortexm-pico2-rp2350b-psram` |
| `-cortexm/test/boards/pico/` | `cortexm-pico` |
| `-cortexm/test/boards/weactf411/` | `cortexm-weactf411` |
| `-cortexm/test/boards/weactf412/` | `cortexm-weactf412` |
| `-posix-arch/test/boards/native/` | `native` |

For each: its values into `tests/platforms/<P>/cmake/platform-library.cmake`,
its linker scripts into `tests/platforms/<P>/device/linker-scripts/`, its
OpenOCD files into `tests/platforms/<P>/openocd/`. Remove `BOARD` from the
`definitions.cmake` of the six upstream-style platforms above. Remove the
`board.cmake` reading from the four port `CMakeLists.txt` (aarch32 and
aarch64 lines 27–40, cortexm lines 167–186, posix-arch lines 135–154); the
port defines all its SoC targets instead (until step 5 moves them out).

Check, per board: `compile_commands.json` identical before and after;
`ctest -N` identical.

### Step 5 — device xPacks (§8, D3)

New **local** repositories in this workspace (D5), each an xPack (`package.json` with
`xpack`, `CMakeLists.txt` with INTERFACE targets, `include/`, `src/`,
`tests/`), history kept with `git subtree split` / `git filter-repo`:

| New package | From |
|---|---|
| `micro-os-plus-iii-devices-bcm283x` | `-aarch32/soc/bcm2837`, `-aarch64/soc/bcm2837`, `-aarch64/soc/bcm2711`, their parts of `drivers/` |
| `micro-os-plus-iii-devices-rk3506` | `-aarch32/soc/rk3506`, Lyra parts of `drivers/` |
| `micro-os-plus-iii-devices-rp2xxx` | `-cortexm/soc/rp2040`, `soc/rp2350` |
| `micro-os-plus-iii-devices-stm32f4` | `-cortexm/soc/stm32f4xx` |

- `drivers/fatfs/ff.c`, `ff.h`, `ffconf.h` in `-aarch32`, `-aarch64`,
  `-posix-arch`: deleted; `@xpacks/chan-fatfs` as a dependency; the
  `diskio` glue stays with the device package (or `-posix-arch` for the
  host).
- `-posix-arch/soc/native` stays in `-posix-arch`.
- The targets keep their names (`micro-os-plus::soc-bcm2711`, …), so the
  platforms only gain a dependency in `dependencies-folders.cmake` and
  `tests/package.json`.

Check: `compile_commands.json` of every configuration identical except
paths; the same pass list.

### Step 6 — each package tests itself (§10, D4)

1. New local repository `micro-os-plus-iii-test-support` (D5): `hw_result.hpp`,
   `board-contract.cpp`, the folder-scan CMake helper (today the core of
   each port's `test/CMakeLists.txt`), the OpenOCD Tcl runner (step 7).
   `micro-os-plus-iii/test_smpl/` is deleted.
2. Each port gets `tests/` in the kernel's `tests/` shape
   (`package.json`, `CMakeLists.txt`, `cmake/tests-main.cmake`,
   `platforms/<P>/`): the architecture tests move there from
   `test/<board>/` (`smp_test0`–`smp_test4`, `smp-mat-test`,
   `smp-num-test`, `smp-pipeline-test`, `smp-pro-cons-test`, `clock-test`,
   …). `test/` (singular) is removed.
3. Each device package gets `tests/` the same way: `sd_test`, `usb_test`,
   Ethernet, Wi-Fi, PSRAM, LED/GPIO tests.
4. The board-specific copies of the portable suites (`rtos-apis`,
   `mutex-stress`, `cmsis-os-validator` folders under `test/<board>/`, and
   the `-ram` variants on pico/pico2 with their `hw.sh` + `load-ram.cfg`)
   are removed: the kernel's `tests/` runs the portable suites on those
   platforms directly.
5. The kernel's `tests/package.json` keeps the configurations for the
   portable suites; board-test configurations move with the tests.

Check: every test that ran before runs in exactly one `tests/` project;
the union of the pass lists equals the old pass list.

### Step 7 — hardware through OpenOCD Tcl (§11, second half)

Per board, in this order, one test at a time, waiting for the user's
acknowledgement between tests:

1. write `tests/platforms/<P>/openocd/` (`board.cfg`, `reset.tcl` where
   needed) using `run-test.tcl` from `micro-os-plus-iii-test-support`;
2. change the board's `hwd` `add_test` to `openocd -f … -f run-test.tcl`;
3. run one test; compare with the same test through the old `hw.sh`;
4. when all the board's tests pass, delete that board's `hw.sh`.

At the end delete: `test_smpl/run-hw.sh`, `run-hw.sh.bak`,
`no-power-cycle/run-hw-simple.sh` (already gone with step 6),
`-aarch32/test/hw.sh`, `-aarch64/test/hw.sh`, `-cortexm/test/hw.sh`,
`-cortexm/test/weactf411/spi-pipeline/check.sh` (its check becomes a CTest
regex or a fixture).

Check: no `.sh` file is called by any `add_test` or `package.json` action
(`grep`), and each board's pass list equals the old one.

### Housekeeping

- `micro-os-plus-iii-cortexm/test/build-pico2/`: a stray build folder, not
  tracked by git; delete.
- `hw_result.hpp` comment about QEMU builds without `SEMIHOST`: correct
  it when the file moves (step 6).
- The usage guides that still name the removed scripts (`run-qemu.sh`,
  `run-host.sh`, the ports' `qemu.sh` / `run.sh`, `sd_test/verify.sh`):
  rewrite once, after step 7, when the run commands are final. In the
  kernel's `docs/`: `tests/TESTS-DEVELOPER-GUIDE.md`,
  `tests-in-aarch32-aarch64.md`, `posix-arch-port.md`, `test-smpl.md`,
  `tests/AARCH32-RPI-ZERO-2W-TESTS.md`, `STATUS.md`,
  `tests/AARCH32-AARCH64-TEST-COMMANDS.md`, `aarch32-second-board.md`,
  `tests/STEPS.md`, `tests/TESTS-CATALOG.md`, `tests/COMMANDS.md`,
  `building-aarch32-aarch64.md`, `cortexm-port.md`; and
  `micro-os-plus-iii-cortexm/README.md`. The history notes
  (`docs/smp-integration/`, `docs/specs/`) stay as they are. The SD
  read-back check `sd_test/verify.sh` did is
  `python3 sd_test/flatfs_tool.py verify <image>`.
- At publishing: `include/cmsis-plus/os-versions.h` still says 7.0.1
  (upstream left it so at v7.1.0 too); set it with the release version.

## 16. Open questions (not answered by the documentation)

- Whether `xpm install` fetches a `github:` specifier that does not exist
  remotely yet when a `.link` is present (matters for the local phase, D5).
- What `xpm install` does when a `.link` already exists, and whether
  `xpm uninstall` removes a `.link`.
- Several development copies of one package on one machine (§7 gives a
  practical rule, not a documented one).
- Exactly what an IIIe port may contain: the µOS++ site has no page defining
  a port; "architecture only" comes from the upstream port repositories and
  the three-layer page.
- `link-and-hide`: not documented.

## 17. Where to read more

- <https://xpack.github.io/xpm/> — CLI (`link`, `install`, `run`,
  `uninstall`), metadata, CMake integration, folders.
- <https://micro-os-plus.github.io/develop/modular-projects/> — the layered
  board support.
- <https://micro-os-plus.github.io/blog/2023/07/19/micro-os-plus-v7-0-0-released/>
  — IIIe vs IVe.
- upstream `README-DEVELOPER.md`, `README-MAINTAINER.md`, `tests/README.md`
  (branch `xpack-development`).
- `BUILD-TEST-CTEST-SYSTEM.md` (next to this file) — how the current `smp`
  system works, with commands.
- `micro-os-plus-iii/docs/smp-integration/xpack-dev-smp.md` — how `smp` was
  built from `xpack-development`.

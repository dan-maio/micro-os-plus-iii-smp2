# Workspace records

Everything here supports the alignment, verification and documentation of the
µOS++ IIIe SMP workspace. It is **not** part of any code repository and is not
needed to configure, build or run the tests; those live in the six sibling
repositories at the workspace root:

- `micro-os-plus-iii/` — kernel and test harness
- `aarch32/` — AArch32 port and RK3506 devices
- `aarch64/` — AArch64 port
- `cortexm/` — Cortex-M port, RP2xxx and STM32F4 devices
- `posix-arch/` — POSIX host port
- `common/` — BCM283x and storage devices

## Contents

| Entry | What it holds |
|---|---|
| `baseline-2026-11-17/` | the pre-alignment baseline logs and `BASELINE.md` |
| `step1-check/` … `step7-check/` | the check notes and before/after snapshots of the seven alignment steps |
| `hw-check/` | hardware runs and results, by date |
| `verify-check/` | the 2026-11-20 xpm/CMake/CTest conformance audit and QEMU matrix |
| `docs-check/` | `md2pdf.sh`, the Markdown → PDF helper used for the guides |
| `wt-smp/` | the reference SMP worktree used during the alignment |
| `claude-1000/`, `.qoder/` | local tool/agent caches |
| `BUILD-TEST-CTEST-SYSTEM.*`, `WHY-SMP-BUILD-TEST-DIFFERS.*` | the build-system analysis documents |
| `new.md`, `pico2-arm-risc.*`, `hw-live.log` | working notes and the long hardware transcript |

Large logs and snapshots are local-only; the root repository tracks the
small documents and check notes.

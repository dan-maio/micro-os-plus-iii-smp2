# µOS++ III SMP, all repositories in one (`micro-os-plus-iii-smp2`)

The µOS++ III SMP work in one repository: the portable kernel with its whole
test harness, the four architecture ports, the five device packages, and the
records of the alignment with upstream. Each part was a separate git
repository. Each is here in the folder it had in the working tree: the kernel
and the device packages merged with their full history, the four ports as git
submodules of their own GitHub repositories:

```text
micro-os-plus-iii/                          the kernel; tests/ is the test harness
aarch32/micro-os-plus-iii-aarch32/          the AArch32 port (submodule)
aarch32/micro-os-plus-iii-devices-rk3506/   Rockchip RK3506 (Luckfox Lyra) devices
aarch64/micro-os-plus-iii-aarch64/          the AArch64 port (submodule)
common/micro-os-plus-iii-devices-bcm283x/   Broadcom BCM283x (Raspberry Pi) devices
common/micro-os-plus-iii-devices-storage/   SD card / storage devices
cortexm/micro-os-plus-iii-cortexm/          the Cortex-M port (submodule)
cortexm/micro-os-plus-iii-devices-rp2xxx/   Raspberry Pi RP2040/RP2350 devices
cortexm/micro-os-plus-iii-devices-stm32f4/  STM32F4 devices
posix-arch/micro-os-plus-iii-posix-arch/    the POSIX (host) port (submodule)
workspace-records/                          the alignment records: baseline and
                                            step*-check/, hw-check/, docs-check/
WORKING-WITH-LOCAL-BARE.md, .pdf            how the parts are kept in local bare repositories
WORKING-WITH-REMOTE.md, .pdf                GitHub: signing in, cloning, pulling, pushing, and
                                            keeping this repository and the parts in step
```

Clone with the submodules, or the four port folders stay empty:

```bash
git clone --recurse-submodules https://github.com/dan-maio/micro-os-plus-iii-smp2.git
# in a clone made without it:
git submodule update --init
```

`git log -- <folder>` shows the history of the kernel or a device package;
`git -C <port folder> log` shows the history of a port.

## Where each part came from

The kernel and the device packages are the branch `align/smp-upstream` of
their repository, imported with `git subtree add` (history kept, commits not
rewritten):

| Folder | Commit |
| --- | --- |
| `micro-os-plus-iii` | `06d365fe` |
| `aarch32/micro-os-plus-iii-devices-rk3506` | `d84cd46` |
| `common/micro-os-plus-iii-devices-bcm283x` | `027acb3` |
| `common/micro-os-plus-iii-devices-storage` | `7c6cd98` |
| `cortexm/micro-os-plus-iii-devices-rp2xxx` | `9e51139` |
| `cortexm/micro-os-plus-iii-devices-stm32f4` | `ea867df` |

The four ports are submodules that follow the branch `smp` of their GitHub
repository (`.gitmodules`). They were imported as subtrees too; on
2026-11-25 each port's `smp` was fast-forwarded to its `align/smp-upstream`
(same commits, same hashes) and the subtree folder was replaced by the
submodule:

| Folder | Repository (`dan-maio/…`, branch `smp`) | Commit |
| --- | --- | --- |
| `aarch32/micro-os-plus-iii-aarch32` | `micro-os-plus-iii-aarch32` | `2eb3719` |
| `aarch64/micro-os-plus-iii-aarch64` | `micro-os-plus-iii-aarch64` | `7141456` |
| `cortexm/micro-os-plus-iii-cortexm` | `micro-os-plus-iii-cortexm` | `a59271c` |
| `posix-arch/micro-os-plus-iii-posix-arch` | `micro-os-plus-iii-posix-arch` | `673a30e` |

The earlier version of the kernel is in `dan-maio/micro-os-plus-iii`, branch
`smp`; this repository does not change it. The earlier version of each port
is now an ancestor on its `smp`: `b79d35d`, `cb16868`, `e0a7ceb` and
`dcd8099`, in the order of the table.

## Using it

The tests are driven from `micro-os-plus-iii/tests/` (xpm configurations in
its `package.json`; see `micro-os-plus-iii/README.md` and
`micro-os-plus-iii/docs/tests/`). Each platform's
`cmake/dependencies-folders.cmake` takes the ports and device packages from
`build/<configuration>/xpacks/@micro-os-plus/<name>`, which
`xpm run link-deps --config <configuration>` connects to the folders above
(`scripts/link-local.sh`, relative links into this repository; not
`xpm link`, which follows the machine's `~/.local/xPacks` registrations).
`WORKING-WITH-REMOTE.md` has the full sequence.

`micro-os-scaffold` (a separate tool) makes a self-contained test workspace
from a clone of this repository, one platform or one test at a time:

```bash
node bin/scaffold.js discover --source /path/to/micro-os-plus-iii-smp2
node bin/scaffold.js /path/to/workspace \
  --source /path/to/micro-os-plus-iii-smp2 \
  --select aarch64-rpi4b.smp_test0 --yes
cd /path/to/workspace && ./run.sh
```

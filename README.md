# µOS++ III SMP, all repositories in one (`micro-os-plus-iii-smp2`)

The µOS++ III SMP work in one repository: the portable kernel with its whole
test harness, the four architecture ports, the five device packages, and the
records of the alignment with upstream. Each part was a separate git
repository. They are merged here, each with its full history, in the folder it
had in the working tree:

```text
micro-os-plus-iii/                          the kernel; tests/ is the test harness
aarch32/micro-os-plus-iii-aarch32/          the AArch32 port
aarch32/micro-os-plus-iii-devices-rk3506/   Rockchip RK3506 (Luckfox Lyra) devices
aarch64/micro-os-plus-iii-aarch64/          the AArch64 port
common/micro-os-plus-iii-devices-bcm283x/   Broadcom BCM283x (Raspberry Pi) devices
common/micro-os-plus-iii-devices-storage/   SD card / storage devices
cortexm/micro-os-plus-iii-cortexm/          the Cortex-M port
cortexm/micro-os-plus-iii-devices-rp2xxx/   Raspberry Pi RP2040/RP2350 devices
cortexm/micro-os-plus-iii-devices-stm32f4/  STM32F4 devices
posix-arch/micro-os-plus-iii-posix-arch/    the POSIX (host) port
workspace-records/                          the alignment records: baseline and
                                            step*-check/, hw-check/, docs-check/
WORKING-WITH-LOCAL-BARE.md, .pdf            how the parts were kept in local bare repositories
```

One `git clone` gets everything. `git log -- <folder>` shows the history of one
part.

## Where each part came from

Every folder is the branch `align/smp-upstream` of its repository, imported
with `git subtree add` (history kept, commits not rewritten):

| Folder | Commit |
| --- | --- |
| `micro-os-plus-iii` | `06d365fe` |
| `aarch32/micro-os-plus-iii-aarch32` | `2eb3719` |
| `aarch32/micro-os-plus-iii-devices-rk3506` | `d84cd46` |
| `aarch64/micro-os-plus-iii-aarch64` | `7141456` |
| `common/micro-os-plus-iii-devices-bcm283x` | `027acb3` |
| `common/micro-os-plus-iii-devices-storage` | `7c6cd98` |
| `cortexm/micro-os-plus-iii-cortexm` | `a59271c` |
| `cortexm/micro-os-plus-iii-devices-rp2xxx` | `9e51139` |
| `cortexm/micro-os-plus-iii-devices-stm32f4` | `ea867df` |
| `posix-arch/micro-os-plus-iii-posix-arch` | `673a30e` |

The earlier version of the kernel and the ports is in the separate repositories
`dan-maio/micro-os-plus-iii`, `micro-os-plus-iii-aarch32`,
`micro-os-plus-iii-aarch64`, `micro-os-plus-iii-cortexm` and
`micro-os-plus-iii-posix-arch`, branch `smp`. This repository does not change
them.

## Using it

The tests are driven from `micro-os-plus-iii/tests/` (xpm configurations in
its `package.json`; see `micro-os-plus-iii/README.md` and
`micro-os-plus-iii/docs/tests/`). Each platform's
`cmake/dependencies-folders.cmake` takes the ports and device packages from
`build/<configuration>/xpacks/@micro-os-plus/<name>`, which `xpm link`
connects to the folders above.

`micro-os-scaffold` (a separate tool) makes a self-contained test workspace
from a clone of this repository, one platform or one test at a time:

```bash
node bin/scaffold.js discover --source /path/to/micro-os-plus-iii-smp2
node bin/scaffold.js /path/to/workspace \
  --source /path/to/micro-os-plus-iii-smp2 \
  --select aarch64-rpi4b.smp_test0 --yes
cd /path/to/workspace && ./run.sh
```

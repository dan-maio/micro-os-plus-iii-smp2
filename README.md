# µOS++ III — Storage drivers

The chip-independent storage drivers of µOS++ III: the **flatfs** file system, the **FatFs** disk glue (`diskio_sd.cpp`, `fatfs_hw.cpp`) with the FatFs sources, and the SD card interface `sd.hpp`, which selects the controller back-end a device package provides (`SD_BACKEND_*`).

It is an xPack: a CMake project of INTERFACE targets, consumed with xpm.
It reads no board; a board (in the consumer's test platforms) names the
targets it needs.

## Targets

- `micro-os-plus::devices-storage`

## Origin

Moved, with its git history, from `drivers/` of micro-os-plus-iii-aarch32, -aarch64 and -posix-arch (identical in the three); history from -aarch64.

## Status

Local only: a pre-release (`0.1.0-pre.1`) used through `xpm link` by the
tests of micro-os-plus-iii. Not published.

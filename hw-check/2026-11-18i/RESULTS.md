# Hardware runs 2026-11-18 (i): WeAct F412RE

cortexm-weactf412-cmake-gcc-debug, rebuilt from the current tree; ST-LINK/V2.1
(0483:374b) wired to the F412 (DBGMCU IDCODE 0x30006441, DEV_ID 0x441 =
STM32F412, checked before the run).

## 5/5 pass

cmsis-os-validator (31 s), mos-test1 (11 s), mutex-stress (44 s),
rtos-apis (42 s), uart-test1 (11 s; self-checking, heartbeats over a fixed
time). weactf412-batch.txt.

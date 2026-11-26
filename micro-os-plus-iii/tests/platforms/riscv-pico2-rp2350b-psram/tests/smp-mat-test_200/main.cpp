/*
 * smp-mat-test_200 -- smp-mat-test (N = 300, B = 50, matrices in the PSRAM)
 * with the system clock at 200 MHz, 1.20 V
 * (PICO2_SYS_CLK_KHZ=200000; flash SCK 50 MHz, PSRAM SCK 100 MHz). Not a
 * Cortex-M test: the 200 MHz clock measured on its own, with no potentiometer and
 * no reboot, against smp-mat-test at 150 MHz.
 */
#include "../smp-mat-test/main.cpp"

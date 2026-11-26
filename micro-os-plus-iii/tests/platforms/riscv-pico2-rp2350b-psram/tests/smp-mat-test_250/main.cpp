/*
 * smp-mat-test_250 -- smp-mat-test (N = 300, B = 50, matrices in the PSRAM)
 * with the system clock overclocked to 250 MHz at 1.25 V
 * (PICO2_SYS_CLK_KHZ=250000; flash SCK 50 MHz, PSRAM SCK 83 MHz). Not a
 * Cortex-M test: the overclock measured on its own, with no potentiometer and
 * no reboot, against smp-mat-test at 150 MHz.
 */
#include "../smp-mat-test/main.cpp"

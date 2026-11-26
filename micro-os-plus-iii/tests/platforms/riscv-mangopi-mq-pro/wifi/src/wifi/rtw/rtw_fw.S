/* rtw_fw.S - RTL8723D firmware image, linked into .rodata.
 * The blob is Realtek's redistributable rtw88/rtw8723d_fw.bin from
 * linux-firmware (see third_party/rtw88/README.md for its licence). */
    .section .rodata.rtw8723d_fw, "a", @progbits
    .balign 8
    .globl rtw8723d_fw_start
    .globl rtw8723d_fw_end
rtw8723d_fw_start:
    .incbin "third_party/rtw88/rtw8723d_fw.bin"
rtw8723d_fw_end:
    .balign 8

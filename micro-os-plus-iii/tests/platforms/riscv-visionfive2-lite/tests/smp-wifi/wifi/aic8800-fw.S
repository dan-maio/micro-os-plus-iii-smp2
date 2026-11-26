/* aic8800-fw.S -- the AIC8800D80 FullMAC firmware images, linked into
 * .rodata and downloaded to the chip's RAM at 0x00120000 through its boot
 * ROM (aic8800.cpp).  Two images: the regular U02/U03 silicon and the "H"
 * variant; the chip-id word read from the boot ROM chooses.
 *
 * The blobs are AICSemi's, unmodified; see third_party/aic8800/README.md for
 * where they come from and their licence status. */
    .section .rodata.aic8800_fw, "a", @progbits
    .balign 8
    .globl aic8800_fw_u02_start
    .globl aic8800_fw_u02_end
aic8800_fw_u02_start:
    .incbin "third_party/aic8800/fmacfw_8800d80_u02.bin"
aic8800_fw_u02_end:
    .balign 8
    .globl aic8800_fw_h_u02_start
    .globl aic8800_fw_h_u02_end
aic8800_fw_h_u02_start:
    .incbin "third_party/aic8800/fmacfw_8800d80_h_u02.bin"
aic8800_fw_h_u02_end:
    .balign 8

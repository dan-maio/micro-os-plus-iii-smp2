# third_party/aic8800 — AIC8800 FullMAC firmware

`fmacfw_8800d80_u02.bin` and `fmacfw_8800d80_h_u02.bin` are the Wi-Fi firmware
images for the **AICSemi AIC8800D80/D40** FullMAC module that the VisionFive 2
Lite carries on SDIO1.  The names are the vendor's: `_u02` is the U02/U03
silicon image, `_h_u02` the "H" variant.  The chip-id word read from the
module's boot ROM (`aic8800.cpp`) chooses between them.

They are linked into the test image by `wifi/aic8800-fw.S` and downloaded to
the chip's RAM at `0x00120000` by the boot-ROM protocol in `aic8800.cpp`.  The
files are used unmodified.

Provenance: AICSemi's AIC8800 vendor driver, `fw/aic8800D80/` (the same images
are distributed through linux-firmware as `aic8800/`).  The driver sources in
`wifi/aic8800.cpp` and `wifi/sdio1.cpp` are a port of that vendor driver's
register and message sequences.

Licence: the vendor's binary-firmware terms — redistribution of the
unmodified image is permitted, reverse engineering is not, and the AICSemi
copyright notice must be kept.  Check the `LICENCE` file in the vendor drop
before redistributing this tree.

# third_party/rtw88

`rtw8723d_fw.bin` is the Realtek RTL8723D WLAN firmware (version 48) from the
[linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git)
repository, path `rtw88/rtw8723d_fw.bin`.  It is linked into the image by
`src/wifi/rtw/rtw_fw.S` and downloaded into the chip's 8051 CPU at start-up.

Licence: Realtek's redistributable firmware licence, `LICENCE.rtlwifi_firmware.txt`
in linux-firmware ("Redistribution and use in binary form, without
modification, are permitted ..."; no reverse engineering, Realtek copyright
notice kept).  The file is used unmodified.

The driver sources in `src/wifi/rtw/` are a port of the Linux `rtw88` driver
(`drivers/net/wireless/realtek/rtw88`, dual licensed GPL-2.0 OR BSD-3-Clause,
Copyright (c) 2018-2019 Realtek Corporation and contributors).

# -----------------------------------------------------------------------------
# The Raspberry Pi OpenOCD, for the RP2350's RISC-V cores (riscv-pico2 and
# riscv-pico2-rp2350b-psram): sets UOS_RISCV_PICO_OPENOCD and _rpi_ocd_args,
# the OPENOCD/SCRIPTS arguments of uos_tests_add_openocd().
# -----------------------------------------------------------------------------

# The Raspberry Pi OpenOCD (the pico-sdk's, which its VS Code extension puts
# under ~/.pico-sdk/openocd/<version>/; on this machine that home may also be
# on a mounted disk). The newest found, unless set.
file (GLOB _rpi_ocds
      "$ENV{HOME}/.pico-sdk/openocd/*/openocd"
      "/run/media/*/*/home/$ENV{USER}/.pico-sdk/openocd/*/openocd")
set (_rpi_ocd "")
if (_rpi_ocds)
  list (SORT _rpi_ocds COMPARE NATURAL ORDER DESCENDING)
  list (GET _rpi_ocds 0 _rpi_ocd)
endif ()
set (UOS_RISCV_PICO_OPENOCD "${_rpi_ocd}" CACHE FILEPATH
     "The Raspberry Pi OpenOCD (rp2350-riscv); the RP2350's RISC-V cores need it")
set (_rpi_ocd_args "")
if (UOS_RISCV_PICO_OPENOCD AND EXISTS "${UOS_RISCV_PICO_OPENOCD}")
  get_filename_component (_rpi_ocd_dir "${UOS_RISCV_PICO_OPENOCD}" DIRECTORY)
  set (_rpi_ocd_args OPENOCD "${UOS_RISCV_PICO_OPENOCD}" SCRIPTS "${_rpi_ocd_dir}/scripts")
else ()
  message (WARNING "${PLATFORM_NAME}: no Raspberry Pi OpenOCD (~/.pico-sdk/openocd/*/openocd); "
           "the hwd tests are registered with the xPack OpenOCD, which cannot drive the RISC-V cores. "
           "Set UOS_RISCV_PICO_OPENOCD and configure again.")
endif ()


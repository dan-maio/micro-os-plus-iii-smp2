# -----------------------------------------------------------------------------
# micro-os-plus::board for a RISC-V board platform.
#
#   uos_riscv_board_library ()
#
# The board facts are the platform's (tests/platforms/<P>/cmake/board.cmake);
# this builds the board target from them: the board's include/ and src/ (the
# platform's own), its defines and flags, and its libraries. This is what the
# RISC-V port's "-board-interface" carried when the port still read the
# board. The port's own settings (its include/, __ARM_EABI__, the forced
# riscv-eabi-compat.h) come with micro-os-plus::riscv, not from here.
#
# Facts read: UOS_BOARD_SRC_DIR, UOS_BOARD_FLAGS, UOS_BOARD_NCPU,
# UOS_BOARD_CAPS, UOS_BOARD_LINKER_HW, UOS_BOARD_DEFINES, and optionally
# UOS_BOARD_LINK_FLAGS (link-only flags after the others: the last -march
# chooses the multilib) and UOS_BOARD_LIBS.
#
# A macro, not a function: the facts it checks and completes stay visible to
# the tests' builder, which the platform adds next.
# -----------------------------------------------------------------------------

include_guard (GLOBAL)

macro (uos_riscv_board_library)
  foreach (_required IN ITEMS UOS_BOARD_SRC_DIR UOS_BOARD_FLAGS UOS_BOARD_NCPU
                              UOS_BOARD_CAPS UOS_BOARD_LINKER_HW
                              UOS_BOARD_DEFINES)
    if (NOT DEFINED ${_required})
      message (FATAL_ERROR
        "platforms/${PLATFORM_NAME}/cmake/board.cmake does not set ${_required}.")
    endif ()
  endforeach ()

  get_filename_component (UOS_BOARD_SRC_DIR "${UOS_BOARD_SRC_DIR}" ABSOLUTE)
  if (NOT IS_DIRECTORY "${UOS_BOARD_SRC_DIR}")
    message (FATAL_ERROR
      "platforms/${PLATFORM_NAME}/cmake/board.cmake points at ${UOS_BOARD_SRC_DIR}, which does not exist.")
  endif ()

  file (GLOB _board_sources CONFIGURE_DEPENDS
        "${UOS_BOARD_SRC_DIR}/src/*.c" "${UOS_BOARD_SRC_DIR}/src/*.cpp"
        "${UOS_BOARD_SRC_DIR}/src/*.S")
  # Not the test start-up beside them (platform-support.cpp and the
  # <suite>-support.cpp files): the tests' builder adds those where they belong.
  list (FILTER _board_sources EXCLUDE REGEX "/[a-z0-9-]*support\\.cpp$")

  add_library (board-${PLATFORM_NAME}-interface INTERFACE)
  target_include_directories (board-${PLATFORM_NAME}-interface INTERFACE
    "${UOS_BOARD_SRC_DIR}/include")
  target_sources (board-${PLATFORM_NAME}-interface INTERFACE ${_board_sources})
  target_compile_definitions (board-${PLATFORM_NAME}-interface INTERFACE ${UOS_BOARD_DEFINES})
  target_compile_options    (board-${PLATFORM_NAME}-interface INTERFACE ${UOS_BOARD_FLAGS})
  target_link_options       (board-${PLATFORM_NAME}-interface INTERFACE ${UOS_BOARD_FLAGS}
                                                                       ${UOS_BOARD_LINK_FLAGS})
  target_link_libraries     (board-${PLATFORM_NAME}-interface INTERFACE ${UOS_BOARD_LIBS})
  add_library (micro-os-plus::board ALIAS board-${PLATFORM_NAME}-interface)
endmacro ()

cmake_minimum_required(VERSION 3.20.0)

set(SEEED_BOARD_ROOT_REL "${CMAKE_CURRENT_LIST_DIR}/../../../../platform-seeedboards/zephyr")
set(SEEED_BOARD_ROOT_ABS "D:/workspace/xiao_nrf54lm20b/platform-seeedboards/zephyr")

if(EXISTS "${SEEED_BOARD_ROOT_REL}/boards")
  list(APPEND BOARD_ROOT "${SEEED_BOARD_ROOT_REL}")
elseif(EXISTS "${SEEED_BOARD_ROOT_ABS}/boards")
  list(APPEND BOARD_ROOT "${SEEED_BOARD_ROOT_ABS}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(usb_cdc_blink)

target_sources(app PRIVATE src/main.c)

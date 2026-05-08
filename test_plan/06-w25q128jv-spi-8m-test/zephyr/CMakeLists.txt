# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.13.1)

if(NOT DEFINED BOARD_ROOT)
  set(SEEED_PLATFORM_BOARD_ROOT "$ENV{ZEPHYR_BASE}/../../platforms/Seeed Studio/zephyr")
  if(EXISTS "${SEEED_PLATFORM_BOARD_ROOT}/boards")
    set(BOARD_ROOT "${SEEED_PLATFORM_BOARD_ROOT}")
  endif()
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(w25q128jv_spi_8m_demo)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})

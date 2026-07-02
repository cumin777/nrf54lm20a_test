cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../platform-seeedboards/zephyr")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(npm1300_fuel_gauge_500ma_20b)

add_subdirectory(src)

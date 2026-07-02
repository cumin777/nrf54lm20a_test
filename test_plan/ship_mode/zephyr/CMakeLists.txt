# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
set(BOARD_ROOT "$ENV{ZEPHYR_BASE}/../../platforms/Seeed Studio/zephyr")
cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ship_mode)
FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})

# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

cmake_minimum_required(VERSION 3.20.0)

# The official application stays in the Edge AI add-on.  Keeping the source
# there makes this board port small and lets it follow add-on updates.
set(EDGE_AI_GESTURE_APP_DIR
    "D:/workspace/ncs/edge_add_on_2/edge-ai/applications/gesture_recognition"
    CACHE PATH "Official nRF Edge AI gesture_recognition application")

if(NOT EXISTS "${EDGE_AI_GESTURE_APP_DIR}/src/main.c")
  message(FATAL_ERROR
    "EDGE_AI_GESTURE_APP_DIR is not a gesture_recognition application: "
    "${EDGE_AI_GESTURE_APP_DIR}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_nrf54lm20b_gesture_recognition)

set(OFFICIAL_SRC "${EDGE_AI_GESTURE_APP_DIR}/src")

# This XIAO board targets nRF54LM20B CPUAPP.  Reuse the official nRF54LM20 DK
# Axon model, which runs on the LM20B NPU.
set(NN_SOURCE "${OFFICIAL_SRC}/nrf_edgeai_generated/nrf54lm20dk/Axon")
set(NN_PATH "${NN_SOURCE}/nrf_edgeai_user_model.c")

if(NOT EXISTS "${NN_PATH}")
  message(FATAL_ERROR "Official Neuton model not found: ${NN_PATH}")
endif()

target_include_directories(app PRIVATE
  ${OFFICIAL_SRC}
  ${OFFICIAL_SRC}/hw_modules
  ${OFFICIAL_SRC}/ble
  ${OFFICIAL_SRC}/uxsm
  ${NN_SOURCE}
)

target_sources(app PRIVATE
  ${OFFICIAL_SRC}/main.c
  ${OFFICIAL_SRC}/inference_postprocessing.c
  ${OFFICIAL_SRC}/hw_modules/button/button.c
  ${OFFICIAL_SRC}/hw_modules/led/led.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/imu_lsm6dsl.c
  ${OFFICIAL_SRC}/ble/ble_common.c
  ${OFFICIAL_SRC}/uxsm/ux_state_manager.c
  ${OFFICIAL_SRC}/ble/hid/ble_hid.c
  ${NN_PATH}
)

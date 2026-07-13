# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_FIRMWARE_LOADER_IMAGE_USB_MCUMGR)
  add_overlay_dts(${SB_CONFIG_FIRMWARE_LOADER_IMAGE_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/boards/xiao_nrf54lm20b_nrf54lm20a_cpuapp_fw_loader.overlay
  )
endif()

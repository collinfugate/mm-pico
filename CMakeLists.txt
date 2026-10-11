# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# No SDK or compiler path is written in this file. They come from flash.py,
# from the Pico VS Code extension (the block above), or from the PICO_SDK_PATH
# environment variable, so the same checkout builds on any computer.
include(pico_sdk_import.cmake)

project(mm_firmware C CXX ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(target_name mm_firmware)

add_executable(${target_name}
    src/main_device.c
    src/usb_descriptors.c
    src/mm.c
)

# Header search path (so tinyusb can find tusb_config.h)
target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

# IMPORTANT: pico_enable_stdio_usb pulls in pico_fix/rp2040_usb_device_enumeration,
# which is the silicon-errata fix needed for USB enumeration on strict hosts (macOS).
# Without this call, the fix's source file never gets compiled into the binary,
# even if PICO_RP2040_USB_DEVICE_ENUMERATION_FIX is defined.
pico_enable_stdio_usb(${target_name} 1)
pico_enable_stdio_uart(${target_name} 0)

# RP2040 silicon errata fixes for USB enumeration. tinyusb_additions sets these
# internally, but defining them explicitly keeps behavior identical across
# pico-sdk versions on different dev machines.
target_compile_definitions(${target_name} PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
)

target_link_libraries(${target_name} PRIVATE
    pico_stdlib
    tinyusb_device
    tinyusb_board       # for device mode (replaces tinyusb_host which is host mode)
    tinyusb_additions   # provides USB enumeration/uframe fix defines
    hardware_clocks
    hardware_spi
    hardware_i2c
)

# Print memory usage at link time
target_link_options(${target_name} PRIVATE -Xlinker --print-memory-usage)

pico_add_extra_outputs(${target_name})

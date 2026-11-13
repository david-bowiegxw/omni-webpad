cmake_minimum_required(VERSION 3.13)

# Supported boards: pico_w (RP2040) and pico2_w (RP2350).
set(PICO_BOARD pico_w CACHE STRING "Board")
if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH)
  message(FATAL_ERROR "Set PICO_SDK_PATH to Pico SDK 2.2.0 or newer")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(omniwebpad C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(FW ${ROOT}/firmware)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web_content.c
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/embed.py ${ROOT}/web/index.html ${CMAKE_CURRENT_BINARY_DIR}/web_content.c
  DEPENDS ${ROOT}/web/index.html ${ROOT}/tools/embed.py VERBATIM)

file(GLOB CORE_SOURCES ${FW}/core/*.c ${FW}/core/modes/*.c)
add_executable(omniwebpad main.c dhcpserver.c ${CORE_SOURCES} ${FW}/usb/usb_link.c
  ${CMAKE_CURRENT_BINARY_DIR}/web_content.c)
target_include_directories(omniwebpad PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${FW}/core ${FW}/usb)
# Core 1 is never started, so flash writes only need interrupts off on core 0.
target_compile_definitions(omniwebpad PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)
target_link_libraries(omniwebpad pico_stdlib pico_unique_id pico_flash hardware_flash hardware_watchdog
  tinyusb_device tinyusb_board pico_cyw43_arch_lwip_poll)
pico_enable_stdio_usb(omniwebpad 0)
pico_enable_stdio_uart(omniwebpad 0)
target_compile_options(omniwebpad PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(omniwebpad PROPERTIES OUTPUT_NAME omniwebpad-${PICO_BOARD})
pico_add_extra_outputs(omniwebpad)

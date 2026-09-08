cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w CACHE STRING "Board")
set(PICO_NO_PICOTOOL 1)
if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH)
  message(FATAL_ERROR "Set PICO_SDK_PATH to Pico SDK 2.2.0")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(ps3_picow C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web_content.h
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed.py
 ${CMAKE_CURRENT_SOURCE_DIR}/src/index.html ${CMAKE_CURRENT_BINARY_DIR}/web_content.h
 DEPENDS src/index.html tools/embed.py VERBATIM)
add_executable(ps3_picow src/main.c src/ds3.c src/packet.c src/dhcpserver.c ${CMAKE_CURRENT_BINARY_DIR}/web_content.h)
target_include_directories(ps3_picow PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ps3_picow pico_stdlib pico_unique_id tinyusb_device tinyusb_board pico_cyw43_arch_lwip_poll)
pico_enable_stdio_usb(ps3_picow 0)
pico_enable_stdio_uart(ps3_picow 0)
target_compile_options(ps3_picow PRIVATE -Wall -Wextra -Wno-unused-parameter)
pico_add_extra_outputs(ps3_picow)
add_custom_command(TARGET ps3_picow POST_BUILD
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/uf2.py
 ${CMAKE_CURRENT_BINARY_DIR}/ps3_picow.bin ${CMAKE_CURRENT_BINARY_DIR}/ps3-picow.uf2 VERBATIM)

cmake_minimum_required(VERSION 3.13)
project(omniwebpad_sim C)
set(CMAKE_C_STANDARD 11)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(FW ${ROOT}/firmware)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web_content.c
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/embed.py ${ROOT}/web/index.html ${CMAKE_CURRENT_BINARY_DIR}/web_content.c
  DEPENDS ${ROOT}/web/index.html ${ROOT}/tools/embed.py VERBATIM)

file(GLOB CORE_SOURCES ${FW}/core/*.c ${FW}/core/modes/*.c)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

add_executable(omniwebpad-sim main.c ${CORE_SOURCES} ${FW}/net/sock_net.c
  ${CMAKE_CURRENT_BINARY_DIR}/web_content.c)
target_include_directories(omniwebpad-sim PRIVATE ${FW}/core ${FW}/net)

add_executable(core-test ${ROOT}/tests/core_test.c ${CORE_SOURCES})
target_include_directories(core-test PRIVATE ${FW}/core)
target_compile_options(core-test PRIVATE -UNDEBUG)   # the tests are assert()s
enable_testing()
add_test(NAME core COMMAND core-test)

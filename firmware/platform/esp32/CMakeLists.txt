cmake_minimum_required(VERSION 3.16)
# ESP-IDF 5.3 or newer. Targets: esp32s2, esp32s3 (USB), esp32s3, esp32c3,
# esp32c6, esp32 (Bluetooth LE).   idf.py set-target esp32s3 && idf.py build
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(omniwebpad)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
set(FW ${ROOT}/firmware)
file(GLOB CORE_SOURCES ${FW}/core/*.c ${FW}/core/modes/*.c)

set(srcs main.c ble_link.c ${CORE_SOURCES} ${FW}/net/sock_net.c ${CMAKE_BINARY_DIR}/web_content.c)
set(includes . ${FW}/core ${FW}/net ${FW}/usb)
set(requires esp_wifi esp_netif esp_event esp_timer nvs_flash esp_driver_gpio lwip bt)

# Native USB (USB-OTG) exists on the S2 and S3 only.
if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
  list(APPEND srcs ${FW}/usb/usb_link.c)
  list(APPEND requires usb)
endif()

idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${includes} PRIV_REQUIRES ${requires})
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/web_content.c
  COMMAND ${PYTHON} ${ROOT}/tools/embed.py ${ROOT}/web/index.html ${CMAKE_BINARY_DIR}/web_content.c
  DEPENDS ${ROOT}/web/index.html ${ROOT}/tools/embed.py VERBATIM)

if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
  # TinyUSB is configured by our tusb_config.h.
  idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
  target_include_directories(${tusb_lib} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
endif()

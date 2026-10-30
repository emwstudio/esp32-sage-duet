cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../vendor/waveshare/firmware/brookesia/components/waveshare__esp32_s3_touch_amoled_1_75"
    "${CMAKE_CURRENT_LIST_DIR}/../vendor/waveshare/firmware/brookesia/components/bsp_extra"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_sage)

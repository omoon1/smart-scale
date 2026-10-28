cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32s3 CACHE STRING "Target MCU")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_scale VERSION 0.1.0)
# Override ESP-IDF's default C++23 flag for all C++ components.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++20" APPEND)

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT DEFINED LUXIO_BUILD)
  message(FATAL_ERROR "Set LUXIO_BUILD to the firmware build number")
endif()
set(PROJECT_VER "${LUXIO_BUILD}")
project(Luxio)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++2a;-DCHIP_HAVE_CONFIG_H" APPEND)

idf_build_set_property(COMPILE_DEFINITIONS "CHIP_PROJECT_CONFIG_INCLUDE=<${CMAKE_SOURCE_DIR}/chip_config.h>" APPEND)

include(${CMAKE_CURRENT_SOURCE_DIR}/bounded-pools.cmake)

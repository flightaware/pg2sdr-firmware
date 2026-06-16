cmake_minimum_required(VERSION 3.15)

# this has to be set very early on, before a project() call
set(CMAKE_TOOLCHAIN_FILE "lpc4370.cmake" CACHE FILEPATH
  "LPC4370 cmake toolchain file")

# Extract firmware version from pg2sdr_protocol.h
file(STRINGS "include/pg2sdr_protocol.h" VERSION_MACRO REGEX "^#define PG2_CURRENT_VERSION")
string(REGEX MATCH "^#define PG2_CURRENT_VERSION _VERSION\\(([0-9]+),([0-9]+),([0-9]+),([0-9]+)\\)" _ ${VERSION_MACRO})
set(FIRMWARE_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${CMAKE_MATCH_4}")

project(pg2sdr-firmware
  LANGUAGES C ASM
  VERSION ${FIRMWARE_VERSION})

set(CMAKE_C_STANDARD 11)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

set(TARGET_HARDWARE PG2SDR CACHE STRING "Target hardware (PG2SDR or AIRSPY)")

message(STATUS "Building firmware version ${FIRMWARE_VERSION}")
message(STATUS "  for target hardware ${TARGET_HARDWARE}")

add_subdirectory(string)
add_subdirectory(src)
add_subdirectory(lpc_chip_43xx/src)

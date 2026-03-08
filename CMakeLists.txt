cmake_minimum_required(VERSION 3.15)

# this has to be set very early on, before a project() call
set(CMAKE_TOOLCHAIN_FILE "lpc4370.cmake" CACHE FILEPATH
  "LPC4370 cmake toolchain file")

project(pg2sdr-firmware LANGUAGES C ASM)

set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

add_subdirectory(string)
add_subdirectory(src)
add_subdirectory(lpc_chip_43xx/src)

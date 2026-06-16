find_package(Python3 REQUIRED)

string(TOLOWER ${TARGET_HARDWARE} hw)
set(image ${CMAKE_BINARY_DIR}/${hw}-firmware-${PROJECT_VERSION}$<$<CONFIG:Debug>:-debug>.bin)

add_executable(firmware.elf
    pg2sdr_dma.c pg2sdr_gpio.c pg2sdr_hsadc.c pg2sdr_ipc.c pg2sdr_m4clock.c
    pg2sdr_main.c pg2sdr_panic.c pg2sdr_spifi.c pg2sdr_startup.c pg2sdr_tuner.c
    pg2sdr_uart.c pg2sdr_usb.c pg2sdr_led.c relocator.s)

target_compile_options(firmware.elf PRIVATE -Os -Wall -Wmissing-declarations -Werror -Wstack-usage=512 -fstack-usage -fsingle-precision-constant)
target_compile_options(firmware.elf PRIVATE -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=)

target_compile_definitions(firmware.elf PRIVATE HW_IS_${TARGET_HARDWARE})
target_compile_definitions(firmware.elf PRIVATE $<$<CONFIG:Debug>:DEBUG>)
target_include_directories(firmware.elf PRIVATE ../include)

target_link_libraries(firmware.elf lpc_chip_43xx string gcc)

file(REAL_PATH pg2sdr.ld ldscript)
set_target_properties(firmware.elf PROPERTIES LINK_DEPENDS ${ldscript})
target_link_options(firmware.elf PRIVATE -T${ldscript})

target_link_options(firmware.elf PRIVATE LINKER:--gc-sections LINKER:-Map,firmware.map)

# Turn the ELF executable into a loadable firmware image
add_custom_command(
  TARGET firmware.elf
  POST_BUILD COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/make-image.py firmware.elf ${image}
  BYPRODUCTS ${image})

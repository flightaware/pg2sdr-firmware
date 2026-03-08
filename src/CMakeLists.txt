find_package(Python3 REQUIRED)

macro(make_firmware_image target image hwmacro)
  add_executable(${target}
    pg2sdr_dma.c pg2sdr_gpio.c pg2sdr_hsadc.c pg2sdr_ipc.c pg2sdr_m4clock.c
    pg2sdr_main.c pg2sdr_panic.c pg2sdr_spifi.c pg2sdr_startup.c pg2sdr_tuner.c
    pg2sdr_uart.c pg2sdr_usb.c relocator.s)

  target_compile_options(${target} PRIVATE -Os -Wall -Wmissing-declarations -Werror -Wstack-usage=512 -fstack-usage -fsingle-precision-constant)
  target_compile_options(${target} PRIVATE -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=)

  target_compile_definitions(${target} PRIVATE HW_IS_${hwmacro})
  target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:DEBUG>)
  target_include_directories(${target} PRIVATE ../include)

  target_link_libraries(${target} lpc_chip_43xx string gcc)

  file(REAL_PATH pg2sdr.ld ldscript)
  set_target_properties(${target} PROPERTIES LINK_DEPENDS ${ldscript})
  target_link_options(${target} PRIVATE -T${ldscript})

  target_link_options(${target} PRIVATE LINKER:--gc-sections LINKER:-Map,${target}.map)

  # Turn the ELF executable into a loadable firmware image
  add_custom_command(
    TARGET ${target}
    POST_BUILD COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/make-image.py ${target} ${image}
    BYPRODUCTS ${image})
endmacro()

make_firmware_image(pg2sdr.elf pg2sdr.bin PG2SDR)
make_firmware_image(pg2sdr-airspy.elf pg2sdr-airspy.bin AIRSPY)

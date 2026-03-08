add_library(lpc_chip_43xx STATIC
  adc_18xx_43xx.c ccan_18xx_43xx.c dac_18xx_43xx.c enet_18xx_43xx.c
  gpdma_18xx_43xx.c hsadc_18xx_43xx.c i2s_18xx_43xx.c otp_18xx_43xx.c
  ring_buffer.c sct_18xx_43xx.c sdmmc_18xx_43xx.c stopwatch_18xx_43xx.c
  uart_18xx_43xx.c aes_18xx_43xx.c chip_18xx_43xx.c eeprom_18xx_43xx.c
  evrt_18xx_43xx.c gpio_18xx_43xx.c i2c_18xx_43xx.c iap_18xx_43xx.c
  pinint_18xx_43xx.c ritimer_18xx_43xx.c sct_pwm_18xx_43xx.c spi_18xx_43xx.c
  sysinit_18xx_43xx.c wwdt_18xx_43xx.c atimer_18xx_43xx.c
  clock_18xx_43xx.c emc_18xx_43xx.c fpu_init.c gpiogroup_18xx_43xx.c
  i2cm_18xx_43xx.c lcd_18xx_43xx.c pmc_18xx_43xx.c rtc_18xx_43xx.c
  sdif_18xx_43xx.c ssp_18xx_43xx.c timer_18xx_43xx.c)

target_compile_options(lpc_chip_43xx PRIVATE
  -Os -Wall -Wstack-usage=512 -fstack-usage -ffunction-sections -fdata-sections -fsingle-precision-constant)
target_compile_definitions(lpc_chip_43xx PUBLIC CORE_M4)
target_include_directories(lpc_chip_43xx PUBLIC ../inc ../inc/config_43xx ../inc/usbd_rom)

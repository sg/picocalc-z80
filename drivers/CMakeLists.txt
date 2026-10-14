cmake_minimum_required(VERSION 3.13)

# PicoCalc hardware drivers, vendored from Lana-chan/picocalc_lua.
# See drivers/README.md for the local changes.

add_library(drivers INTERFACE)

pico_generate_pio_header(drivers ${CMAKE_CURRENT_LIST_DIR}/st7789_lcd.pio)

target_sources(drivers INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/lcd.c
	${CMAKE_CURRENT_LIST_DIR}/keyboard.c
	${CMAKE_CURRENT_LIST_DIR}/term.c
	${CMAKE_CURRENT_LIST_DIR}/draw.c
	${CMAKE_CURRENT_LIST_DIR}/multicore.c
)

target_include_directories(drivers INTERFACE
	${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(drivers INTERFACE
	pico_stdlib
	pico_multicore
	pico_fatfs
	hardware_pio
	hardware_i2c
	hardware_irq
	hardware_watchdog
)

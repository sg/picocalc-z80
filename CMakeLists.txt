cmake_minimum_required(VERSION 3.13)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT PICO_BOARD)
	set(PICO_BOARD pimoroni_pico_plus2_w_rp2350 CACHE STRING "Board type")
endif()

set(CMAKE_C_STANDARD 11)

# git submodules; run "git submodule update --init" after cloning, or point
# these at other checkouts with -D
set(Z80PACK "${CMAKE_CURRENT_LIST_DIR}/submodules/z80pack"
	CACHE PATH "z80pack checkout, dev branch")
set(PICO_FATFS "${CMAKE_CURRENT_LIST_DIR}/submodules/pico_fatfs"
	CACHE PATH "elehobica/pico_fatfs checkout")

foreach(dep Z80PACK PICO_FATFS)
	if(NOT EXISTS "${${dep}}/CMakeLists.txt" AND NOT EXISTS "${${dep}}/z80core")
		message(FATAL_ERROR "${dep} not found at '${${dep}}'. "
			"Pass -D${dep}=/path/to/it.")
	endif()
endforeach()

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# the linker script variables below arrived in 2.3.0; 2.3.1 is the tested one
if(PICO_SDK_VERSION_STRING VERSION_LESS "2.3.1")
	message(FATAL_ERROR "Pico SDK 2.3.1 or later is required, found ${PICO_SDK_VERSION_STRING}")
endif()

project(picocalc-z80 C CXX ASM)

pico_sdk_init()

# FatFs itself from the submodule, but not its tf_card.c: sdcard/ holds a
# copy that checks the card's CRCs
add_subdirectory(${PICO_FATFS}/pio/spi pio_spi)
add_subdirectory(drivers)

add_executable(${PROJECT_NAME}
	srcsim/picosim.c
	srcsim/disks.c
	srcsim/keymap.c
	srcsim/status.c
	srcsim/crash.c
	srcsim/psram.c
	srcsim/scrollback.c
	srcsim/overlay.c
	srcsim/drivemenu.c
	srcsim/loadmenu.c
	srcsim/dazzler.c
	srcsim/hostbdos.c
	srcsim/rtc_compat.c
	srcsim/simcfg.c
	srcsim/simio.c
	srcsim/simmem.c
	srcsim/net_vars.c
	sdcard/tf_card.c
	${PICO_FATFS}/fatfs/ff.c
	${PICO_FATFS}/fatfs/ffsystem.c
	${PICO_FATFS}/fatfs/ffunicode.c
	${Z80PACK}/iodevices/rtc80.c
	${Z80PACK}/iodevices/sd-fdc.c
	${Z80PACK}/z80core/sim8080.c
	${Z80PACK}/z80core/simcore.c
	${Z80PACK}/z80core/simdis.c
	${Z80PACK}/z80core/simglb.c
	${Z80PACK}/z80core/simice.c
	${Z80PACK}/z80core/simz80-cb.c
	${Z80PACK}/z80core/simz80-dd.c
	${Z80PACK}/z80core/simz80-ddcb.c
	${Z80PACK}/z80core/simz80-ed.c
	${Z80PACK}/z80core/simz80-fd.c
	${Z80PACK}/z80core/simz80-fdcb.c
	${Z80PACK}/z80core/simz80.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/fatfs_conf
	${CMAKE_CURRENT_LIST_DIR}/sdcard
	${PICO_FATFS}/fatfs
	${CMAKE_CURRENT_LIST_DIR}/srcsim
	${Z80PACK}/iodevices
	${Z80PACK}/z80core
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	# RP2350 has no calendar RTC block, but srcsim/rtc_compat.c still wants
	# the datetime_t type that pico-sdk only defines by default on RP2040
	PICO_INCLUDE_RTC_DATETIME=1
	CONF_FILE="PCALC.DAT"
	# srcsim/crash.c records a panic and restarts, so the banner can show it
	PICO_PANIC_FUNCTION=picocalc_panic
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

# pico_fatfs is not -Wextra clean upstream
set_source_files_properties(
	${CMAKE_CURRENT_LIST_DIR}/sdcard/tf_card.c
	${PICO_FATFS}/fatfs/ff.c
	${PICO_FATFS}/fatfs/ffunicode.c
	${PICO_FATFS}/fatfs/ffsystem.c
	PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare"
)

# vendored PicoCalc drivers, likewise not -Wextra clean upstream
set_source_files_properties(
	${CMAKE_CURRENT_LIST_DIR}/drivers/lcd.c
	${CMAKE_CURRENT_LIST_DIR}/drivers/keyboard.c
	${CMAKE_CURRENT_LIST_DIR}/drivers/term.c
	${CMAKE_CURRENT_LIST_DIR}/drivers/draw.c
	${CMAKE_CURRENT_LIST_DIR}/drivers/multicore.c
	PROPERTIES COMPILE_OPTIONS
	"-Wno-sign-compare;-Wno-unused-parameter;-Wno-char-subscripts;-Wno-unused-function;-Wno-unused-variable;-Wno-old-style-declaration;-Wno-parentheses"
)
if(PICO_C_COMPILER_IS_GNU)
	target_compile_options(${PROJECT_NAME} PRIVATE -fdiagnostics-color=always)
elseif(PICO_C_COMPILER_IS_CLANG)
	target_compile_options(${PROJECT_NAME} PRIVATE -fcolor-diagnostics)
endif()

target_link_libraries(${PROJECT_NAME}
	pico_stdlib
	pico_multicore
	pico_aon_timer
	pio_spi
	drivers
	hardware_clocks
	hardware_spi
	hardware_watchdog
)

pico_add_extra_outputs(${PROJECT_NAME})

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)

pico_set_program_name(${PROJECT_NAME} "z80pack picocalc-z80")
pico_set_program_description(${PROJECT_NAME} "z80pack on the ClockworkPi PicoCalc")
# the boot banner shows this: a tagged checkout reads v0.2.0, later commits
# v0.2.0-5-g9e11580 and local edits add a +; build.sh and CI pass it in
set(PICOCALC_VERSION "" CACHE STRING "version shown in the banner")
if(NOT PICOCALC_VERSION)
	execute_process(COMMAND git describe --tags --always --dirty=+
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		OUTPUT_VARIABLE PICOCALC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
endif()
if(NOT PICOCALC_VERSION)
	set(PICOCALC_VERSION "unknown")
endif()
# the copyright runs from the first year to the year of the build
string(TIMESTAMP PICOCALC_YEAR "%Y")
if(PICOCALC_YEAR GREATER 2026)
	set(PICOCALC_YEARS "2026-${PICOCALC_YEAR}")
else()
	set(PICOCALC_YEARS "2026")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
	USR_REL="${PICOCALC_VERSION}"
	USR_CPR_YEARS="${PICOCALC_YEARS}")
pico_set_program_version(${PROJECT_NAME} "${PICOCALC_VERSION}")
# the short board name release assets carry, picocalc-z80-<board>-<tag>.uf2,
# which the update check needs to find its own; build.sh passes it in
set(PICOCALC_BOARD "" CACHE STRING "short board name used in release file names")
if(NOT PICOCALC_BOARD)
	if(PICO_BOARD STREQUAL "pico_w")
		set(PICOCALC_BOARD "picow")
	elseif(PICO_BOARD STREQUAL "pico2_w")
		set(PICOCALC_BOARD "pico2w")
	elseif(PICO_BOARD STREQUAL "pimoroni_pico_plus2_w_rp2350")
		set(PICOCALC_BOARD "pim2w")
	else()
		set(PICOCALC_BOARD "${PICO_BOARD}")
	endif()
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE USR_BOARD="${PICOCALC_BOARD}")

# The console is the PicoCalc LCD and keyboard, registered as an stdio driver
# by drivers/term.c. Enabling USB adds a second console in parallel, which is
# useful when the display itself is what you are debugging.
option(PICOCALC_CONSOLE_USB "also expose the console over USB CDC" OFF)

# The mainboard's USB-C port is a CH340 bridged to UART0 on GP0/GP1 (with the
# DIP switch off), so this mirrors the console to a terminal program on a PC.
# srcsim/uartcon.c stands in for the SDK's stdio_uart to buffer pasted input.
option(PICOCALC_CONSOLE_UART "also mirror the console on the USB-C serial port" ON)
set(PICOCALC_UART_BAUD 115200 CACHE STRING "USB-C serial console baud rate")
pico_enable_stdio_uart(${PROJECT_NAME} 0)
if(PICOCALC_CONSOLE_UART)
	target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/srcsim/uartcon.c)
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		PICOCALC_CONSOLE_UART=1
		PICOCALC_UART_BAUD=${PICOCALC_UART_BAUD}
	)
	target_link_libraries(${PROJECT_NAME} hardware_uart)
endif()
# WiFi on boards that have the CYW43 radio (the Pico Plus 2 W does). lwIP
# runs in the background on core 0; see srcsim/wifi.h.
if(PICO_CYW43_SUPPORTED)
	option(PICOCALC_WIFI "WiFi, NTP clock set and the network menu" ON)
else()
	set(PICOCALC_WIFI OFF)
endif()
if(PICOCALC_WIFI)
	# the update's gunzip, submodules/uzlib
	set(UZLIB ${CMAKE_CURRENT_LIST_DIR}/submodules/uzlib/src)
	target_sources(${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/srcsim/wifi.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/netsock.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/modem.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/http.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/wifimenu.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/update.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/sha256.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/version.c
		${CMAKE_CURRENT_LIST_DIR}/srcsim/untar.c
		${UZLIB}/tinflate.c
		${UZLIB}/tinfgzip.c
		${UZLIB}/crc32.c
		${UZLIB}/adler32.c
		${Z80PACK}/iodevices/libtelnet.c
	)
	target_include_directories(${PROJECT_NAME} PRIVATE ${UZLIB})
	set_source_files_properties(${Z80PACK}/iodevices/libtelnet.c
		PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-implicit-fallthrough")
	set_source_files_properties(${UZLIB}/tinflate.c
		PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare")
	target_compile_definitions(${PROJECT_NAME} PRIVATE PICOCALC_WIFI=1)
	# lwipopts.h is found by include path, from the project root
	target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
	target_link_libraries(${PROJECT_NAME} pico_cyw43_arch_lwip_threadsafe_background)
	# the Pico W's 264 KB cannot hold WiFi beside the full machine, so it
	# gets half the scrollback, fewer lwIP buffers and a smaller heap floor
	if(PICO_PLATFORM STREQUAL "rp2040")
		target_compile_definitions(${PROJECT_NAME} PRIVATE
			PICOCALC_SMALL_RAM=1
			PICO_HEAP_SIZE=16384
		)
		set(PICOCALC_TLS OFF)
	else()
		# HTTPS for the update client: mbedtls under lwIP's altcp, with
		# mbedtls_config.h from the project root; too big for the Pico W
		option(PICOCALC_TLS "HTTPS in the update client" ON)
	endif()
	if(PICOCALC_TLS)
		target_compile_definitions(${PROJECT_NAME} PRIVATE PICOCALC_TLS=1)
		target_link_libraries(${PROJECT_NAME} pico_lwip_mbedtls pico_mbedtls)
	endif()
endif()
# core 0's stack: the TLS handshake runs on it, inside lwIP's callbacks, and
# was measured at 2.5 KB with everything else on the core, too close to the
# SDK's 4 KB scratch bank, so the stack moves into main RAM
if(PICOCALC_TLS)
	# core 1 runs on picosim.c's own stack; the SDK's default one only has
	# to fit its scratch bank
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		PICO_STACK_SIZE=16384
		PICO_CORE1_STACK_SIZE=4096)
	# the SDK puts core 0's stack at the top of scratch Y, which holds nothing
	# else here, so scratch Y becomes the last 16 KB of main RAM
	pico_set_linker_script_var(${PROJECT_NAME} RAM_LENGTH 0x7C000)
	pico_set_linker_script_var(${PROJECT_NAME} SCRATCH_Y_ORIGIN 0x2007C000)
	pico_set_linker_script_var(${PROJECT_NAME} SCRATCH_Y_LENGTH 0x4000)
else()
	target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_STACK_SIZE=4096)
endif()
# paints core 0's stack at boot and reports the untouched part after an
# update check, to size PICO_STACK_SIZE
option(PICOCALC_STACK_WATERMARK "report core 0 stack headroom in the update menu" OFF)
if(PICOCALC_STACK_WATERMARK)
	target_compile_definitions(${PROJECT_NAME} PRIVATE PICOCALC_STACK_WATERMARK=1)
endif()
if(NOT PICOCALC_WIFI OR NOT PICO_PLATFORM STREQUAL "rp2040")
	target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_HEAP_SIZE=32768)
endif()

if(PICOCALC_CONSOLE_USB)
	pico_enable_stdio_usb(${PROJECT_NAME} 1)
else()
	pico_enable_stdio_usb(${PROJECT_NAME} 0)
endif()

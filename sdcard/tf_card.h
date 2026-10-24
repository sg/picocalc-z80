#pragma once

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/spi.h"

#define CLK_SLOW_DEFAULT     (100 * KHZ)
#define CLK_FAST_DEFAULT     (50 * MHZ)
// CLK_FAST: actually set to clk_peri (= 125.0 MHz) / N,
// which is determined by spi_set_baudrate() in pico-sdk/src/rp2_common/hardware_spi/spi.c

/*
 * SPI pin default assignment
 *   Refer to _pin_miso_conf, _pin_sck_conf, _pin_mosi_conf in tf_card.c
 *     for permitted pin assignment for SPI0 and SPI1 usecase
 *   If the pin assignment constraints for SPI0/SPI1 are not satisfied,
 *     SPI PIO will be configured instead of SPI function
 */
#define PIN_SPI0_MISO_DEFAULT   4
#define PIN_SPI0_CS_DEFAULT     5
#define PIN_SPI0_SCK_DEFAULT    2
#define PIN_SPI0_MOSI_DEFAULT   3

#define PIN_SPI1_MISO_DEFAULT   8
#define PIN_SPI1_CS_DEFAULT     9
#define PIN_SPI1_SCK_DEFAULT    10
#define PIN_SPI1_MOSI_DEFAULT   11

#define SPI_PIO_DEFAULT_PIO (pio0)
#define SPI_PIO_DEFAULT_SM  (0)

typedef struct _pico_fatfs_spi_config_t {
    spi_inst_t* spi_inst;  // spi0 or spi1
    uint        clk_slow;
    uint        clk_fast;
    uint        pin_miso;
    uint        pin_cs;
    uint        pin_sck;
    uint        pin_mosi;
    bool        pullup;     // miso, mosi pins only
} pico_fatfs_spi_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
* Set configuration
*
* @param[in] config the pointer of pico_fatfs_spi_config_t to configure with
*
* @return true if configured for SPI, false if configured for PIO SPI
*/
bool pico_fatfs_set_config(pico_fatfs_spi_config_t* config);

/**
* Configure SPI PIO
*
* @param[in] pio the PIO instance (pio0/pio1 for rp2040, pio0/pio1/pio2 for rp2350)
* @param[in] sm the state machine number of the PIO (0 ~ 3)
*/
void pico_fatfs_config_spi_pio(PIO pio, uint sm);

/**
* Reset SPI access
*
* @return 1: OK, 0: Timeout
*/
int pico_fatfs_reboot_spi(void);

/**
* Get clk_slow frequency
* If called before disk_initialize(), it returns the default setting frequency.
* If called after disk_initialize(), it returns the actual operation frequency.
*
* @return frequency in Hz
*/
uint pico_fatfs_get_clk_slow_freq(void);

/**
* Get clk_fast frequency
* If called before disk_initialize(), it returns the default setting frequency.
* If called after disk_initialize(), it returns the actual operation frequency.
*
* @return frequency in Hz
*/
uint pico_fatfs_get_clk_fast_freq(void);

/**
* Whether the card accepted CRC_ON_OFF (CMD59) at the last disk_initialize(),
* so that every command and data block is CRC checked in both directions
*
* @return true if CRC checking is on
*/
bool pico_fatfs_crc_enabled(void);

/**
* Count of block reads and writes that failed, by CRC or otherwise, and were
* retried or reported to FatFs, since boot
*
* @return number of failed attempts
*/
uint pico_fatfs_get_error_count(void);

/**
* Report that the card may have been removed or swapped, for instance from a
* card-detect interrupt. The next FatFs call re-initialises the card and
* remounts the volume, which invalidates files and directories left open.
* Safe to call from an interrupt or the other core.
*/
void pico_fatfs_media_changed(void);

#ifdef __cplusplus
}
#endif
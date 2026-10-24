# SD card driver

`tf_card.c` and `tf_card.h` are the SPI driver from elehobica's
[pico_fatfs](https://github.com/elehobica/pico_fatfs), copied at `e0e512b`
(BSD 2-clause, see `LICENSE`). FatFs itself still builds from
`submodules/pico_fatfs`. picocalc-ftpserver carries the same copy.

## Local changes

- CRC checking is turned on at init (CMD59). Every command now carries a
  real CRC7 and every written block a real CRC16, and the CRC16 of every
  block read is checked. Upstream sends dummy CRCs and discards the card's,
  so a bit flipped on the SPI bus reached FatFs unnoticed; a 51 MB download
  from picocalc-ftpserver once came back with a single bit wrong. Here a
  misread can do worse, because FatFs writes a whole 512-byte sector back
  for every 128-byte CP/M sector, and CP/M rewrites what it reads.
- `disk_read` and `disk_write` retry from the block that failed, up to 3
  attempts in a row, as FatFs fails a file for good on the first disk error.
- CMD12 follows CMD18 even when CMD18's reply was garbled, and is resent
  while the card reports it arrived damaged; the stop token follows CMD25
  the same way. A card left streaming, or waiting for data, refuses every
  command after.
- `GET_BLOCK_SIZE` reads the whole 64-byte SD status, as the CRC covers
  all of it.
- `pico_fatfs_crc_enabled()` and `pico_fatfs_get_error_count()` report
  whether the card took CMD59 and how many block transfers have failed.
- `pico_fatfs_media_changed()` marks the card as possibly swapped, so the
  next FatFs call re-initialises it and remounts the volume. A card-detect
  interrupt can call it, leaving every FatFs call on the core that makes
  them.

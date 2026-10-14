# Plan: WiFi and a virtual Hayes AT modem

Status: phases 1 and 2 done; phase 3 built and tested in hostsim, not yet on hardware; phase 4 not started.

## Sources

| Piece | From | Notes |
| ----- | ---- | ----- |
| WiFi join, scan, status, IP | `picocalc_lua/drivers/wifi.c` | Plain C, no Lua coupling. Keep the link-settle fix (spurious -7 on connect). Scan callback dedupes by SSID, keeps strongest RSSI. |
| TCP client/server, RX ring, DNS | `picocalc_lua/drivers/socket.c` | Keep the pbuf-retention receive path (ACK only what was copied). Drop UDP, ping, TLS. |
| `lwipopts.h` | `picocalc_lua/lwipopts.h` | NO_SYS 1, MEM_SIZE 8192, PBUF_POOL 16, TCP_WND 4*MSS, SND_BUF 2*MSS. Remove ALTCP/TLS options. |
| AT command set, S-registers, `+++`, telnet options | `z80pack/iodevices/generic-at-modem.c` (MIT, D. McNaughton) | Replace POSIX sockets, `poll`, `getaddrinfo`, `getenv`. |
| Telnet protocol | `z80pack/iodevices/libtelnet.c/.h` (public domain) | Portable as is; no zlib. Cap its subnegotiation buffer growth. |
| NTP clock set | `Pico-Eval-Board-80/srcsim/net_ntp.c` (Udo) | Settings (`ntp_server`, `utc_offset`) already in `PCALC.DAT`. |

## Architecture

- **Core 0 owns the radio.** `cyw43_arch_init()` (threadsafe_background) runs on core 0, so lwIP callbacks run as low-priority IRQs there, away from the Z80 loop. picocalc_lua initialises on core 1; we deliberately don't.
- **Nothing on core 1 blocks.** The Z80 side calls a small non-blocking `netsock` API (`open`, `state`, `read`, `write`, `close`) wrapped in `cyw43_arch_lwip_begin/end`. DNS and connect complete by callback; the modem polls state. picocalc_lua's wait loops (`cyw43_arch_poll(); sleep_ms()`) are dropped — `poll` is a no-op in this mode anyway.
- **Flow control falls out of the ports.** SIO-2 B "TX ready" reflects `tcp_sndbuf() > 0`; "RX ready" reflects the 4 KB RX ring. The TCP window back-pressures the far end when CP/M is slow.
- **Blocking is fine in the config dialog** (scan, connect with progress), since the machine isn't running yet.

## I/O ports (IMSAI SIO-2, as z80pack's IMSAI)

| Port | Use |
| ---- | --- |
| 22h/23h | Channel A — USB-C serial raw (done) |
| 24h/25h | Channel B — modem data / status (bit 0 TxRDY, bit 1 RxRDY) |
| 28h | Control — bit 6 = channel B carrier detect |

This is what QTerm (`^^ U` then `B`), Udo's IMSAI BIOS and Kermit expect. XMODEM over the modem: `/X2 /P 25 24 01 02 01`.

## Modem behaviour

Keep z80pack's command set: `AT`, `A/`, `AT$`, `ATI`, `ATZ`, `AT&F`, `ATD`, `ATH`, `ATO`, `ATSn`/`?`/`=v`, `ATE`, `ATQ`, `AT&A`, `ATA`, `AT+T`, S-registers 0-49 including telnet option registers S15-S22.

Changes from z80pack:

- `ATD` strips `T`/`P` (`ATDTbbs.example.com:23` works) and defaults to port 23, not 8023.
- Dialling becomes a state: `ATD` → DNS → connect → `CONNECT` / `NO ANSWER`, without stalling the CPU. Any key while dialling aborts, as on a real modem.
- `ATA` is non-blocking: `NO CARRIER` if nothing is pending.
- Accept lowercase `at`, ignore spaces, and add `ATH0` and `ATV`/`ATX` as no-op `OK`s so common init strings don't fail.
- `ATI1` shows SSID, IP and link state.
- `+++` guard time to 1 s using `time_us_64()`.
- Telnet is on by default (S15=1); `ATS15=0` gives raw TCP.
- `MODEM.init` comes from a config dialog field instead of an environment variable.
- `AT+WIFI` manages WiFi from CP/M. Scan and connect run in the background: `OK` at once, results or `WIFI CONNECTED <ip>` / `WIFI FAILED` when done. Quoted arguments keep their case. A successful connect saves the SSID and password to `PCALC.DAT`.

  | Command | Action |
  | --- | --- |
  | `AT+WIFI?` | State, SSID, IP, RSSI |
  | `AT+WIFI=SCAN` | Numbered list of networks |
  | `AT+WIFI=n,"password"` | Join network n from the last scan |
  | `AT+WIFI="SSID","password"` | Join by name |
  | `AT+WIFI=UP` | Reconnect with the saved settings |
  | `AT+WIFI=DOWN` | Disconnect |

- Batch writes: collect bytes and `tcp_output` once per poll or when 64 bytes are queued, rather than one segment per character.

## Configuration dialog

`n - network: <state> <SSID> <IP>` (Udo's key) opens:

```
WiFi: connected to HomeNet  192.168.1.42  -52 dBm

s - scan for networks
1-9 - pick a network from the last scan
e - enter SSID manually
p - password: ********
c - connect
d - disconnect
a - connect at boot: yes
y - country: US
n - NTP server: pool.ntp.org
u - UTC offset: -8
m - modem init: ATS15=1
q - back
```

- Scan prints numbered SSIDs with RSSI, channel and open/WPA.
- Picking one fills the SSID and prompts for the password if it's secured.
- NTP sets the clock every time WiFi comes up, however it was connected.
- Connect at boot is asynchronous (`cyw43_arch_wifi_connect_async`). The boot banner doesn't wait, and the status line shows progress.

## Status line

Line 1 becomes `Z80 max  192.168.1.42 [drive activity]   clock  battery`.

- `WiFi...` while joining, `WiFi fail` on error, nothing when WiFi is off.
- A trailing `*` while the modem has carrier.
- Below about 60 columns the IP is dropped before the drive activity.

## Config file

- `WIFI_PWD_LEN` is 33 (Udo's); WPA2 allows 63. Growing it shifts every later field in `PCALC.DAT`.
- Add a magic/version header. Files without it are read with the old layout and rewritten in the new one, so existing cards keep their settings.
- New fields: country, connect-at-boot, modem init string.
- Password stored in plaintext, as now; masked on screen.

## Build and footprint

- `PICOCALC_WIFI` option, on when `PICO_CYW43_SUPPORTED`.
- Link `pico_cyw43_arch_lwip_threadsafe_background`; no mbedTLS.
- Estimated RAM: 60-80 KB SRAM for cyw43, lwIP pools and one connection (picocalc_lua's measured range after its buffer cuts). Current use is 245 KB of 512 KB.
- Flash grows by about 250 KB for the CYW43 firmware blob.

## Testing

- The modem core talks to `netsock` only, so `tools/hostsim` gets a POSIX `netsock` backend.
- On Linux, dial real BBSes and a local `nc -l`, and run QTerm and XMODEM against them before touching hardware.
- WiFi and lwIP themselves can only be tested on the device.

## Phases

1. **WiFi base:** netsock/wifi port, core 0 init, `w` menu, boot auto-connect, status line IP, config file versioning, NTP clock set.
2. **Outbound modem:** ports 24h/25h/28h, AT parser, async dial, raw and telnet, `+++`/`ATH`/`ATO`, hostsim backend, README.
3. **Inbound:** `AT&A` listen on S14, `RING`, S0 auto-answer, `ATA` — run a CP/M BBS reachable over WiFi.
4. **Optional:** BIOS IOBYTE routing (Kermit `SET PORT UC1`, RDR:/PUN: to the modem); a comms disk image with QTerm, Kermit and XMODEM configured for 24h/25h.

## Decisions

- Network menu stays on `n`.
- Telnet negotiation on by default.
- WiFi also configurable from CP/M with `AT+WIFI`.
- NTP sets the clock on every WiFi connect.
- Z80 on core 1, WiFi and lwIP on core 0.

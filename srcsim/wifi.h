/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * WiFi station for the Pico Plus 2 W, plus an NTP clock set on every join.
 *
 * The radio is owned by core 0: cyw43_arch_init() binds lwIP's callbacks to
 * the core that calls it, and core 1 is busy being the Z80. So core 1 only
 * posts requests and reads cached state, and wifi_service(), run from core
 * 0's idle loop, makes every driver and lwIP call. None of these block
 * except wifi_wait(), which the config dialog uses while the machine is
 * stopped anyway.
 */

#ifndef WIFI_INC
#define WIFI_INC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	WIFI_OFF,	/* radio not started */
	WIFI_IDLE,	/* started, not joined */
	WIFI_JOINING,	/* associating or waiting for DHCP */
	WIFI_UP,	/* joined with an address */
	WIFI_FAILED	/* last join failed, see wifi_fail_reason() */
} wifi_state_t;

#define WIFI_SCAN_MAX	24

typedef struct {
	char ssid[33];
	int16_t rssi;
	uint8_t channel;
	uint8_t auth;	/* cyw43 bits: 1 WEP, 2 WPA, 4 WPA2, 0 open */
} wifi_scan_result_t;

/* core 0 */
extern void wifi_service(void);

/* core 1: requests, all asynchronous */
extern void wifi_start(const char *country);
extern void wifi_connect(const char *ssid, const char *password);
extern void wifi_disconnect(void);
extern void wifi_scan(void);

/* core 1: a request is queued, or a join or scan is in progress */
extern bool wifi_busy(void);

/* core 1: wait for the last request to finish, false on timeout */
extern bool wifi_wait(uint32_t timeout_ms);

/* either core */
extern wifi_state_t wifi_get_state(void);
extern const char *wifi_fail_reason(void);
extern bool wifi_get_ip(char *buf, size_t len);
extern const char *wifi_get_ssid(void);
extern int wifi_get_rssi(void);
extern bool wifi_scan_busy(void);
extern int wifi_scan_count(void);
extern const wifi_scan_result_t *wifi_scan_result(int i);
extern bool wifi_clock_synced(void);
/* the state in a few words, with the network and address when joined */
extern void wifi_status_text(char *buf, size_t len);

#endif /* !WIFI_INC */

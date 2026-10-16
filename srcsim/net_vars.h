/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2025 by Udo Munk
 *
 * This module contains global variables for the network configuration
 * of Pico W.
 * Not compiled conditionally because the configuration is saved on MicroSD,
 * and I want files with identical format for Pico and Pico W.
 *
 * History:
 * 18-MAY-2025 first implementation
 */

#ifndef NET_VARS_INC
#define NET_VARS_INC

#define WIFI_SSID_LEN	63
/* WPA2 allows 63; Udo's 33 survives only as the legacy config layout */
#define WIFI_PWD_LEN	63
#define WIFI_PWD_LEN_V1	33
#define HOST_NAME_MAX	63
#define DEFAULT_NTP	"pool.ntp.org"
#define MODEM_INIT_LEN	63
#define UPDATE_URL_LEN	127

extern char wifi_ssid[WIFI_SSID_LEN+1];
extern char wifi_password[WIFI_PWD_LEN+1];
extern char ntp_server[HOST_NAME_MAX+1];
extern int utc_offset;
extern char wifi_country[3];	/* ISO 3166 alpha-2, empty for worldwide */
extern int wifi_autoconnect;
extern char modem_init_str[MODEM_INIT_LEN+1];
extern char update_url[UPDATE_URL_LEN+1];	/* a repository page, see update.h */

#endif /* !NET_VARS_INC */

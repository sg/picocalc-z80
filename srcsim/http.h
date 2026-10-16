/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * A small HTTP client, used by update.c to fetch releases.
 *
 * The split is netsock.c's: core 0 owns the connection and copies what
 * arrives into a ring, holding a pbuf back while the ring is full so that
 * TCP flow control follows the reader; core 1 sends the request, parses the
 * reply out of the ring and hands the body to a sink. The buffers come from
 * a work area the caller lends, because the client only runs while the
 * machine is stopped and its memory is idle.
 */

#ifndef HTTP_INC
#define HTTP_INC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* a power of two; it comes out of the machine's idle memory on every board,
   and the bigger it is the longer a slow SD write can go before TCP has to
   hold the sender back */
#define HTTP_RING_SIZE	16384
#define HTTP_HDR_SIZE	8192	/* GitHub's replies carry 5 KB of headers */
#define HTTP_URL_MAX	2048	/* its signed asset redirects run to 1 KB */
#define HTTP_REQ_SIZE	(HTTP_URL_MAX + 256)
#define HTTP_WORK_SIZE	(HTTP_RING_SIZE + HTTP_HDR_SIZE + HTTP_URL_MAX + \
			 HTTP_REQ_SIZE)

typedef struct {
	int status;		/* 0 when no reply was read */
	long length;		/* Content-Length, -1 when absent */
	long received;		/* body bytes given to the sink */
	const char *location;	/* Location header of the last reply, or NULL */
} http_reply_t;

/* return false to stop the transfer */
typedef bool (*http_sink_t)(const uint8_t *data, int len, void *arg);

/* core 0, from wifi_service(); lwip_up is false until the radio started */
extern void http_service(bool lwip_up);

/* core 1: lend HTTP_WORK_SIZE bytes before the first request */
extern void http_init(uint8_t *work);

/* core 1: GET, following redirects, with the body going to sink; false
   with *err set when anything went wrong */
extern bool http_get(const char *url, http_sink_t sink, void *arg,
		     http_reply_t *reply, const char **err);

/* core 1: one request, stopping after the headers; a redirect is reported,
   not followed */
extern bool http_probe(const char *url, http_reply_t *reply,
		       const char **err);

/* the body in flight, for the status line; total is 0 when unknown */
extern volatile uint32_t http_bytes, http_total;
extern volatile bool http_active;

#endif /* !HTTP_INC */

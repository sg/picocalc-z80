/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * SHA-256 (FIPS 180-4) for checking downloads against a release's sums
 * file. Self-contained so the boards without TLS have it too.
 */

#ifndef SHA256_INC
#define SHA256_INC

#include <stddef.h>
#include <stdint.h>

#define SHA256_LEN	32

typedef struct {
	uint32_t h[8];
	uint64_t len;
	uint8_t block[64];
	size_t fill;
} sha256_t;

extern void sha256_init(sha256_t *c);
extern void sha256_update(sha256_t *c, const void *data, size_t len);
extern void sha256_final(sha256_t *c, uint8_t out[SHA256_LEN]);

#endif /* !SHA256_INC */

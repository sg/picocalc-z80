/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Software update from a release site.
 *
 * The update URL names a Forgejo, Gitea or GitHub repository. Its
 * releases/latest page redirects to the newest published tag, which is all
 * the version check needs, and the release assets are then fetched from
 * releases/download/<tag>/<name>.
 *
 * The client's buffers are borrowed from the machine's memory, which is
 * idle while the configuration dialog runs, so update_end() has to put the
 * memory back before the machine starts.
 */

#ifndef UPDATE_INC
#define UPDATE_INC

#include <stdbool.h>
#include <stddef.h>

extern void update_begin(void);
extern void update_end(void);

/* the version the SD card files came from, /Z80/VERSION.TXT */
extern bool update_card_version(char *buf, size_t len);

/* the newest release tag at the update URL; false with *err set */
extern bool update_latest(char *tag, size_t len, const char **err);

#endif /* !UPDATE_INC */

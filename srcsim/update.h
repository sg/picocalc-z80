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
 * Nothing is flashed from here. The firmware goes to the directory pelrun's
 * uf2loader reads, and the user reboots into the loader to install it.
 *
 * The client's buffers are borrowed from the machine's memory, which is
 * idle while the configuration dialog runs, so update_end() has to put the
 * memory back before the machine starts.
 */

#ifndef UPDATE_INC
#define UPDATE_INC

#include <stdbool.h>
#include <stddef.h>

/* a release tag, v0.1.8 or so */
#define TAG_LEN	31

extern void update_begin(void);
extern void update_end(void);

/* the version the SD card files came from, /Z80/VERSION.TXT */
extern bool update_card_version(char *buf, size_t len);

/* the newest release tag at the update URL; false with *err set */
extern bool update_latest(char *tag, size_t len, const char **err);

/* install the release: its firmware for this board into the loader's
   directory, then its SD card files over the Z80 directory, each checked
   against the release's sums file; reports on the console */
extern bool update_run(const char *tag);

#endif /* !UPDATE_INC */

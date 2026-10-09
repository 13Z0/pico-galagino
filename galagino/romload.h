/*
 * ROM set loader, see romload.c.
 */
#ifndef GAL_ROMLOAD_H
#define GAL_ROMLOAD_H

#include <stdint.h>

#include "galagino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Searches dir, then dir/<mameset>: the set's own zip first, then any other
 * zip, then loose files. `found` is the mask of files already loaded (bit i =
 * g->files[i]); the updated mask is returned. Each file is written to its
 * region at its offset; with regions == NULL nothing is loaded and the mask
 * only says which files are there. A NULL entry in regions skips the files of
 * that region: they are neither loaded nor counted. */
uint32_t gal_romload(const gal_game_info_t *g, uint8_t *const *regions, const gal_io_t *io, const char *dir,
                     uint32_t found);

#ifdef __cplusplus
}
#endif

#endif

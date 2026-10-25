#ifndef ROMZIP_H
#define ROMZIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reads the ROM out of a zipped cartridge ROM, so it can be hashed the way
   minarch hashes it: minarch unzips such games before loading them and
   hands rcheevos the file inside, not the archive. Only what ROM zips use:
   stored or deflated entries, no ZIP64, no encryption. */

/* Extracts the first file in the zip that looks like a ROM (skipping
   folders, dotfiles and readme-type files), up to max_size bytes. Returns
   a malloc'd buffer the caller frees, or NULL. */
uint8_t *RomZip_ExtractFirstRom(const char *path, size_t max_size, size_t *out_size,
                                char *name_out, size_t name_size);

#endif

#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stddef.h>

/* HTTP and file helpers shared by the club page and the ROM finder. */

typedef struct {
    char *data;  /* NUL-terminated, so text can be read in place */
    size_t size;
} Io_Buffer;

/* GETs url into buffer, which the caller frees. Only a 200 counts, so an
   error page is never mistaken for content. Blocking. */
bool Io_HttpGet(const char *url, Io_Buffer *buffer);

/* Writes via a temporary file, so a failed write never replaces a good copy. */
bool Io_WriteFileAtomic(const char *path, const char *data, size_t len);

/* Reads a whole file, NUL-terminated. Returns NULL on failure. */
char *Io_ReadFile(const char *path, size_t *out_len);

/* Seconds since path was modified, or -1 if it does not exist. */
long Io_FileAge(const char *path);

#endif

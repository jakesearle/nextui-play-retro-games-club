#ifndef PATHS_H
#define PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* $SHARED_USERDATA_PATH/prgc-tracker on device, ~/.userdata/prgc-tracker
   otherwise, optionally followed by subdir. */
bool Paths_UserData(char *out_path, size_t out_size, const char *subdir);

/* mkdir -p */
bool Paths_MakeDirs(const char *path);

#endif

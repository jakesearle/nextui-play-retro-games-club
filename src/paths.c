#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "paths.h"

bool Paths_UserData(char *out_path, size_t out_size, const char *subdir) {
    char base[400];

    const char *shared = getenv("SHARED_USERDATA_PATH");
    const char *home = getenv("HOME");

    if (shared && shared[0]) {
        snprintf(base, sizeof(base), "%s/prgc-tracker", shared);
    } else if (home && home[0]) {
        snprintf(base, sizeof(base), "%s/.userdata/prgc-tracker", home);
    } else {
        return false;
    }

    int written = subdir && subdir[0]
        ? snprintf(out_path, out_size, "%s/%s", base, subdir)
        : snprintf(out_path, out_size, "%s", base);

    return written > 0 && (size_t)written < out_size;
}

bool Paths_MakeDirs(const char *path) {
    char partial[512];
    if (snprintf(partial, sizeof(partial), "%s", path) >= (int)sizeof(partial)) {
        return false;
    }

    for (char *p = partial + 1; *p; p++) {
        if (*p != '/') continue;

        *p = '\0';
        if (mkdir(partial, 0755) != 0 && errno != EEXIST) return false;
        *p = '/';
    }

    return mkdir(partial, 0755) == 0 || errno == EEXIST;
}

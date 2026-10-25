/* NextUI's MAX_RECENTS. */
#define LAUNCH_MAX_RECENTS 24

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "launch.h"
#include "paths.h"

bool Launch_AddRecent(const char *rom_path) {
    const char *sdcard = getenv("SDCARD_PATH");
    const char *shared = getenv("SHARED_USERDATA_PATH");
    if (!sdcard || !sdcard[0] || !shared || !shared[0]) return false;

    /* NextUI stores paths relative to the SD card so they survive a card
       moving between devices, which mount it in different places. */
    size_t sd_len = strlen(sdcard);
    if (strncmp(rom_path, sdcard, sd_len) != 0 || rom_path[sd_len] != '/') return false;
    const char *rel = rom_path + sd_len;

    char dir[512], path[600];
    snprintf(dir, sizeof(dir), "%s/.minui", shared);
    if (!Paths_MakeDirs(dir)) return false;
    snprintf(path, sizeof(path), "%s/recent.txt", dir);

    /* Lines are "<path>" or "<path>\t<alias>". An existing entry keeps its
       alias and moves to the top, as NextUI's addRecent does. */
    size_t len = 0;
    char *old = Io_ReadFile(path, &len);
    if (!old) old = calloc(1, 1);
    if (!old) return false;

    char *lines[LAUNCH_MAX_RECENTS * 4];
    int count = 0, found = -1;
    size_t rel_len = strlen(rel);
    for (char *line = old; *line && count < (int)(sizeof(lines) / sizeof(lines[0]));) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (*line) {
            if (found < 0 && strcspn(line, "\t") == rel_len && strncmp(line, rel, rel_len) == 0) {
                found = count;
            }
            lines[count++] = line;
        }
        if (!nl) break;
        line = nl + 1;
    }

    size_t cap = len + rel_len + 2;
    char *out = malloc(cap);
    if (!out) { free(old); return false; }
    size_t out_len = (size_t)snprintf(out, cap, "%s\n", found >= 0 ? lines[found] : rel);
    int kept = 1;
    for (int i = 0; i < count && kept < LAUNCH_MAX_RECENTS; i++) {
        if (i == found) continue;
        out_len += (size_t)snprintf(out + out_len, cap - out_len, "%s\n", lines[i]);
        kept++;
    }

    bool ok = Io_WriteFileAtomic(path, out, out_len);
    free(out);
    free(old);
    return ok;
}

bool Launch_FindResume(const char *rom_path, Launch_Resume *resume) {
    memset(resume, 0, sizeof(*resume));
    const char *shared = getenv("SHARED_USERDATA_PATH");
    char tag[32];
    if (!shared || !shared[0] || !Roms_TagForPath(rom_path, tag, sizeof(tag))) return false;

    const char *file = strrchr(rom_path, '/');
    file = file ? file + 1 : rom_path;

    char path[1024];
    snprintf(path, sizeof(path), "%s/.minui/%s/%s.txt", shared, tag, file);
    size_t len = 0;
    char *text = Io_ReadFile(path, &len);
    if (!text) return false;
    char *end;
    long slot = strtol(text, &end, 10);
    bool ok = end != text && slot >= 0 && slot < LAUNCH_FRESH_SLOT;
    free(text);
    if (!ok) return false;

    resume->slot = (int)slot;
    snprintf(path, sizeof(path), "%s/.minui/%s/%s.%d.bmp", shared, tag, file, resume->slot);
    if (Io_FileAge(path) >= 0) snprintf(resume->preview, sizeof(resume->preview), "%s", path);
    return true;
}

bool Launch_HandOff(const Roms_Match *match, int resume_slot) {
    char text[1100];
    int n = snprintf(text, sizeof(text), "%s\n%s\n%d\n", match->emu_launch, match->rom_path,
                     resume_slot);
    if (n <= 0 || (size_t)n >= sizeof(text)) return false;
    if (!Io_WriteFileAtomic(LAUNCH_FILE, text, (size_t)n)) return false;

    /* Not fatal: the game still launches, it just is not listed. */
    if (!Launch_AddRecent(match->rom_path)) {
        fprintf(stderr, "could not add %s to Recently Played\n", match->rom_path);
    }
    return true;
}

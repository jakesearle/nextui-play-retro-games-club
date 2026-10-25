#ifndef ROMS_H
#define ROMS_H

#include <stdbool.h>
#include <stddef.h>

/* Finds a club pick on the device. The pick's RetroAchievements link gives
   its game id and its title ends in a console label, "(SNES)". RA publishes
   every known ROM hash per console, so the finder fetches that list, keeps
   the hashes for this game, and hashes the ROMs in the matching Roms folders
   with rcheevos (the version NextUI's own RA support uses) until one is on
   the list. Hashes are cached by path, size and mtime, so only new or
   changed ROMs are ever read twice. */

typedef enum {
    ROMS_FOUND = 0,
    ROMS_NOT_FOUND,       /* Searched every candidate; none matched */
    ROMS_UNKNOWN_CONSOLE, /* The title names a console we cannot map */
    ROMS_NO_FOLDER,       /* No Roms folder for that console */
    ROMS_NO_GAMES,        /* Its folders exist but hold no games */
    ROMS_NO_HASHES,       /* RA's hash list is unreachable and not cached */
    ROMS_CANCELLED,
} Roms_Result;

typedef struct {
    char rom_path[512];
    /* The emulator pak's launch.sh, as NextUI would pick it; empty when no
       pak for the ROM's folder tag is installed (always so on desktop). */
    char emu_launch[512];
} Roms_Match;

/* Every ROM that is this game: duplicates, and variants RA lists under the
   same game (an MSU-1 version beside the plain one, say). Best-named
   first. Choosing between them is the caller's business. */
#define ROMS_MAX_MATCHES 16
typedef struct {
    Roms_Match items[ROMS_MAX_MATCHES];
    int count;
} Roms_Matches;

/* The RA console id for a title like "Paper Mario (N64)", or 0. The label
   itself ("N64") is copied to label_out for messages. */
int Roms_ConsoleFromTitle(const char *title, char *label_out, size_t label_size);

/* A console's name for messages, "Nintendo 64" for RC_CONSOLE_NINTENDO_64;
   RA's own name for consoles the club has not picked from yet. NULL for an
   id RA does not know. */
const char *Roms_ConsoleName(int console_id);

/* The Roms folder NextUI creates for the console, "Nintendo Entertainment
   System (FC)", or NULL if it creates none. */
const char *Roms_NextUIFolder(int console_id);

/* Whether NextUI ships an emulator for the console. */
bool Roms_NextUIPlays(int console_id);

/* The name of a Pak Store emulator pak for the console that runs on this
   platform ($PLATFORM), as the store lists it ("Nintendo 64"), or NULL. */
const char *Roms_PakStoreEmulator(int console_id);

/* The game id in a retroachievements.org/game/<id> link, or 0. */
int Roms_GameIdFromUrl(const char *url);

/* Searches the Roms folders for every copy of the game. Blocking, and slow
   the first time a folder is searched (every file in it is read once and
   its hash cached); run it on a worker. progress (0..1) and cancel may be
   NULL. */
Roms_Result Roms_Find(int game_id, int console_id, const char *title, Roms_Matches *matches,
                      float *progress, const int *cancel);

/* One ROM's RA hash, as minarch computes it: zipped cartridge ROMs are
   hashed by the file inside. */
bool Roms_HashFile(int console_id, const char *path, char hash[33]);

/* The emulator tag of the Roms folder a ROM is in, as NextUI's getEmuName
   reads it: ".../Roms/Super Nintendo (SFC)/sub/x.sfc" -> "SFC". */
bool Roms_TagForPath(const char *rom_path, char *tag, size_t tag_size);

/* Where ROMs live: $ROMS_PATH, else $SDCARD_PATH/Roms, else ~/Roms. */
bool Roms_RootPath(char *out, size_t out_size);

#endif

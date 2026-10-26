/* How long a downloaded hash list is trusted before it is fetched again.
   RA adds hashes as people link new dumps; a few days behind is harmless. */
#define ROMS_HASHLIB_MAX_AGE_SECONDS (3L * 24 * 60 * 60)
#define ROMS_MAX_GAME_HASHES 128
#define ROMS_MAX_CANDIDATES 8192
/* Roms/<Console (TAG)>/ plus however the player sorts their games, short of
   following a symlink loop forever. */
#define ROMS_MAX_DEPTH 12
/* The largest ROM unzipped into memory for hashing; N64 tops out at 64 MB. */
#define ROMS_MAX_UNZIPPED (128u * 1024 * 1024)

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "rc_consoles.h"
#include "rc_hash.h"

#include "io.h"
#include "paths.h"
#include "romzip.h"
#include "roms.h"

/* ── Consoles ────────────────────────────────────────────────────────────── */

/* The labels the club page puts in parentheses after a title. Collected
   from its history, plus the obvious neighbours. */
static const struct { const char *label; int console_id; const char *name; } k_labels[] = {
    { "Arcade", RC_CONSOLE_ARCADE, "arcade" },
    { "NES", RC_CONSOLE_NINTENDO, "NES" },
    { "FDS", RC_CONSOLE_FAMICOM_DISK_SYSTEM, "Famicom Disk System" },
    { "SNES", RC_CONSOLE_SUPER_NINTENDO, "SNES" },
    { "N64", RC_CONSOLE_NINTENDO_64, "Nintendo 64" },
    { "GC", RC_CONSOLE_GAMECUBE, "GameCube" },
    { "Wii", RC_CONSOLE_WII, "Wii" },
    { "GB", RC_CONSOLE_GAMEBOY, "Game Boy" },
    { "GBC", RC_CONSOLE_GAMEBOY_COLOR, "Game Boy Color" },
    { "GBA", RC_CONSOLE_GAMEBOY_ADVANCE, "Game Boy Advance" },
    { "NDS", RC_CONSOLE_NINTENDO_DS, "Nintendo DS" },
    { "VB", RC_CONSOLE_VIRTUAL_BOY, "Virtual Boy" },
    { "MD", RC_CONSOLE_MEGA_DRIVE, "Genesis" },
    { "Genesis", RC_CONSOLE_MEGA_DRIVE, "Genesis" },
    { "SMS", RC_CONSOLE_MASTER_SYSTEM, "Master System" },
    { "GG", RC_CONSOLE_GAME_GEAR, "Game Gear" },
    { "SCD", RC_CONSOLE_SEGA_CD, "Sega CD" },
    { "32X", RC_CONSOLE_SEGA_32X, "32X" },
    { "Saturn", RC_CONSOLE_SATURN, "Saturn" },
    { "DC", RC_CONSOLE_DREAMCAST, "Dreamcast" },
    { "PS1", RC_CONSOLE_PLAYSTATION, "PlayStation" },
    { "PSX", RC_CONSOLE_PLAYSTATION, "PlayStation" },
    { "PS2", RC_CONSOLE_PLAYSTATION_2, "PlayStation 2" },
    { "PSP", RC_CONSOLE_PSP, "PSP" },
    { "PCE", RC_CONSOLE_PC_ENGINE, "PC Engine" },
    { "TG16", RC_CONSOLE_PC_ENGINE, "TurboGrafx-16" },
    { "NGP", RC_CONSOLE_NEOGEO_POCKET, "Neo Geo Pocket" },
    { "NGPC", RC_CONSOLE_NEOGEO_POCKET, "Neo Geo Pocket Color" },
    { "Lynx", RC_CONSOLE_ATARI_LYNX, "Lynx" },
    { "2600", RC_CONSOLE_ATARI_2600, "Atari 2600" },
    { "7800", RC_CONSOLE_ATARI_7800, "Atari 7800" },
};

/* Other ways a title might name a RetroAchievements console, mostly the
   short forms emulator frontends use. Anything not here is still matched
   against RA's own names (rc_console_name). */
static const struct { const char *label; int console_id; } k_aliases[] = {
    { "FC", RC_CONSOLE_NINTENDO }, { "Famicom", RC_CONSOLE_NINTENDO },
    { "SFC", RC_CONSOLE_SUPER_NINTENDO }, { "Super Famicom", RC_CONSOLE_SUPER_NINTENDO },
    { "Mega Drive", RC_CONSOLE_MEGA_DRIVE }, { "GEN", RC_CONSOLE_MEGA_DRIVE },
    { "Sega CD", RC_CONSOLE_SEGA_CD }, { "Mega CD", RC_CONSOLE_SEGA_CD },
    { "SAT", RC_CONSOLE_SATURN }, { "PS", RC_CONSOLE_PLAYSTATION },
    { "PCECD", RC_CONSOLE_PC_ENGINE_CD }, { "PCE-CD", RC_CONSOLE_PC_ENGINE_CD },
    { "TGCD", RC_CONSOLE_PC_ENGINE_CD }, { "TG-16", RC_CONSOLE_PC_ENGINE },
    { "PCFX", RC_CONSOLE_PCFX }, { "SG1000", RC_CONSOLE_SG1000 },
    { "Wii U", RC_CONSOLE_WII_U }, { "WiiU", RC_CONSOLE_WII_U },
    { "3DS", RC_CONSOLE_NINTENDO_3DS }, { "DSi", RC_CONSOLE_NINTENDO_DSI },
    { "Xbox", RC_CONSOLE_XBOX }, { "JAG", RC_CONSOLE_ATARI_JAGUAR },
    { "Jaguar", RC_CONSOLE_ATARI_JAGUAR }, { "Jaguar CD", RC_CONSOLE_ATARI_JAGUAR_CD },
    { "5200", RC_CONSOLE_ATARI_5200 }, { "A2600", RC_CONSOLE_ATARI_2600 },
    { "A5200", RC_CONSOLE_ATARI_5200 }, { "A7800", RC_CONSOLE_ATARI_7800 },
    { "ST", RC_CONSOLE_ATARI_ST }, { "WS", RC_CONSOLE_WONDERSWAN },
    { "WSC", RC_CONSOLE_WONDERSWAN }, { "WonderSwan Color", RC_CONSOLE_WONDERSWAN },
    { "NGCD", RC_CONSOLE_NEO_GEO_CD }, { "INTV", RC_CONSOLE_INTELLIVISION },
    { "CV", RC_CONSOLE_COLECOVISION }, { "Coleco", RC_CONSOLE_COLECOVISION },
    { "O2", RC_CONSOLE_MAGNAVOX_ODYSSEY2 }, { "Odyssey 2", RC_CONSOLE_MAGNAVOX_ODYSSEY2 },
    { "DOS", RC_CONSOLE_MS_DOS }, { "C64", RC_CONSOLE_COMMODORE_64 },
    { "CPC", RC_CONSOLE_AMSTRAD_PC }, { "A2", RC_CONSOLE_APPLE_II },
    { "Apple 2", RC_CONSOLE_APPLE_II }, { "CDi", RC_CONSOLE_CDI },
    { "PKM", RC_CONSOLE_POKEMON_MINI }, { "Pokemon Mini", RC_CONSOLE_POKEMON_MINI },
    { "G&W", RC_CONSOLE_GAME_AND_WATCH }, { "N-Gage", RC_CONSOLE_NOKIA_NGAGE },
    { "Pico", RC_CONSOLE_PICO }, { "Channel F", RC_CONSOLE_FAIRCHILD_CHANNEL_F },
};

/* Roms folder tags. NextUI's own table (minarch/ra_consoles.h), plus tags
   community emulator paks commonly use for consoles NextUI ships without. */
static const struct { const char *tag; int console_id; } k_tags[] = {
    { "A2600", RC_CONSOLE_ATARI_2600 }, { "A5200", RC_CONSOLE_ATARI_5200 },
    { "A7800", RC_CONSOLE_ATARI_7800 }, { "32X", RC_CONSOLE_SEGA_32X },
    { "C128", RC_CONSOLE_COMMODORE_64 }, { "C64", RC_CONSOLE_COMMODORE_64 },
    { "COLECO", RC_CONSOLE_COLECOVISION }, { "CPC", RC_CONSOLE_AMSTRAD_PC },
    { "FC", RC_CONSOLE_NINTENDO }, { "FBN", RC_CONSOLE_ARCADE },
    { "FDS", RC_CONSOLE_FAMICOM_DISK_SYSTEM }, { "GB", RC_CONSOLE_GAMEBOY },
    { "GBA", RC_CONSOLE_GAMEBOY_ADVANCE }, { "GBC", RC_CONSOLE_GAMEBOY_COLOR },
    { "GG", RC_CONSOLE_GAME_GEAR }, { "LYNX", RC_CONSOLE_ATARI_LYNX },
    { "MD", RC_CONSOLE_MEGA_DRIVE }, { "MGBA", RC_CONSOLE_GAMEBOY_ADVANCE },
    { "MSX", RC_CONSOLE_MSX }, { "NGP", RC_CONSOLE_NEOGEO_POCKET },
    { "NGPC", RC_CONSOLE_NEOGEO_POCKET }, { "P8", RC_CONSOLE_PICO },
    { "PCE", RC_CONSOLE_PC_ENGINE }, { "PKM", RC_CONSOLE_POKEMON_MINI },
    { "PS", RC_CONSOLE_PLAYSTATION }, { "PSX", RC_CONSOLE_PLAYSTATION },
    { "PUAE", RC_CONSOLE_AMIGA }, { "SEGACD", RC_CONSOLE_SEGA_CD },
    { "SFC", RC_CONSOLE_SUPER_NINTENDO }, { "SG1000", RC_CONSOLE_SG1000 },
    { "SGB", RC_CONSOLE_GAMEBOY }, { "SMS", RC_CONSOLE_MASTER_SYSTEM },
    { "SUPA", RC_CONSOLE_SUPER_NINTENDO }, { "VB", RC_CONSOLE_VIRTUAL_BOY },
    { "VIC", RC_CONSOLE_VIC20 },
};

/* Tags community emulator paks use for consoles NextUI ships without. */
static const struct { const char *tag; int console_id; } k_community_tags[] = {
    { "N64", RC_CONSOLE_NINTENDO_64 }, { "NDS", RC_CONSOLE_NINTENDO_DS },
    { "PSP", RC_CONSOLE_PSP }, { "DC", RC_CONSOLE_DREAMCAST },
    { "FBNEO", RC_CONSOLE_ARCADE }, { "MAME", RC_CONSOLE_ARCADE },
    { "ARCADE", RC_CONSOLE_ARCADE }, { "NEOGEO", RC_CONSOLE_ARCADE },
    { "CPS1", RC_CONSOLE_ARCADE }, { "CPS2", RC_CONSOLE_ARCADE },
    { "CPS3", RC_CONSOLE_ARCADE },
};

static int ConsoleForTag(const char *tag) {
    for (size_t i = 0; i < sizeof(k_tags) / sizeof(k_tags[0]); i++) {
        if (strcasecmp(tag, k_tags[i].tag) == 0) return k_tags[i].console_id;
    }
    for (size_t i = 0; i < sizeof(k_community_tags) / sizeof(k_community_tags[0]); i++) {
        if (strcasecmp(tag, k_community_tags[i].tag) == 0) return k_community_tags[i].console_id;
    }
    return RC_CONSOLE_UNKNOWN;
}

/* The empty Roms folders NextUI's install creates (skeleton/BASE and
   EXTRAS), one per console: where its games are expected to go. */
static const struct { int console_id; const char *folder; } k_folders[] = {
    { RC_CONSOLE_GAMEBOY, "Game Boy (GB)" },
    { RC_CONSOLE_GAMEBOY_ADVANCE, "Game Boy Advance (GBA)" },
    { RC_CONSOLE_GAMEBOY_COLOR, "Game Boy Color (GBC)" },
    { RC_CONSOLE_NINTENDO, "Nintendo Entertainment System (FC)" },
    { RC_CONSOLE_MEGA_DRIVE, "Sega Genesis (MD)" },
    { RC_CONSOLE_PLAYSTATION, "Sony PlayStation (PS)" },
    { RC_CONSOLE_SUPER_NINTENDO, "Super Nintendo Entertainment System (SFC)" },
    { RC_CONSOLE_AMIGA, "Amiga (PUAE)" },
    { RC_CONSOLE_AMSTRAD_PC, "Amstrad CPC (CPC)" },
    { RC_CONSOLE_ARCADE, "Arcade (FBN)" },
    { RC_CONSOLE_ATARI_2600, "Atari 2600 (A2600)" },
    { RC_CONSOLE_ATARI_5200, "Atari 5200 (A5200)" },
    { RC_CONSOLE_ATARI_7800, "Atari 7800 (A7800)" },
    { RC_CONSOLE_ATARI_LYNX, "Atari Lynx (LYNX)" },
    { RC_CONSOLE_COLECOVISION, "Colecovision (COLECO)" },
    { RC_CONSOLE_COMMODORE_64, "Commodore 64 (C64)" },
    { RC_CONSOLE_VIC20, "Commodore VIC20 (VIC)" },
    { RC_CONSOLE_FAMICOM_DISK_SYSTEM, "Famicom Disk System (FDS)" },
    { RC_CONSOLE_MSX, "Microsoft MSX (MSX)" },
    { RC_CONSOLE_NEOGEO_POCKET, "Neo Geo Pocket Color (NGPC)" },
    { RC_CONSOLE_PICO, "Pico-8 (P8)" },
    { RC_CONSOLE_POKEMON_MINI, "Pok\u00e9mon mini (PKM)" },
    { RC_CONSOLE_SEGA_32X, "Sega 32X (32X)" },
    { RC_CONSOLE_SEGA_CD, "Sega CD (SEGACD)" },
    { RC_CONSOLE_GAME_GEAR, "Sega Game Gear (GG)" },
    { RC_CONSOLE_MASTER_SYSTEM, "Sega Master System (SMS)" },
    { RC_CONSOLE_SG1000, "Sega SG-1000 (SG1000)" },
    { RC_CONSOLE_PC_ENGINE, "TurboGrafx-16 (PCE)" },
    { RC_CONSOLE_VIRTUAL_BOY, "Virtual Boy (VB)" },
};

const char *Roms_NextUIFolder(int console_id) {
    for (size_t i = 0; i < sizeof(k_folders) / sizeof(k_folders[0]); i++) {
        if (k_folders[i].console_id == console_id) return k_folders[i].folder;
    }
    return NULL;
}

/* Emulator paks in the Pak Store, by its listing (storefront_base.json):
   the name to search for and the platforms each is built for. */
static const struct { int console_id; const char *name; const char *platforms; } k_store_paks[] = {
    { RC_CONSOLE_NINTENDO_64, "Nintendo 64", " tg5040 tg5050 my355 h700 " },
    { RC_CONSOLE_DREAMCAST, "Dreamcast", " tg5040 h700 " },
    { RC_CONSOLE_PSP, "PPSSPP", " tg5040 tg5050 " },
};

const char *Roms_ConsoleName(int console_id) {
    for (size_t i = 0; i < sizeof(k_labels) / sizeof(k_labels[0]); i++) {
        if (k_labels[i].console_id == console_id) return k_labels[i].name;
    }
    const char *name = rc_console_name((uint32_t)console_id);
    return name && strcmp(name, "Unknown") != 0 ? name : NULL;
}

bool Roms_NextUIPlays(int console_id) {
    for (size_t i = 0; i < sizeof(k_tags) / sizeof(k_tags[0]); i++) {
        if (k_tags[i].console_id == console_id) return true;
    }
    return false;
}

const char *Roms_PakStoreEmulator(int console_id) {
    const char *platform = getenv("PLATFORM");
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", platform ? platform : "");
    for (size_t i = 0; i < sizeof(k_store_paks) / sizeof(k_store_paks[0]); i++) {
        if (k_store_paks[i].console_id != console_id) continue;
        /* Off device there is no platform; name the pak anyway. */
        if (!platform || !platform[0] || strstr(k_store_paks[i].platforms, padded)) {
            return k_store_paks[i].name;
        }
    }
    return NULL;
}

/* NextUI files CD games under the cartridge console's tag (PCE, MD), so a
   folder can hold either; RA keeps them as separate consoles. */
static int CdVariant(int console_id) {
    if (console_id == RC_CONSOLE_PC_ENGINE) return RC_CONSOLE_PC_ENGINE_CD;
    if (console_id == RC_CONSOLE_MEGA_DRIVE) return RC_CONSOLE_SEGA_CD;
    return RC_CONSOLE_UNKNOWN;
}

static const char *Extension(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(path, '.');
    return dot && (!slash || dot > slash) ? dot + 1 : "";
}

static bool IsCdImage(const char *path) {
    static const char *const exts[] = { "chd", "cue", "ccd", "toc", "m3u", "iso", "pbp" };
    const char *ext = Extension(path);
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (strcasecmp(ext, exts[i]) == 0) return true;
    }
    return false;
}

/* The console rcheevos should hash a file as, mirroring minarch. */
static int HashConsoleFor(int folder_console, const char *path) {
    int cd = CdVariant(folder_console);
    return cd != RC_CONSOLE_UNKNOWN && IsCdImage(path) ? cd : folder_console;
}

int Roms_ConsoleFromTitle(const char *title, char *label_out, size_t label_size) {
    if (label_size) label_out[0] = '\0';
    const char *open = strrchr(title, '(');
    const char *close = open ? strchr(open, ')') : NULL;
    if (!close || close[1] != '\0' || close == open + 1) return RC_CONSOLE_UNKNOWN;

    char label[24];
    size_t len = (size_t)(close - open - 1);
    if (len >= sizeof(label)) return RC_CONSOLE_UNKNOWN;
    memcpy(label, open + 1, len);
    label[len] = '\0';
    snprintf(label_out, label_size, "%s", label);

    for (size_t i = 0; i < sizeof(k_labels) / sizeof(k_labels[0]); i++) {
        if (strcasecmp(label, k_labels[i].label) == 0) return k_labels[i].console_id;
    }
    for (size_t i = 0; i < sizeof(k_aliases) / sizeof(k_aliases[0]); i++) {
        if (strcasecmp(label, k_aliases[i].label) == 0) return k_aliases[i].console_id;
    }
    /* RA's own names, "Sega Saturn" or "Atari Jaguar CD". The ids past
       Famicom Disk System are hubs and events, not consoles. */
    for (int id = 1; id <= RC_CONSOLE_FAMICOM_DISK_SYSTEM; id++) {
        const char *name = rc_console_name((uint32_t)id);
        if (name && strcasecmp(label, name) == 0) return id;
    }
    return RC_CONSOLE_UNKNOWN;
}

int Roms_GameIdFromUrl(const char *url) {
    const char *p = url ? strstr(url, "/game/") : NULL;
    return p ? atoi(p + 6) : 0;
}

/* ── RA hash lists ───────────────────────────────────────────────────────── */

typedef struct {
    char hashes[ROMS_MAX_GAME_HASHES][33];
    int count;
} GameHashes;

/* Pulls out the hashes belonging to game_id from a hashlibrary response:
   {"Success":true,"MD5List":{"<md5>":<game id>,...}}. A full JSON parser
   would be overkill for one flat object of string keys and integers. */
static void CollectHashes(const char *json, int game_id, GameHashes *out) {
    const char *p = strstr(json, "\"MD5List\"");
    if (!p) return;

    while ((p = strchr(p, '"')) != NULL && out->count < ROMS_MAX_GAME_HASHES) {
        const char *hash = p + 1;
        const char *end = strchr(hash, '"');
        if (!end) return;
        p = end + 1;
        if (end - hash != 32 || *p != ':') continue;

        if (atoi(p + 1) == game_id) {
            char *dst = out->hashes[out->count++];
            for (int i = 0; i < 32; i++) dst[i] = (char)tolower((unsigned char)hash[i]);
            dst[32] = '\0';
        }
    }
}

/* Adds this console's hashes for game_id, from a fresh download when the
   cached list is old, else from the cache. Returns whether a list (any
   list) was available. */
static bool LoadGameHashes(int console_id, int game_id, GameHashes *out) {
    char dir[400], path[512];
    if (!Paths_UserData(dir, sizeof(dir), "hashlib") || !Paths_MakeDirs(dir)) return false;
    snprintf(path, sizeof(path), "%s/%d.json", dir, console_id);

    long age = Io_FileAge(path);
    if (age < 0 || age > ROMS_HASHLIB_MAX_AGE_SECONDS) {
        char url[128];
        snprintf(url, sizeof(url),
                 "https://retroachievements.org/dorequest.php?r=hashlibrary&c=%d", console_id);
        Io_Buffer response = { 0 };
        if (Io_HttpGet(url, &response) && strstr(response.data, "\"MD5List\"")) {
            Io_WriteFileAtomic(path, response.data, response.size);
        }
        free(response.data);
    }

    size_t len = 0;
    char *json = Io_ReadFile(path, &len);
    if (!json) return false;
    CollectHashes(json, game_id, out);
    free(json);
    return true;
}

static bool HasHash(const GameHashes *game, const char *hash) {
    for (int i = 0; i < game->count; i++) {
        if (strcmp(game->hashes[i], hash) == 0) return true;
    }
    return false;
}

/* ── Local hash cache ────────────────────────────────────────────────────── */

/* hashes-v2.tsv: one "<console>\t<size>\t<mtime>\t<md5>\t<path>" line per file.
   Appended to as files are hashed; a changed file just gets a newer line,
   and lookups take the last match. */
typedef struct {
    int console_id;
    long long size;
    long long mtime;
    char hash[33];
    char *path;
} CacheEntry;

typedef struct {
    CacheEntry *entries;
    int count, capacity;
    char path[512];
} HashCache;

static void CacheAdd(HashCache *cache, int console_id, long long size, long long mtime,
                     const char *hash, const char *path) {
    if (cache->count == cache->capacity) {
        int capacity = cache->capacity ? cache->capacity * 2 : 256;
        CacheEntry *grown = realloc(cache->entries, (size_t)capacity * sizeof(*grown));
        if (!grown) return;
        cache->entries = grown;
        cache->capacity = capacity;
    }
    char *copy = strdup(path);
    if (!copy) return;
    CacheEntry *e = &cache->entries[cache->count++];
    e->console_id = console_id;
    e->size = size;
    e->mtime = mtime;
    snprintf(e->hash, sizeof(e->hash), "%s", hash);
    e->path = copy;
}

static void CacheLoad(HashCache *cache) {
    memset(cache, 0, sizeof(*cache));
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL) || !Paths_MakeDirs(dir)) return;
    /* v2: v1 hashed zipped cartridge ROMs as archives, which never match, so
       its entries for them are wrong; starting a new file drops them all. */
    snprintf(cache->path, sizeof(cache->path), "%s/hashes-v2.tsv", dir);

    FILE *f = fopen(cache->path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        int console_id;
        long long size, mtime;
        char hash[33];
        int consumed = 0;
        if (sscanf(line, "%d\t%lld\t%lld\t%32s\t%n", &console_id, &size, &mtime, hash, &consumed) == 4 &&
            consumed > 0 && line[consumed]) {
            CacheAdd(cache, console_id, size, mtime, hash, line + consumed);
        }
    }
    fclose(f);
}

static const char *CacheLookup(const HashCache *cache, int console_id, long long size,
                               long long mtime, const char *path) {
    for (int i = cache->count - 1; i >= 0; i--) {
        const CacheEntry *e = &cache->entries[i];
        if (e->console_id == console_id && e->size == size && e->mtime == mtime &&
            strcmp(e->path, path) == 0) {
            return e->hash;
        }
    }
    return NULL;
}

static void CacheStore(HashCache *cache, int console_id, long long size, long long mtime,
                       const char *hash, const char *path) {
    CacheAdd(cache, console_id, size, mtime, hash, path);
    if (!cache->path[0] || strchr(path, '\n') || strchr(path, '\t')) return;
    FILE *f = fopen(cache->path, "a");
    if (!f) return;
    fprintf(f, "%d\t%lld\t%lld\t%s\t%s\n", console_id, size, mtime, hash, path);
    fclose(f);
}

static void CacheFree(HashCache *cache) {
    for (int i = 0; i < cache->count; i++) free(cache->entries[i].path);
    free(cache->entries);
}

/* ── Candidates ──────────────────────────────────────────────────────────── */

typedef struct {
    char *path;
    int console_id; /* the folder's console */
    int score;
} Candidate;

typedef struct {
    Candidate *items;
    int count, capacity;
} CandidateList;

/* Files that are never ROMs: artwork, saves, notes and the like. */
static bool IsIgnored(const char *name) {
    static const char *const exts[] = {
        "txt", "png", "jpg", "jpeg", "gif", "bmp", "xml", "dat", "nfo", "pdf",
        "sav", "srm", "state", "cfg", "ini", "db", "json", "log", "ips", "bps", "ups",
    };
    if (name[0] == '.') return true;
    const char *ext = Extension(name);
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (strcasecmp(ext, exts[i]) == 0) return true;
    }
    return false;
}

/* Lowercase alphanumeric words of s, skipping anything in () or [] (region
   and revision tags in file names, the console label in titles). */
static int Words(const char *s, char words[][32], int max) {
    int count = 0, depth = 0;
    size_t len = 0;
    for (;; s++) {
        char c = *s;
        if (c == '(' || c == '[') depth++;
        else if ((c == ')' || c == ']') && depth > 0) depth--;

        bool word_char = depth == 0 && isalnum((unsigned char)c);
        if (word_char && len + 1 < 32) {
            words[count][len++] = (char)tolower((unsigned char)c);
        } else if (!word_char && len > 0) {
            words[count][len] = '\0';
            len = 0;
            if (++count == max) break;
        }
        if (!c) break;
    }
    return count;
}

/* How much a file name looks like the title, 0..1000: the share of the
   title's words that appear in it. Only orders the search; every candidate
   is still hashed before anything is called a match. */
static int Score(char title_words[][32], int title_count, const char *path) {
    if (title_count == 0) return 0;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    char base[256];
    snprintf(base, sizeof(base), "%s", name);
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    char words[32][32];
    int count = Words(base, words, 32);
    int hits = 0;
    for (int i = 0; i < title_count; i++) {
        for (int j = 0; j < count; j++) {
            if (strcmp(title_words[i], words[j]) == 0) { hits++; break; }
        }
    }
    return hits * 1000 / title_count;
}

static bool DirHasCue(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        found = strcasecmp(Extension(e->d_name), "cue") == 0;
    }
    closedir(d);
    return found;
}

static void Collect(CandidateList *list, const char *dir, int console_id, int depth) {
    if (depth > ROMS_MAX_DEPTH) return;
    DIR *d = opendir(dir);
    if (!d) return;

    /* A .cue describes its .bin tracks; hashing the tracks on their own
       would only produce hashes nothing matches. */
    bool has_cue = DirHasCue(dir);

    struct dirent *e;
    while ((e = readdir(d)) != NULL && list->count < ROMS_MAX_CANDIDATES) {
        if (IsIgnored(e->d_name)) continue;

        char path[512];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path)) continue;
        struct stat info;
        if (stat(path, &info) != 0) continue;

        if (S_ISDIR(info.st_mode)) {
            Collect(list, path, console_id, depth + 1);
            continue;
        }
        if (!S_ISREG(info.st_mode)) continue;
        const char *ext = Extension(e->d_name);
        if (has_cue && (strcasecmp(ext, "bin") == 0 || strcasecmp(ext, "img") == 0)) continue;

        if (list->count == list->capacity) {
            int capacity = list->capacity ? list->capacity * 2 : 128;
            Candidate *grown = realloc(list->items, (size_t)capacity * sizeof(*grown));
            if (!grown) break;
            list->items = grown;
            list->capacity = capacity;
        }
        char *copy = strdup(path);
        if (!copy) break;
        list->items[list->count++] = (Candidate){ .path = copy, .console_id = console_id };
    }
    closedir(d);
}

static int ByScore(const void *a, const void *b) {
    const Candidate *ca = a, *cb = b;
    if (ca->score != cb->score) return cb->score - ca->score;
    return strcmp(ca->path, cb->path);
}

/* The Roms folder tag: "Super Nintendo (SFC)" -> "SFC". */
static bool FolderTag(const char *name, char *out, size_t out_size) {
    const char *open = strrchr(name, '(');
    const char *close = open ? strchr(open, ')') : NULL;
    if (!close || close == open + 1 || (size_t)(close - open - 1) >= out_size) return false;
    memcpy(out, open + 1, (size_t)(close - open - 1));
    out[close - open - 1] = '\0';
    return true;
}

/* ── Emulators ───────────────────────────────────────────────────────────── */

/* Mirrors NextUI's getEmuPath: a pak in Emus/<platform>/ wins over the one
   bundled in .system. */
static void FindEmulator(const char *tag, char *out, size_t out_size) {
    out[0] = '\0';
    const char *sdcard = getenv("SDCARD_PATH");
    const char *platform = getenv("PLATFORM");
    const char *system = getenv("SYSTEM_PATH");
    char path[512];
    struct stat info;

    if (sdcard && platform) {
        snprintf(path, sizeof(path), "%s/Emus/%s/%s.pak/launch.sh", sdcard, platform, tag);
        if (stat(path, &info) == 0) { snprintf(out, out_size, "%s", path); return; }
    }
    if (system) {
        snprintf(path, sizeof(path), "%s/paks/Emus/%s.pak/launch.sh", system, tag);
        if (stat(path, &info) == 0) { snprintf(out, out_size, "%s", path); return; }
    }
}

/* ── Hashing ─────────────────────────────────────────────────────────────── */

/* Hashes a ROM the way minarch does when it identifies a game. A zipped
   cartridge ROM is unzipped first and the file inside hashed, since that is
   what minarch hands rcheevos. Arcade zips stay zipped: their cores load
   them directly, and RA identifies arcade games by file name anyway. */
bool Roms_HashFile(int console_id, const char *path, char hash[33]) {
    if (strcasecmp(Extension(path), "zip") == 0 && console_id != RC_CONSOLE_ARCADE) {
        size_t size = 0;
        char inner[256];
        uint8_t *data = RomZip_ExtractFirstRom(path, ROMS_MAX_UNZIPPED, &size, inner, sizeof(inner));
        if (!data) return false;
        bool ok = rc_hash_generate_from_buffer(hash, (uint32_t)console_id, data, size) != 0;
        free(data);
        return ok;
    }
    return rc_hash_generate_from_file(hash, (uint32_t)console_id, path) != 0;
}

/* ── Search ──────────────────────────────────────────────────────────────── */

bool Roms_TagForPath(const char *rom_path, char *tag, size_t tag_size) {
    char root[512];
    if (!Roms_RootPath(root, sizeof(root))) return false;
    size_t root_len = strlen(root);
    if (strncmp(rom_path, root, root_len) != 0 || rom_path[root_len] != '/') return false;

    const char *rel = rom_path + root_len + 1;
    char top[256];
    snprintf(top, sizeof(top), "%.*s", (int)strcspn(rel, "/"), rel);
    return FolderTag(top, tag, tag_size);
}

bool Roms_RootPath(char *out, size_t out_size) {
    const char *roms = getenv("ROMS_PATH");
    const char *sdcard = getenv("SDCARD_PATH");
    const char *home = getenv("HOME");
    int written;
    if (roms && roms[0]) written = snprintf(out, out_size, "%s", roms);
    else if (sdcard && sdcard[0]) written = snprintf(out, out_size, "%s/Roms", sdcard);
    else if (home && home[0]) written = snprintf(out, out_size, "%s/Roms", home);
    else return false;
    return written > 0 && (size_t)written < out_size;
}

Roms_Result Roms_Find(int game_id, int console_id, const char *title, Roms_Matches *matches,
                      float *progress, const int *cancel) {
    memset(matches, 0, sizeof(*matches));
    if (progress) *progress = 0.0f;
    if (console_id == RC_CONSOLE_UNKNOWN || game_id <= 0) return ROMS_UNKNOWN_CONSOLE;

    char root[512];
    if (!Roms_RootPath(root, sizeof(root))) return ROMS_NO_FOLDER;

    /* Folders whose tag maps to this console, or to the cartridge console a
       CD game is filed under. */
    CandidateList list = { 0 };
    bool any_folder = false;
    DIR *d = opendir(root);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char tag[32];
            if (e->d_name[0] == '.' || !FolderTag(e->d_name, tag, sizeof(tag))) continue;
            int folder_console = ConsoleForTag(tag);
            if (folder_console == RC_CONSOLE_UNKNOWN) continue;
            if (folder_console != console_id && CdVariant(folder_console) != console_id) continue;

            char dir[512];
            if (snprintf(dir, sizeof(dir), "%s/%s", root, e->d_name) >= (int)sizeof(dir)) continue;
            any_folder = true;
            int before = list.count;
            Collect(&list, dir, folder_console, 1);
            fprintf(stderr, "find: %s: %d files\n", dir, list.count - before);
        }
        closedir(d);
    }
    if (!any_folder) return ROMS_NO_FOLDER;
    /* NextUI creates its folders empty, so this, not a missing folder, is
       the usual way to have none of a console's games. */
    if (list.count == 0) return ROMS_NO_GAMES;

    GameHashes game = { .count = 0 };
    bool have_list = LoadGameHashes(console_id, game_id, &game);
    if (CdVariant(console_id) != RC_CONSOLE_UNKNOWN) {
        have_list = LoadGameHashes(CdVariant(console_id), game_id, &game) || have_list;
    }

    fprintf(stderr, "find: game %d on console %d: RA lists %d hashes%s\n", game_id, console_id,
            game.count, have_list ? "" : " (no hash list available)");

    Roms_Result result = ROMS_NOT_FOUND;
    if (!have_list) {
        result = ROMS_NO_HASHES;
    } else if (game.count > 0 && list.count > 0) {
        char title_words[32][32];
        int title_count = Words(title, title_words, 32);
        for (int i = 0; i < list.count; i++) {
            list.items[i].score = Score(title_words, title_count, list.items[i].path);
        }
        qsort(list.items, (size_t)list.count, sizeof(list.items[0]), ByScore);

        HashCache cache;
        CacheLoad(&cache);
        rc_hash_init_default_cdreader();

        for (int i = 0; i < list.count; i++) {
            if (cancel && *cancel) { result = ROMS_CANCELLED; break; }
            if (progress) *progress = (float)i / (float)list.count;

            const Candidate *c = &list.items[i];
            int hash_console = HashConsoleFor(c->console_id, c->path);
            struct stat info;
            if (stat(c->path, &info) != 0) continue;

            char hash[33];
            const char *cached = CacheLookup(&cache, hash_console, (long long)info.st_size,
                                             (long long)info.st_mtime, c->path);
            if (cached) {
                snprintf(hash, sizeof(hash), "%s", cached);
            } else if (Roms_HashFile(hash_console, c->path, hash)) {
                CacheStore(&cache, hash_console, (long long)info.st_size,
                           (long long)info.st_mtime, hash, c->path);
            } else {
                fprintf(stderr, "find: could not hash %s\n", c->path);
                continue; /* unreadable or a format rcheevos cannot hash */
            }
            fprintf(stderr, "find: %s %s%s\n", hash, c->path, cached ? " (cached)" : "");

            /* Keep going: every copy is needed to choose between them. */
            if (HasHash(&game, hash) && matches->count < ROMS_MAX_MATCHES) {
                Roms_Match *match = &matches->items[matches->count++];
                snprintf(match->rom_path, sizeof(match->rom_path), "%s", c->path);
                char tag[32] = "";
                if (Roms_TagForPath(c->path, tag, sizeof(tag))) {
                    FindEmulator(tag, match->emu_launch, sizeof(match->emu_launch));
                }
                result = ROMS_FOUND;
            }
        }
        CacheFree(&cache);
    }

    for (int i = 0; i < list.count; i++) free(list.items[i].path);
    free(list.items);
    if (progress) *progress = 1.0f;
    return result;
}

/* ── Links ───────────────────────────────────────────────────────────────── */

/* links.tsv: one "<game id>\t<size>\t<mtime>\t<path>" line per RA game,
   for the ROM last launched for it. */
static bool LinksPath(char *out, size_t out_size) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL) || !Paths_MakeDirs(dir)) return false;
    int written = snprintf(out, out_size, "%s/links.tsv", dir);
    return written > 0 && (size_t)written < out_size;
}

bool Roms_FindLinked(int game_id, Roms_Match *match) {
    memset(match, 0, sizeof(*match));
    char path[512];
    if (game_id <= 0 || !LinksPath(path, sizeof(path))) return false;
    size_t len = 0;
    char *text = Io_ReadFile(path, &len);
    if (!text) return false;

    bool found = false;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line && !found; line = strtok_r(NULL, "\n", &save)) {
        int id;
        long long size, mtime;
        int consumed = 0;
        if (sscanf(line, "%d\t%lld\t%lld\t%n", &id, &size, &mtime, &consumed) != 3 || id != game_id ||
            consumed <= 0) {
            continue;
        }
        const char *rom = line + consumed;
        struct stat info;
        char tag[32];
        /* Still a file, unchanged, and still in a Roms folder NextUI knows. */
        if (stat(rom, &info) != 0 || !S_ISREG(info.st_mode) || (long long)info.st_size != size ||
            (long long)info.st_mtime != mtime || !Roms_TagForPath(rom, tag, sizeof(tag))) {
            break;
        }
        snprintf(match->rom_path, sizeof(match->rom_path), "%s", rom);
        FindEmulator(tag, match->emu_launch, sizeof(match->emu_launch));
        found = true;
    }
    free(text);
    return found;
}

bool Roms_SaveLink(int game_id, const char *rom_path) {
    char path[512];
    struct stat info;
    if (game_id <= 0 || !LinksPath(path, sizeof(path)) || stat(rom_path, &info) != 0 ||
        strchr(rom_path, '\n') || strchr(rom_path, '\t')) {
        return false;
    }

    /* Every other game's link, then this one. */
    size_t len = 0;
    char *old = Io_ReadFile(path, &len);
    size_t cap = len + strlen(rom_path) + 64;
    char *out = malloc(cap);
    if (!out) { free(old); return false; }
    size_t out_len = 0;
    if (old) {
        char *save = NULL;
        for (char *line = strtok_r(old, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (atoi(line) == game_id) continue;
            out_len += (size_t)snprintf(out + out_len, cap - out_len, "%s\n", line);
        }
    }
    out_len += (size_t)snprintf(out + out_len, cap - out_len, "%d\t%lld\t%lld\t%s\n", game_id,
                                (long long)info.st_size, (long long)info.st_mtime, rom_path);
    bool ok = Io_WriteFileAtomic(path, out, out_len);
    free(out);
    free(old);
    return ok;
}

int Roms_LinkCount(void) {
    char path[512];
    if (!LinksPath(path, sizeof(path))) return 0;
    size_t len = 0;
    char *text = Io_ReadFile(path, &len);
    if (!text) return 0;
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) count += atoi(line) > 0;
    free(text);
    return count;
}

int Roms_ClearLinks(void) {
    char path[512];
    if (!LinksPath(path, sizeof(path))) return -1;
    int count = Roms_LinkCount();
    if (Io_FileAge(path) < 0) return 0; /* nothing linked yet */
    return remove(path) == 0 ? count : -1;
}

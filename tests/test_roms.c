/* The ROM finder: console and game from a pick, hashing as minarch does,
   and the search's results. Every search runs against a fixture SD card
   with fixture RA hash lists, so nothing here touches the network. */

#include <sys/time.h>
#include <zlib.h>

#include "rc_consoles.h"

#include "check.h"
#include "roms.h"

/* ── Fixture helpers ─────────────────────────────────────────────────────── */

/* Writes the RA hash list the finder would download for a console, listing
   hashes[] as game_id. Written now, so it counts as fresh. */
static void HashList(int console_id, int game_id, const char *const *hashes, int count) {
    char json[4096] = "{\"Success\":true,\"MD5List\":{\"ffffffffffffffffffffffffffffffff\":1";
    for (int i = 0; i < count; i++) {
        char entry[64];
        snprintf(entry, sizeof(entry), ",\"%s\":%d", hashes[i], game_id);
        strcat(json, entry);
    }
    strcat(json, "}}");
    char rel[128];
    snprintf(rel, sizeof(rel), "sd/.userdata/shared/prgc-tracker/hashlib/%d.json", console_id);
    WriteText(rel, json);
}

/* A fake ROM of len bytes; out gets its RA hash for console_id. */
static void Rom(const char *rel, unsigned seed, size_t len, int console_id, char out[33]) {
    unsigned char *data = malloc(len);
    Filler(data, len, seed);
    WriteBytes(rel, data, len);
    free(data);
    if (out && !Roms_HashFile(console_id, At(rel), out)) out[0] = '\0';
}

static void Touch(const char *rel) {
    WriteBytes(rel, "", 0);
}

static void Le16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void Le32(unsigned char *p, unsigned long v) { Le16(p, (unsigned)(v & 0xFFFF)); Le16(p + 2, (unsigned)(v >> 16)); }

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t len;
    bool deflate;
} ZipEntry;

/* A zip of entries, stored or deflated, the way ROM sets are packed. */
static void Zip(const char *rel, const ZipEntry *entries, int count) {
    unsigned char *out = malloc(1 << 22), central[4096];
    size_t pos = 0, central_len = 0;
    for (int i = 0; i < count; i++) {
        const ZipEntry *e = &entries[i];
        unsigned long crc = crc32(0, e->data, (uInt)e->len);
        unsigned char *packed = (unsigned char *)e->data;
        size_t packed_len = e->len;
        if (e->deflate) {
            packed = malloc(compressBound((uLong)e->len) + 64);
            z_stream z = { 0 };
            deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            z.next_in = (Bytef *)e->data;
            z.avail_in = (uInt)e->len;
            z.next_out = packed;
            z.avail_out = (uInt)(compressBound((uLong)e->len) + 64);
            deflate(&z, Z_FINISH);
            packed_len = z.total_out;
            deflateEnd(&z);
        }
        size_t name_len = strlen(e->name);
        unsigned char *local = out + pos;
        memset(local, 0, 30);
        Le32(local, 0x04034b50); Le16(local + 4, 20); Le16(local + 8, e->deflate ? 8 : 0);
        Le32(local + 14, crc); Le32(local + 18, (unsigned long)packed_len); Le32(local + 22, (unsigned long)e->len);
        Le16(local + 26, (unsigned)name_len);
        memcpy(local + 30, e->name, name_len);
        memcpy(local + 30 + name_len, packed, packed_len);

        unsigned char *c = central + central_len;
        memset(c, 0, 46);
        Le32(c, 0x02014b50); Le16(c + 4, 20); Le16(c + 6, 20); Le16(c + 10, e->deflate ? 8 : 0);
        Le32(c + 16, crc); Le32(c + 20, (unsigned long)packed_len); Le32(c + 24, (unsigned long)e->len);
        Le16(c + 28, (unsigned)name_len); Le32(c + 42, (unsigned long)pos);
        memcpy(c + 46, e->name, name_len);
        central_len += 46 + name_len;

        pos += 30 + name_len + packed_len;
        if (e->deflate) free(packed);
    }
    memcpy(out + pos, central, central_len);
    unsigned char *eocd = out + pos + central_len;
    memset(eocd, 0, 22);
    Le32(eocd, 0x06054b50); Le16(eocd + 8, (unsigned)count); Le16(eocd + 10, (unsigned)count);
    Le32(eocd + 12, (unsigned long)central_len); Le32(eocd + 16, (unsigned long)pos);
    WriteBytes(rel, out, pos + central_len + 22);
    free(out);
}

static Roms_Matches g_matches;

static Roms_Result Find(int game_id, int console_id, const char *title) {
    return Roms_Find(game_id, console_id, title, &g_matches, NULL, NULL);
}

/* The match's path relative to the fixture SD card's Roms folder. */
static const char *MatchRel(int i) {
    const char *roms = At("sd/Roms/");
    const char *path = g_matches.items[i].rom_path;
    return strncmp(path, roms, strlen(roms)) == 0 ? path + strlen(roms) : path;
}

#define SNES RC_CONSOLE_SUPER_NINTENDO
#define SFC_DIR "sd/Roms/Super Nintendo Entertainment System (SFC)/"
#define GAME 1216

/* ── Pick → console and game ─────────────────────────────────────────────── */

static void console_from_club_labels(void) {
    char label[24];
    CHECK_INT(Roms_ConsoleFromTitle("Galaga (NES)", label, sizeof(label)), RC_CONSOLE_NINTENDO);
    CHECK_STR(label, "NES");
    CHECK_INT(Roms_ConsoleFromTitle("Super Ghouls 'n Ghosts (SNES)", label, sizeof(label)), SNES);
    CHECK_INT(Roms_ConsoleFromTitle("Neo Turf Masters (Arcade)", label, sizeof(label)), RC_CONSOLE_ARCADE);
    CHECK_INT(Roms_ConsoleFromTitle("R4: Ridge Racer Type 4 (PS1)", label, sizeof(label)), RC_CONSOLE_PLAYSTATION);
    CHECK_INT(Roms_ConsoleFromTitle("Super Mario Sunshine (GC)", label, sizeof(label)), RC_CONSOLE_GAMECUBE);
}

static void console_from_aliases_and_ra_names(void) {
    char label[24];
    CHECK_INT(Roms_ConsoleFromTitle("X (Genesis)", label, sizeof(label)), RC_CONSOLE_MEGA_DRIVE);
    CHECK_INT(Roms_ConsoleFromTitle("X (sfc)", label, sizeof(label)), SNES); /* any case */
    CHECK_INT(Roms_ConsoleFromTitle("X (3DS)", label, sizeof(label)), RC_CONSOLE_NINTENDO_3DS);
    CHECK_INT(Roms_ConsoleFromTitle("X (Sega Saturn)", label, sizeof(label)), RC_CONSOLE_SATURN);
    CHECK_INT(Roms_ConsoleFromTitle("X (Atari Jaguar CD)", label, sizeof(label)), RC_CONSOLE_ATARI_JAGUAR_CD);
}

static void console_unknown(void) {
    char label[24];
    CHECK_INT(Roms_ConsoleFromTitle("X (Zorblax 9000)", label, sizeof(label)), 0);
    CHECK_STR(label, "Zorblax 9000");
    CHECK_INT(Roms_ConsoleFromTitle("No label here", label, sizeof(label)), 0);
    CHECK_INT(Roms_ConsoleFromTitle("Only the end counts (SNES) Deluxe", label, sizeof(label)), 0);
    CHECK_INT(Roms_ConsoleFromTitle("RA hubs are not consoles (Hubs)", label, sizeof(label)), 0);
}

static void game_id_from_link(void) {
    CHECK_INT(Roms_GameIdFromUrl("https://retroachievements.org/game/13796"), 13796);
    CHECK_INT(Roms_GameIdFromUrl("https://retroachievements.org/game/1216?foo=1"), 1216);
    CHECK_INT(Roms_GameIdFromUrl("https://www.ign.com/wikis/paper-mario"), 0);
    CHECK_INT(Roms_GameIdFromUrl(""), 0);
}

static void console_support(void) {
    Fixture();
    CHECK(Roms_NextUIPlays(SNES));
    CHECK(!Roms_NextUIPlays(RC_CONSOLE_NINTENDO_64));
    CHECK_STR(Roms_NextUIFolder(RC_CONSOLE_NINTENDO), "Nintendo Entertainment System (FC)");
    CHECK(Roms_NextUIFolder(RC_CONSOLE_GAMECUBE) == NULL);

    /* Pak Store emulators depend on the device. */
    setenv("PLATFORM", "h700", 1);
    CHECK_STR(Roms_PakStoreEmulator(RC_CONSOLE_NINTENDO_64), "Nintendo 64");
    CHECK_STR(Roms_PakStoreEmulator(RC_CONSOLE_DREAMCAST), "Dreamcast");
    CHECK(Roms_PakStoreEmulator(RC_CONSOLE_PSP) == NULL);
    CHECK(Roms_PakStoreEmulator(RC_CONSOLE_GAMECUBE) == NULL);
    setenv("PLATFORM", "tg5040", 1);
    CHECK_STR(Roms_PakStoreEmulator(RC_CONSOLE_PSP), "PPSSPP");
    setenv("PLATFORM", "my355", 1);
    CHECK(Roms_PakStoreEmulator(RC_CONSOLE_DREAMCAST) == NULL);
}

static void tag_for_path(void) {
    Fixture();
    char tag[32];
    CHECK(Roms_TagForPath(At(SFC_DIR "a/b/x.sfc"), tag, sizeof(tag)));
    CHECK_STR(tag, "SFC");
    CHECK(!Roms_TagForPath(At("sd/Elsewhere (SFC)/x.sfc"), tag, sizeof(tag)));
}

/* ── Hashing as minarch does ─────────────────────────────────────────────── */

static void zipped_rom_hashes_like_the_rom_inside(void) {
    Fixture();
    unsigned char rom[65536];
    Filler(rom, sizeof(rom), 7);
    WriteBytes("plain.sfc", rom, sizeof(rom));
    ZipEntry readme_first[] = {
        { "readme.txt", (const unsigned char *)"hi", 2, false },
        { "Game (USA).sfc", rom, sizeof(rom), true },
    };
    Zip("deflated.zip", readme_first, 2);
    ZipEntry stored[] = { { "Game (USA).sfc", rom, sizeof(rom), false } };
    Zip("stored.zip", stored, 1);

    char plain[33], deflated[33], stored_hash[33];
    CHECK(Roms_HashFile(SNES, At("plain.sfc"), plain));
    CHECK(Roms_HashFile(SNES, At("deflated.zip"), deflated));
    CHECK(Roms_HashFile(SNES, At("stored.zip"), stored_hash));
    CHECK_STR(deflated, plain);
    CHECK_STR(stored_hash, plain);
}

static void headers_are_stripped(void) {
    Fixture();
    unsigned char rom[32768], headered[512 + 32768];
    Filler(rom, sizeof(rom), 3);
    memset(headered, 0, 512);
    memcpy(headered + 512, rom, sizeof(rom));
    WriteBytes("bare.sfc", rom, sizeof(rom));
    WriteBytes("copier.smc", headered, sizeof(headered));
    char bare[33], copier[33];
    CHECK(Roms_HashFile(SNES, At("bare.sfc"), bare));
    CHECK(Roms_HashFile(SNES, At("copier.smc"), copier));
    CHECK_STR(copier, bare);

    unsigned char ines[16 + 16384];
    memset(ines, 0, 16);
    memcpy(ines, "NES\x1a", 4);
    Filler(ines + 16, 16384, 5);
    WriteBytes("game.nes", ines, sizeof(ines));
    WriteBytes("prg.bin", ines + 16, 16384);
    char with_header[33], without[33];
    CHECK(Roms_HashFile(RC_CONSOLE_NINTENDO, At("game.nes"), with_header));
    CHECK(Roms_HashFile(RC_CONSOLE_NINTENDO, At("prg.bin"), without));
    CHECK_STR(with_header, without);
}

static void arcade_hashes_by_file_name(void) {
    Fixture();
    unsigned char junk[100];
    Filler(junk, sizeof(junk), 1);
    WriteBytes("turfmast.zip", junk, sizeof(junk));
    char hash[33];
    CHECK(Roms_HashFile(RC_CONSOLE_ARCADE, At("turfmast.zip"), hash));
    CHECK_STR(hash, "86a5e87842b12616c52f467f5693348f"); /* md5("turfmast"), RA's Neo Turf Masters */
}

/* ── The search ──────────────────────────────────────────────────────────── */

static void unknown_console_or_game(void) {
    Fixture();
    CHECK_INT(Find(GAME, 0, "X"), ROMS_UNKNOWN_CONSOLE);
    CHECK_INT(Find(0, SNES, "X"), ROMS_UNKNOWN_CONSOLE);
}

static void no_folder_for_the_console(void) {
    Fixture();
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NO_FOLDER); /* no Roms at all */
    MakeDirs(At("sd/Roms/Game Boy (GB)"));
    MakeDirs(At("sd/Roms/Not A Console Folder"));
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NO_FOLDER);
    /* A folder with an unknown tag is not this console's, even if named so. */
    MakeDirs(At("sd/Roms/GameCube (GC)"));
    CHECK_INT(Find(6049, RC_CONSOLE_GAMECUBE, "X (GC)"), ROMS_NO_FOLDER);
}

static void empty_folders_mean_no_games(void) {
    Fixture();
    MakeDirs(At(SFC_DIR));
    MakeDirs(At("sd/Roms/Super Nintendo Entertainment System (SUPA)"));
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NO_GAMES);
    /* Artwork, saves, notes and dotfiles are not games. */
    Touch(SFC_DIR "notes.txt");
    Touch(SFC_DIR "boxart.png");
    Touch(SFC_DIR "Game.srm");
    Touch(SFC_DIR ".DS_Store");
    Touch(SFC_DIR "._Game.sfc");
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NO_GAMES);
}

static void games_but_no_match(void) {
    Fixture();
    char other[33];
    Rom(SFC_DIR "Other Game (USA).sfc", 1, 4096, SNES, other);
    const char *list[] = { "0123456789abcdef0123456789abcdef" };
    HashList(SNES, GAME, list, 1);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NOT_FOUND);
    CHECK_INT(g_matches.count, 0);
    /* RA knowing no hashes for the game at all is the same answer. */
    HashList(SNES, GAME, NULL, 0);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NOT_FOUND);
}

static void offline_without_a_saved_hash_list(void) {
    Fixture();
    Rom(SFC_DIR "Game (USA).sfc", 1, 4096, SNES, NULL);
    /* Nothing listening here: the download fails fast. */
    setenv("https_proxy", "http://127.0.0.1:9", 1);
    setenv("HTTPS_PROXY", "http://127.0.0.1:9", 1);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_NO_HASHES);
    unsetenv("https_proxy");
    unsetenv("HTTPS_PROXY");
}

static void found_with_its_emulator(void) {
    Fixture();
    char hash[33];
    Rom(SFC_DIR "Super Ghouls 'N Ghosts (USA).sfc", 1, 8192, SNES, hash);
    Rom(SFC_DIR "Other Game (USA).sfc", 2, 8192, SNES, NULL);
    const char *list[] = { hash };
    HashList(SNES, GAME, list, 1);

    CHECK_INT(Find(GAME, SNES, "Super Ghouls 'n Ghosts (SNES)"), ROMS_FOUND);
    CHECK_INT(g_matches.count, 1);
    CHECK_STR(MatchRel(0), "Super Nintendo Entertainment System (SFC)/Super Ghouls 'N Ghosts (USA).sfc");
    CHECK_STR(g_matches.items[0].emu_launch, ""); /* no emulator installed */

    /* NextUI's bundled pak, then a user's own pak, which wins. */
    Touch("sd/.system/h700/paks/Emus/SFC.pak/launch.sh");
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
    CHECK_STR(g_matches.items[0].emu_launch, At("sd/.system/h700/paks/Emus/SFC.pak/launch.sh"));
    Touch("sd/Emus/h700/SFC.pak/launch.sh");
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
    CHECK_STR(g_matches.items[0].emu_launch, At("sd/Emus/h700/SFC.pak/launch.sh"));
}

static void every_copy_is_returned_best_named_first(void) {
    Fixture();
    char hash[33];
    Rom(SFC_DIR "zzz renamed.sfc", 1, 8192, SNES, hash);
    Rom(SFC_DIR "Super Ghouls 'N Ghosts (USA).sfc", 1, 8192, SNES, NULL); /* same bytes */
    const char *list[] = { hash };
    HashList(SNES, GAME, list, 1);
    CHECK_INT(Find(GAME, SNES, "Super Ghouls 'n Ghosts (SNES)"), ROMS_FOUND);
    CHECK_INT(g_matches.count, 2);
    CHECK_STR(MatchRel(0), "Super Nintendo Entertainment System (SFC)/Super Ghouls 'N Ghosts (USA).sfc");
    CHECK_STR(MatchRel(1), "Super Nintendo Entertainment System (SFC)/zzz renamed.sfc");
}

static void variants_of_one_game_all_match(void) {
    Fixture();
    char plain[33], msu1[33];
    Rom(SFC_DIR "Super Ghouls 'N Ghosts (USA).sfc", 1, 8192, SNES, plain);
    Rom(SFC_DIR "MSU-1/Super Ghouls N Ghosts (USA) (MSU-1).sfc", 2, 8192, SNES, msu1);
    const char *list[] = { plain, msu1 };
    HashList(SNES, GAME, list, 2);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
    CHECK_INT(g_matches.count, 2);
}

static void all_of_a_consoles_folders_are_searched(void) {
    Fixture();
    char hash[33];
    Rom("sd/Roms/Super Nintendo Entertainment System (SUPA)/Game.sfc", 1, 8192, SNES, hash);
    MakeDirs(At(SFC_DIR));
    const char *list[] = { hash };
    HashList(SNES, GAME, list, 1);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
    CHECK_STR(MatchRel(0), "Super Nintendo Entertainment System (SUPA)/Game.sfc");
}

static void deep_subfolders_are_searched(void) {
    Fixture();
    char hash[33];
    Rom(SFC_DIR "a/b/c/d/e/Game.sfc", 1, 8192, SNES, hash);
    const char *list[] = { hash };
    HashList(SNES, GAME, list, 1);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
}

static void zipped_rom_is_found(void) {
    Fixture();
    unsigned char rom[16384];
    Filler(rom, sizeof(rom), 9);
    WriteBytes("loose.sfc", rom, sizeof(rom));
    char hash[33];
    CHECK(Roms_HashFile(SNES, At("loose.sfc"), hash));
    ZipEntry entry[] = { { "Super Ghouls 'N Ghosts (USA).sfc", rom, sizeof(rom), true } };
    Zip(SFC_DIR "Super Ghouls 'N Ghosts (USA).zip", entry, 1);
    const char *list[] = { hash };
    HashList(SNES, GAME, list, 1);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
    CHECK_STR(MatchRel(0), "Super Nintendo Entertainment System (SFC)/Super Ghouls 'N Ghosts (USA).zip");
}

static void bin_tracks_beside_a_cue_are_skipped(void) {
    Fixture();
    /* Genesis cartridges are .bin files too, and Sega CD games share the MD
       folder, so the same .bin is a ROM alone and a CD track beside a cue. */
    const int MD = RC_CONSOLE_MEGA_DRIVE, GAME_MD = 1;
    char hash[33];
    Rom("sd/Roms/Sega Genesis (MD)/Game/Game.bin", 1, 4096, MD, hash);
    const char *list[] = { hash };
    HashList(MD, GAME_MD, list, 1);
    HashList(RC_CONSOLE_SEGA_CD, GAME_MD, NULL, 0);
    CHECK_INT(Find(GAME_MD, MD, "X (MD)"), ROMS_FOUND);
    WriteText("sd/Roms/Sega Genesis (MD)/Game/Game.cue", "FILE \"Game.bin\" BINARY\n");
    CHECK_INT(Find(GAME_MD, MD, "X (MD)"), ROMS_NOT_FOUND); /* the cue owns it now */
}

static void cd_games_in_the_cartridge_folder(void) {
    Fixture();
    /* NextUI files PC Engine CD games under PCE, so a PCE CD pick looks there. */
    Rom("sd/Roms/TurboGrafx-16 (PCE)/Some HuCard.pce", 1, 4096, RC_CONSOLE_PC_ENGINE, NULL);
    const char *none[] = { "0123456789abcdef0123456789abcdef" };
    HashList(RC_CONSOLE_PC_ENGINE_CD, 99, none, 1);
    HashList(RC_CONSOLE_PC_ENGINE, 99, none, 1);
    CHECK_INT(Find(99, RC_CONSOLE_PC_ENGINE_CD, "X (PCECD)"), ROMS_NOT_FOUND);
}

static void arcade_found_by_name(void) {
    Fixture();
    Touch("sd/Roms/Arcade (FBN)/turfmast.zip");
    const char *list[] = { "86a5e87842b12616c52f467f5693348f" };
    HashList(RC_CONSOLE_ARCADE, 13796, list, 1);
    CHECK_INT(Find(13796, RC_CONSOLE_ARCADE, "Neo Turf Masters (Arcade)"), ROMS_FOUND);
}

static void hashes_are_cached(void) {
    Fixture();
    char hash[33];
    Rom(SFC_DIR "Game.sfc", 1, 8192, SNES, hash);
    const char *list[] = { hash };
    HashList(SNES, GAME, list, 1);
    CHECK_INT(Find(GAME, SNES, "X (SNES)"), ROMS_FOUND);
    const char *cache = ReadText("sd/.userdata/shared/prgc-tracker/hashes-v2.tsv");
    CHECK(cache && strstr(cache, hash) && strstr(cache, "Game.sfc"));
}

/* ── Saved links ─────────────────────────────────────────────────────────── */

static void a_saved_link_skips_the_search(void) {
    Fixture();
    Rom(SFC_DIR "Super Ghouls 'N Ghosts (USA).sfc", 1, 8192, SNES, NULL);
    Touch("sd/.system/h700/paks/Emus/SFC.pak/launch.sh");
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "Super Ghouls 'N Ghosts (USA).sfc")));

    /* No hash list and no network: a search could not work, the link does. */
    setenv("https_proxy", "http://127.0.0.1:9", 1);
    setenv("HTTPS_PROXY", "http://127.0.0.1:9", 1);
    Roms_Match match;
    CHECK(Roms_FindLinked(GAME, &match));
    CHECK_STR(match.rom_path, At(SFC_DIR "Super Ghouls 'N Ghosts (USA).sfc"));
    CHECK_STR(match.emu_launch, At("sd/.system/h700/paks/Emus/SFC.pak/launch.sh"));
    unsetenv("https_proxy");
    unsetenv("HTTPS_PROXY");

    CHECK(!Roms_FindLinked(GAME + 1, &match)); /* other games have no link */
}

static void a_link_to_a_changed_file_is_dropped(void) {
    Fixture();
    Rom(SFC_DIR "Game.sfc", 1, 8192, SNES, NULL);
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "Game.sfc")));
    Roms_Match match;
    CHECK(Roms_FindLinked(GAME, &match));

    Rom(SFC_DIR "Game.sfc", 2, 4096, SNES, NULL); /* replaced by another file */
    CHECK(!Roms_FindLinked(GAME, &match));

    /* Same size, but touched since: not trusted either. */
    Rom(SFC_DIR "Other.sfc", 3, 8192, SNES, NULL);
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "Other.sfc")));
    struct timeval times[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
    utimes(At(SFC_DIR "Other.sfc"), times);
    CHECK(!Roms_FindLinked(GAME, &match));
}

static void a_link_to_a_moved_or_deleted_file_is_dropped(void) {
    Fixture();
    Rom(SFC_DIR "Game.sfc", 1, 8192, SNES, NULL);
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "Game.sfc")));
    rename(At(SFC_DIR "Game.sfc"), At(SFC_DIR "Moved.sfc"));
    Roms_Match match;
    CHECK(!Roms_FindLinked(GAME, &match));
    remove(At(SFC_DIR "Moved.sfc"));
    CHECK(!Roms_FindLinked(GAME, &match));
}

static void saving_replaces_only_that_games_link(void) {
    Fixture();
    Rom(SFC_DIR "A.sfc", 1, 4096, SNES, NULL);
    Rom(SFC_DIR "B.sfc", 2, 4096, SNES, NULL);
    Rom(SFC_DIR "C.sfc", 3, 4096, SNES, NULL);
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "A.sfc")));
    CHECK(Roms_SaveLink(12, At(SFC_DIR "C.sfc")));
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "B.sfc"))); /* the player picked another copy */
    Roms_Match match;
    CHECK(Roms_FindLinked(GAME, &match));
    CHECK_STR(match.rom_path, At(SFC_DIR "B.sfc"));
    CHECK(Roms_FindLinked(12, &match));
    CHECK_STR(match.rom_path, At(SFC_DIR "C.sfc"));
}

static void unlinking_forgets_every_game(void) {
    Fixture();
    CHECK_INT(Roms_ClearLinks(), 0); /* nothing linked yet */
    CHECK_INT(Roms_LinkCount(), 0);
    Rom(SFC_DIR "A.sfc", 1, 4096, SNES, NULL);
    Rom(SFC_DIR "B.sfc", 2, 4096, SNES, NULL);
    CHECK(Roms_SaveLink(GAME, At(SFC_DIR "A.sfc")));
    CHECK(Roms_SaveLink(12, At(SFC_DIR "B.sfc")));
    CHECK_INT(Roms_LinkCount(), 2);
    CHECK_INT(Roms_ClearLinks(), 2);
    CHECK_INT(Roms_LinkCount(), 0);
    Roms_Match match;
    CHECK(!Roms_FindLinked(GAME, &match));
    CHECK(!Roms_FindLinked(12, &match));
    CHECK_INT(Roms_ClearLinks(), 0);
}

int main(void) {
    RUN(console_from_club_labels);
    RUN(console_from_aliases_and_ra_names);
    RUN(console_unknown);
    RUN(game_id_from_link);
    RUN(console_support);
    RUN(tag_for_path);
    RUN(zipped_rom_hashes_like_the_rom_inside);
    RUN(headers_are_stripped);
    RUN(arcade_hashes_by_file_name);
    RUN(unknown_console_or_game);
    RUN(no_folder_for_the_console);
    RUN(empty_folders_mean_no_games);
    RUN(games_but_no_match);
    RUN(offline_without_a_saved_hash_list);
    RUN(found_with_its_emulator);
    RUN(every_copy_is_returned_best_named_first);
    RUN(variants_of_one_game_all_match);
    RUN(all_of_a_consoles_folders_are_searched);
    RUN(deep_subfolders_are_searched);
    RUN(zipped_rom_is_found);
    RUN(bin_tracks_beside_a_cue_are_skipped);
    RUN(cd_games_in_the_cartridge_folder);
    RUN(arcade_found_by_name);
    RUN(hashes_are_cached);
    RUN(a_saved_link_skips_the_search);
    RUN(a_link_to_a_changed_file_is_dropped);
    RUN(a_link_to_a_moved_or_deleted_file_is_dropped);
    RUN(saving_replaces_only_that_games_link);
    RUN(unlinking_forgets_every_game);
    CHECK_DONE();
}

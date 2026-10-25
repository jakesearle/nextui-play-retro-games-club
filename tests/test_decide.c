/* The rules behind Play: which copy of a game to launch, and what a search
   result means for the player. */

#include "rc_consoles.h"

#include "check.h"
#include "decide.h"

static int g_ask[ROMS_MAX_MATCHES], g_ask_count;

static int Choose(const Decide_CopyInfo *copies, int count) {
    return Decide_Copy(copies, count, g_ask, &g_ask_count);
}

/* ── Which copy ──────────────────────────────────────────────────────────── */

static void one_copy_is_simply_played(void) {
    Decide_CopyInfo copies[] = { { false, -1 } };
    CHECK_INT(Choose(copies, 1), 0);
    CHECK_INT(g_ask_count, 0);
}

static void the_copy_with_a_save_state_wins(void) {
    Decide_CopyInfo copies[] = { { false, 0 }, { true, -1 }, { false, 1 } };
    /* Even over one played more recently. */
    CHECK_INT(Choose(copies, 3), 1);
}

static void then_the_most_recently_played(void) {
    Decide_CopyInfo none_saved[] = { { false, 5 }, { false, -1 }, { false, 2 } };
    CHECK_INT(Choose(none_saved, 3), 2);

    /* Among copies with save states, recency decides; the unsaved copy is
       out even though it was played most recently of all. */
    Decide_CopyInfo two_saved[] = { { true, 7 }, { false, 0 }, { true, 3 } };
    CHECK_INT(Choose(two_saved, 3), 2);
}

static void otherwise_the_player_is_asked(void) {
    Decide_CopyInfo copies[] = { { false, -1 }, { false, -1 } };
    CHECK_INT(Choose(copies, 2), -1);
    CHECK_INT(g_ask_count, 2);
    CHECK_INT(g_ask[0], 0);
    CHECK_INT(g_ask[1], 1);
}

static void only_the_saved_copies_are_offered(void) {
    Decide_CopyInfo copies[] = { { true, -1 }, { false, -1 }, { true, -1 } };
    CHECK_INT(Choose(copies, 3), -1);
    CHECK_INT(g_ask_count, 2);
    CHECK_INT(g_ask[0], 0);
    CHECK_INT(g_ask[1], 2);
}

static void no_copies(void) {
    CHECK_INT(Choose(NULL, 0), -1);
    CHECK_INT(g_ask_count, 0);
}

/* ── What a result means ─────────────────────────────────────────────────── */

static void found_launches_only_with_an_emulator(void) {
    CHECK_INT(Decide_ForResult(ROMS_FOUND, RC_CONSOLE_SUPER_NINTENDO, true), DECIDE_LAUNCH);
    CHECK_INT(Decide_ForResult(ROMS_FOUND, RC_CONSOLE_NINTENDO_64, false), DECIDE_NO_EMULATOR);
}

static void searches_that_found_nothing(void) {
    CHECK_INT(Decide_ForResult(ROMS_NOT_FOUND, RC_CONSOLE_NINTENDO, false), DECIDE_NO_MATCH);
    CHECK_INT(Decide_ForResult(ROMS_NO_GAMES, RC_CONSOLE_NINTENDO, false), DECIDE_NO_GAMES);
    CHECK_INT(Decide_ForResult(ROMS_NO_HASHES, RC_CONSOLE_NINTENDO, false), DECIDE_NO_HASH_LIST);
    CHECK_INT(Decide_ForResult(ROMS_CANCELLED, RC_CONSOLE_NINTENDO, false), DECIDE_SILENT);
    CHECK_INT(Decide_ForResult(ROMS_UNKNOWN_CONSOLE, 0, false), DECIDE_SILENT);
}

static void a_missing_folder_depends_on_the_console(void) {
    Fixture();
    setenv("PLATFORM", "h700", 1);
    /* NextUI made a folder for it, so it was removed or renamed. */
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_NINTENDO, false), DECIDE_MISSING_FOLDER);
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_ARCADE, false), DECIDE_MISSING_FOLDER);
    /* No folder because no emulator pak; the Pak Store has one. */
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_NINTENDO_64, false), DECIDE_GET_EMULATOR);
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_DREAMCAST, false), DECIDE_GET_EMULATOR);
    /* No emulator for this device anywhere. */
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_GAMECUBE, false), DECIDE_CANT_PLAY);
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_PLAYSTATION_2, false), DECIDE_CANT_PLAY);
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_WII, false), DECIDE_CANT_PLAY);
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_PSP, false), DECIDE_CANT_PLAY);
    /* PPSSPP is built for TrimUI devices. */
    setenv("PLATFORM", "tg5040", 1);
    CHECK_INT(Decide_ForResult(ROMS_NO_FOLDER, RC_CONSOLE_PSP, false), DECIDE_GET_EMULATOR);
}

int main(void) {
    RUN(one_copy_is_simply_played);
    RUN(the_copy_with_a_save_state_wins);
    RUN(then_the_most_recently_played);
    RUN(otherwise_the_player_is_asked);
    RUN(only_the_saved_copies_are_offered);
    RUN(no_copies);
    RUN(found_launches_only_with_an_emulator);
    RUN(searches_that_found_nothing);
    RUN(a_missing_folder_depends_on_the_console);
    CHECK_DONE();
}

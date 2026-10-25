/* Handing a game to launch.sh the way NextUI's menu would: the save slot to
   resume, Recently Played, and the hand-off file. */

#include "check.h"
#include "launch.h"

#define RECENTS "sd/.userdata/shared/.minui/recent.txt"
#define SFC_ROM "sd/Roms/Super Nintendo (SFC)/Game (USA).zip"
#define SLOT_FILE "sd/.userdata/shared/.minui/SFC/Game (USA).zip.txt"

/* ── Resume ──────────────────────────────────────────────────────────────── */

static void no_save_state(void) {
    Fixture();
    WriteText(SFC_ROM, "");
    Launch_Resume state;
    CHECK(!Launch_FindResume(At(SFC_ROM), &state));
}

static void the_last_slot_used(void) {
    Fixture();
    WriteText(SFC_ROM, "");
    WriteText(SLOT_FILE, "3");
    Launch_Resume state;
    CHECK(Launch_FindResume(At(SFC_ROM), &state));
    CHECK_INT(state.slot, 3);
    WriteText(SLOT_FILE, "0\n");
    CHECK(Launch_FindResume(At(SFC_ROM), &state));
    CHECK_INT(state.slot, 0);
}

static void slots_outside_the_menus_are_ignored(void) {
    Fixture();
    WriteText(SFC_ROM, "");
    Launch_Resume state;
    /* 8 is the default-state slot and 9 the sleep auto-save, neither of
       which the player chose; anything unreadable counts as no save. */
    const char *bad[] = { "8", "9", "-1", "", "x" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        WriteText(SLOT_FILE, bad[i]);
        CHECK(!Launch_FindResume(At(SFC_ROM), &state));
    }
}

static void the_record_is_by_folder_tag_and_file(void) {
    Fixture();
    /* A ROM in a subfolder is still recorded under its top folder's tag. */
    const char *rom = "sd/Roms/Super Nintendo (SFC)/Capcom/Game (USA).zip";
    WriteText(rom, "");
    WriteText(SLOT_FILE, "2");
    Launch_Resume state;
    CHECK(Launch_FindResume(At(rom), &state));
    CHECK_INT(state.slot, 2);
}

/* ── Recently Played ─────────────────────────────────────────────────────── */

static void a_new_game_goes_on_top(void) {
    Fixture();
    WriteText(RECENTS, "/Roms/A (GB)/a.gb\n");
    CHECK(Launch_AddRecent(At(SFC_ROM)));
    CHECK_STR(ReadText(RECENTS), "/Roms/Super Nintendo (SFC)/Game (USA).zip\n/Roms/A (GB)/a.gb\n");
    CHECK_INT(Launch_RecentRank(At(SFC_ROM)), 0);
    CHECK_INT(Launch_RecentRank(At("sd/Roms/A (GB)/a.gb")), 1);
    CHECK_INT(Launch_RecentRank(At("sd/Roms/A (GB)/never.gb")), -1);
}

static void a_replayed_game_moves_up_with_its_alias(void) {
    Fixture();
    WriteText(RECENTS, "/Roms/A (GB)/a.gb\n/Roms/Super Nintendo (SFC)/Game (USA).zip\tMy Game\n/Roms/C (FC)/c.nes\n");
    CHECK(Launch_AddRecent(At(SFC_ROM)));
    CHECK_STR(ReadText(RECENTS),
              "/Roms/Super Nintendo (SFC)/Game (USA).zip\tMy Game\n/Roms/A (GB)/a.gb\n/Roms/C (FC)/c.nes\n");
}

static void recents_stop_at_nextuis_limit(void) {
    Fixture();
    char many[4096] = "";
    for (int i = 0; i < 30; i++) {
        char line[64];
        snprintf(line, sizeof(line), "/Roms/X (FC)/%d.nes\n", i);
        strcat(many, line);
    }
    WriteText(RECENTS, many);
    CHECK(Launch_AddRecent(At(SFC_ROM)));
    const char *text = ReadText(RECENTS);
    int lines = 0;
    for (const char *p = text; *p; p++) lines += *p == '\n';
    CHECK_INT(lines, 24);
    CHECK(strncmp(text, "/Roms/Super Nintendo (SFC)/Game (USA).zip\n", 42) == 0);
    CHECK(strstr(text, "/Roms/X (FC)/22.nes\n") != NULL);
    CHECK(strstr(text, "/Roms/X (FC)/23.nes\n") == NULL);
}

static void roms_off_the_sd_card_are_not_listed(void) {
    Fixture();
    CHECK(!Launch_AddRecent("/elsewhere/Game.zip"));
    CHECK_INT(Launch_RecentRank("/elsewhere/Game.zip"), -1);
}

/* ── Hand-off ────────────────────────────────────────────────────────────── */

static void the_hand_off_names_emulator_rom_and_slot(void) {
    Fixture();
    WriteText(SFC_ROM, "");
    Roms_Match match;
    snprintf(match.emu_launch, sizeof(match.emu_launch), "%s", At("sd/Emus/h700/SFC.pak/launch.sh"));
    snprintf(match.rom_path, sizeof(match.rom_path), "%s", At(SFC_ROM));
    remove(LAUNCH_FILE);
    CHECK(Launch_HandOff(&match, 3));

    FILE *f = fopen(LAUNCH_FILE, "r");
    CHECK(f != NULL);
    if (f) {
        char text[2048] = "";
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
        char expected[2048];
        snprintf(expected, sizeof(expected), "%s\n%s\n3\n", match.emu_launch, match.rom_path);
        CHECK_STR(text, expected);
    }
    remove(LAUNCH_FILE);
    CHECK_INT(Launch_RecentRank(At(SFC_ROM)), 0);
}

int main(void) {
    RUN(no_save_state);
    RUN(the_last_slot_used);
    RUN(slots_outside_the_menus_are_ignored);
    RUN(the_record_is_by_folder_tag_and_file);
    RUN(a_new_game_goes_on_top);
    RUN(a_replayed_game_moves_up_with_its_alias);
    RUN(recents_stop_at_nextuis_limit);
    RUN(roms_off_the_sd_card_are_not_listed);
    RUN(the_hand_off_names_emulator_rom_and_slot);
    CHECK_DONE();
}

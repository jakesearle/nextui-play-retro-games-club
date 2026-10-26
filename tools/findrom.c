/* Runs the real ROM finder without the UI, against the current club picks:

     ROMS_PATH=~/Roms task findrom                       # every current pick
     task findrom ARGS="--hash 3 'Roms/SNES (SFC)/x.sfc'"  # one file's RA hash

   ROMS_PATH points at a folder laid out like an SD card's Roms/, with
   "Console Name (TAG)" subfolders. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "rc_hash.h"

#include "club.h"
#include "roms.h"

static const char *ResultName(Roms_Result result) {
    switch (result) {
    case ROMS_FOUND: return "found";
    case ROMS_NOT_FOUND: return "not found";
    case ROMS_UNKNOWN_CONSOLE: return "unknown console";
    case ROMS_NO_FOLDER: return "no Roms folder for this console";
    case ROMS_NO_GAMES: return "folder exists but holds no games";
    case ROMS_NO_HASHES: return "no RA hash list (offline?)";
    case ROMS_CANCELLED: return "cancelled";
    }
    return "?";
}

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (argc == 4 && strcmp(argv[1], "--hash") == 0) {
        char hash[33];
        rc_hash_init_default_cdreader();
        bool ok = Roms_HashFile(atoi(argv[2]), argv[3], hash);
        printf("%s\n", ok ? hash : "(could not hash)");
        return ok ? 0 : 1;
    }

    static Club_Data data;
    if (Club_Load(&data, CLUB_LOAD_AUTO) != CLUB_OK) {
        fprintf(stderr, "could not load the club page\n");
        return 1;
    }

    char root[512] = "";
    Roms_RootPath(root, sizeof(root));
    printf("Roms: %s\n", root);

    for (int i = 0; i < data.pick_count; i++) {
        const Club_Pick *pick = &data.picks[i];
        char label[24];
        int console_id = Roms_ConsoleFromTitle(pick->title, label, sizeof(label));
        int game_id = Roms_GameIdFromUrl(pick->ra_url);

        static Roms_Matches matches;
        Roms_Result result = Roms_Find(game_id, console_id, pick->title, &matches, NULL, NULL);
        printf("\n%s\n  console %s = %d, game %d: %s\n", pick->title, label, console_id, game_id,
               ResultName(result));
        for (int m = 0; m < matches.count; m++) {
            printf("  rom: %s\n  emulator: %s\n", matches.items[m].rom_path,
                   matches.items[m].emu_launch[0] ? matches.items[m].emu_launch : "(none installed)");
        }
    }

    curl_global_cleanup();
    return 0;
}

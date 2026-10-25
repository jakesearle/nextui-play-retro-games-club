#include "decide.h"

int Decide_Copy(const Decide_CopyInfo *copies, int count, int *ask, int *ask_count) {
    *ask_count = 0;
    if (count <= 0) return -1;

    int saved = 0;
    for (int i = 0; i < count; i++) saved += copies[i].has_save_state;

    /* With a save state somewhere, only those copies are in the running. */
    int left = 0;
    for (int i = 0; i < count; i++) {
        if (saved == 0 || copies[i].has_save_state) ask[left++] = i;
    }
    if (left == 1) return ask[0];

    int best = -1;
    for (int i = 0; i < left; i++) {
        int rank = copies[ask[i]].recent_rank;
        if (rank >= 0 && (best < 0 || rank < copies[best].recent_rank)) best = ask[i];
    }
    if (best >= 0) return best;

    *ask_count = left;
    return -1;
}

Decide_Outcome Decide_ForResult(Roms_Result result, int console_id, bool has_emulator) {
    switch (result) {
    case ROMS_FOUND:
        return has_emulator ? DECIDE_LAUNCH : DECIDE_NO_EMULATOR;
    case ROMS_CANCELLED:
    case ROMS_UNKNOWN_CONSOLE:
        return DECIDE_SILENT;
    case ROMS_NOT_FOUND:
        return DECIDE_NO_MATCH;
    case ROMS_NO_GAMES:
        return DECIDE_NO_GAMES;
    case ROMS_NO_HASHES:
        return DECIDE_NO_HASH_LIST;
    case ROMS_NO_FOLDER:
        /* NextUI creates a folder for every console it plays, so for those
           a missing one was removed or renamed. For the rest, no folder
           means no emulator pak: point to one, or say there is none. */
        if (Roms_NextUIFolder(console_id)) return DECIDE_MISSING_FOLDER;
        if (Roms_PakStoreEmulator(console_id)) return DECIDE_GET_EMULATOR;
        if (Roms_NextUIPlays(console_id)) return DECIDE_NO_FOLDER;
        return DECIDE_CANT_PLAY;
    }
    return DECIDE_SILENT;
}

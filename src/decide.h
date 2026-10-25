#ifndef DECIDE_H
#define DECIDE_H

#include <stdbool.h>

#include "roms.h"

/* The rules behind Play, kept apart from the screens so they can be tested:
   which copy of a game to launch, and what to tell the player when there is
   nothing to launch. The screens turn these answers into words. */

/* What is known about one copy of the game when choosing between them. */
typedef struct {
    bool has_save_state;
    int recent_rank; /* 0 = most recently played, -1 = not in Recently Played */
} Decide_CopyInfo;

/* Picks a copy: the one with a save state, else the one played most
   recently, else nobody can tell. Only the copies left after each step go
   on to the next. Returns the chosen index, or -1 when the player has to
   be asked, in which case ask[] holds the indices to offer (in their
   original order) and *ask_count how many. */
int Decide_Copy(const Decide_CopyInfo *copies, int count, int *ask, int *ask_count);

typedef enum {
    DECIDE_LAUNCH = 0,          /* Found, with an emulator: go */
    DECIDE_SILENT,              /* Cancelled; say nothing */
    DECIDE_NO_MATCH,            /* Has games for the console, none is this one */
    DECIDE_NO_GAMES,            /* The console's folders are empty */
    DECIDE_MISSING_FOLDER,      /* NextUI's own folder for it is gone */
    DECIDE_GET_EMULATOR,        /* No folder; the Pak Store has an emulator */
    DECIDE_NO_FOLDER,           /* No folder, NextUI plays it, no store pak */
    DECIDE_CANT_PLAY,           /* No emulator for the console anywhere */
    DECIDE_NO_HASH_LIST,        /* Offline, and RA's list was never saved */
    DECIDE_NO_EMULATOR,         /* Found the game, but its emulator is missing */
} Decide_Outcome;

/* What a search result means for the player. has_emulator is whether the
   chosen copy's emulator pak is installed, and only matters when found. */
Decide_Outcome Decide_ForResult(Roms_Result result, int console_id, bool has_emulator);

#endif

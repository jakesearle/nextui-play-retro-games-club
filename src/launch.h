#ifndef LAUNCH_H
#define LAUNCH_H

#include <stdbool.h>

#include "roms.h"

/* Read by launch.sh after the app exits: the emulator pak's launch.sh, the
   ROM and the state slot to load, one per line. NextUI's own /tmp/next is deleted as soon as a pak
   returns, so launch.sh starts the game itself. */
#define LAUNCH_FILE "/tmp/prgc-tracker-launch"

/* The title of the pick being played, so the app can reselect it when
   launch.sh reopens it after the game. */
#define LAUNCH_SELECTION_FILE "/tmp/prgc-tracker-selection"

/* minarch's slot for "start fresh": NextUI's menu asks for it on a plain
   launch. It is a MinUI default-state slot that normally does not exist,
   so the game boots from the start. */
#define LAUNCH_FRESH_SLOT 8

/* The save state NextUI's menu would offer to resume (its X button): the
   last slot the player used for this ROM, and the screenshot minarch keeps
   beside it. */
typedef struct {
    int slot;
    char preview[1024]; /* empty when there is none */
} Launch_Resume;

/* Whether the ROM has a save state to resume, read from where minarch
   records it: .userdata/shared/.minui/<TAG>/<rom file>.txt. */
bool Launch_FindResume(const char *rom_path, Launch_Resume *resume);

/* Hands a found game to launch.sh, to be loaded from state slot
   resume_slot (LAUNCH_FRESH_SLOT to start fresh), and adds it to NextUI's
   Recently Played as opening it from NextUI's menu would. The caller then
   exits. */
bool Launch_HandOff(const Roms_Match *match, int resume_slot);

/* Moves path to the top of NextUI's Recently Played, adding it if new and
   keeping NextUI's limit. Exposed for testing; Launch_HandOff calls it. */
bool Launch_AddRecent(const char *rom_path);

#endif

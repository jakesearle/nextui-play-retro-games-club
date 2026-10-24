#ifndef LAUNCH_H
#define LAUNCH_H

#include <stdbool.h>

#include "roms.h"

/* Read by launch.sh after the app exits: the emulator pak's launch.sh, then
   the ROM, one per line. NextUI's own /tmp/next is deleted as soon as a pak
   returns, so launch.sh starts the game itself. */
#define LAUNCH_FILE "/tmp/prgc-tracker-launch"

/* The title of the pick being played, so the app can reselect it when
   launch.sh reopens it after the game. */
#define LAUNCH_SELECTION_FILE "/tmp/prgc-tracker-selection"

/* Hands a found game to launch.sh and adds it to NextUI's Recently Played,
   as opening it from NextUI's menu would. The caller then exits. */
bool Launch_HandOff(const Roms_Match *match);

/* Moves path to the top of NextUI's Recently Played, adding it if new and
   keeping NextUI's limit. Exposed for testing; Launch_HandOff calls it. */
bool Launch_AddRecent(const char *rom_path);

#endif

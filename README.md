# PRGC Tracker

A NextUI Pak that shows what the
[Play Retro Games Club](https://www.joeysretrohandhelds.com/play-retro-games-club/)
is playing right now: Game of the Week, Game of the Month, Romhack of the
Month, Game of the Season and any specials, with each game's stats,
description and links.

Built with [Apostrophe](https://github.com/Helaas/Apostrophe), a C UI toolkit
for NextUI, following the layout of
[RA Explorer](https://github.com/apommel/nextui-ra-explorer).

## Screens

| Screen | Contents |
|---|---|
| Current picks | One row per current game, with its event underneath, and Refresh at the bottom |
| Game details | Box art, event dates, rating, beaten/mastered/completed counts, time to beat, description, links |

| Screen | B | X | A |
|---|---|---|---|
| Current picks | Quit | Resume the game | Details |
| Game details | Back | Resume the game | Play from the start |

Resume loads the last save state you used for that game, as NextUI's own
X does; a game with no save state just starts. Neither asks first: when the
game is closed you are back in the Pak, on the same pick.

## Playing a pick

Play looks for the pick among your own ROMs and starts it in the emulator
NextUI would use. A ROM counts only if RetroAchievements recognizes it as that
game, so a misnamed file still matches and a different dump of the right game
does not:

1. The pick's RetroAchievements link gives its game id, and the label at the
   end of its title ("(SNES)") gives the console.
2. The Pak downloads RA's list of every known ROM hash for that console
   (`dorequest.php?r=hashlibrary`, no account needed) and keeps it for three
   days.
3. It hashes the ROMs in `Roms/` folders whose tag belongs to that console
   (`Super Nintendo (SFC)` → SFC), subfolders included, with
   [rcheevos](https://github.com/RetroAchievements/rcheevos) at the exact
   version NextUI's own RA support uses. Every file is checked, so the first
   search of a folder reads it all; hashes are cached by path, size and
   modified time, so after that only new ROMs are read.
4. If several files are the game (a duplicate, or a variant RA lists under
   the same game, like an MSU-1 version beside the plain one), the one with a
   save state wins, then the one played most recently; failing both, the
   Pak asks which copy. Once played it is in Recently Played, so that
   question comes up once.
5. Resume reads the last save slot you used for the ROM from where minarch
   records it, `.userdata/shared/.minui/<TAG>/<rom>.txt`; Play, or a game
   with no save, asks minarch for slot 8, its fresh start.
6. On a match it adds the game to Recently Played, writes the
   emulator and ROM to `/tmp/prgc-tracker-launch` and exits. `launch.sh` then
   starts the game the way NextUI's menu would: Game Time tracking, and the `pre-launch.d` and
   `post-launch.d` hooks with NextUI's `HOOK_*` variables. NextUI offers paks
   no way to launch a ROM themselves: hooks only observe, and `/tmp/next` is
   deleted as soon as a pak exits.
7. When the game is closed, `launch.sh` opens the Pak again on the game you
   played, reusing the page it fetched before (if under 15 minutes old)
   instead of downloading it again. Quitting the Pak returns to NextUI.

For a console with an emulator in the Pak Store for this device (Nintendo 64,
Dreamcast, PPSSPP on TrimUI) but none installed, the Pak says which one to
download. Any other RetroAchievements console NextUI cannot run (GameCube,
Wii, PS2, Saturn...) is reported as such. CHD disc images are
not hashed yet. Arcade games are identified by file name, as RA does, so they
need their standard MAME/FBNeo names.

`task findrom` runs the same search without the UI against `$ROMS_PATH`, and
`task findrom ARGS="--hash <console id> <file>"` prints one file's RA hash.

Without a connection the Pak shows the last page it fetched and marks every
title with how old it is.

## How it gets the data

There is no API. The club page is server-rendered WordPress, so the Pak
downloads it and reads the cards by their CSS classes (`jrh-pick-card`,
`jrh-pick-card__title`, `jrh-pick-card__stat`, …). Those class names are
the contract: if the site's markup changes, the Pak reports that no games were
found rather than showing half-empty screens. `src/club.c` is the only file
that knows the page's layout.

`task scrape` runs the real parser without the UI and prints what it found,
which is the quickest way to check a markup change:

```
task scrape                   # live page
task scrape FILE=saved.html   # a saved copy
```

## Tests

```
task test
```

`meson test` runs the rules behind Play against fixture SD cards: fake ROMs
generated on the fly (zipped with zlib where a test needs it), fixture RA
hash lists, and stand-ins for the emulator, NextUI's hook runner and the
CPU governor. Nothing touches the network or needs a real ROM.

| Suite | Covers |
|---|---|
| `tests/test_roms.c` | Pick → console and game id; hashing as minarch does (zips, copier and iNES headers, arcade by name); every search result: no folder, empty folders, no match, offline, found with its emulator, duplicates and variants, all of a console's folders, deep subfolders, `.bin` tracks owned by a `.cue`, CD games in the cartridge folder, the hash cache |
| `tests/test_decide.c` | Which copy to play (save state, then Recently Played, then ask) and what each result tells the player (missing NextUI folder, Pak Store emulator, can't play), per device |
| `tests/test_launch.c` | Resume slots, Recently Played (order, aliases, the 24-entry cap) and the hand-off file |
| `tests/test_launch_sh.sh` | `launch.sh`: the app → game → app loop, slot fallback, missing ROMs, hooks, the governor, paths with quotes |

The rules themselves live outside the screens, in `src/decide.c`, so the
tests do not need SDL. They check which outcome is chosen, not its wording.

## Requirements

- [Meson](https://mesonbuild.com/) and Ninja
- [Task](https://taskfile.dev/)
- SDL2, SDL2_ttf, SDL2_image and libcurl, for native builds
- Docker, for device builds

On macOS: `brew install meson ninja go-task sdl2 sdl2_ttf sdl2_image`, plus
Docker Desktop for device builds.

## Building

```
task build     # native, for development
task run       # native, and launch it
```

The device build runs inside the NextUI toolchain container and produces a
single binary for every handheld: `tg5040` (TrimUI Brick / Smart Pro),
`tg5050` (Smart Pro S), `my355` (Miyoo Flip) and `h700` (Anbernic).

```
task device                    # cross-compile
task pak                       # assemble the Pak in dist/
task package                   # zip it into dist/PRGC.Tracker.pak.zip
task adb PLATFORM=tg5040       # build, assemble and push over adb
```

The first device build also compiles curl and OpenSSL, which takes a while.

## Installing by hand

Create `Tools/<platform>/PRGC Tracker.pak/` on the SD card and extract
`PRGC.Tracker.pak.zip` into it:

```
Tools/tg5040/PRGC Tracker.pak/
├── launch.sh
├── prgc-tracker
├── cacert.pem
├── LICENSE.txt
└── THIRD-PARTY-NOTICES.txt
```

The saved page and box art live in `$SHARED_USERDATA_PATH/prgc-tracker` on
device (`~/.userdata/prgc-tracker` elsewhere), along with RA's hash lists and
the ROM hash cache (`hashes-v2.tsv`); the log is
`$LOGS_PATH/prgc-tracker.txt`.

## Publishing to the Pak Store

1. Push this repo to GitHub and make sure `repo_url` in `pak.json` matches.
2. Take screenshots and list them under `screenshots` in `pak.json`.
3. Tag a release `v1.0.0` (matching `version`) and attach
   `dist/PRGC.Tracker.pak.zip`, matching `release_filename`.
4. Submit it with the
   [Pak Store issue form](https://github.com/LoveRetro/nextui-pak-store/issues/new?template=new-pak-submission.yml).

## Licence

MIT; see `LICENSE.txt`. Bundled components (Apostrophe, rcheevos, curl, OpenSSL
and a CA bundle) are under their own terms in `THIRD-PARTY-NOTICES.txt`. Game data
and box art are fetched at runtime from joeysretrohandhelds.com and
retroachievements.org and are not distributed with the Pak.

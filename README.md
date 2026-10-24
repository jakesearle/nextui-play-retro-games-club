# PRGC Tracker

A NextUI Pak that shows what the
[Play Retro Games Club](https://www.joeysretrohandhelds.com/play-retro-games-club/)
is playing right now: Game of the Week, Game of the Month, Romhack of the
Month, Game of the Season and any specials, with each game's stats,
description and links, plus the club's recent activity feed.

Built with [Apostrophe](https://github.com/Helaas/Apostrophe), a C UI toolkit
for NextUI, following the layout of
[RA Explorer](https://github.com/apommel/nextui-ra-explorer).

## Screens

| Screen | Contents |
|---|---|
| Current picks | One row per current game, with its event on the right |
| Game details | Box art, event dates, rating, beaten/mastered/completed counts, time to beat, description, links |
| Recent Activity | Who beat or completed what, and when |

**A** opens, **B** goes back (or quits from the main list), **Y** refreshes.

Without a connection the Pak shows the last page it fetched and marks every
title with how old it is.

## How it gets the data

There is no API. The club page is server-rendered WordPress, so the Pak
downloads it and reads the cards by their CSS classes (`jrh-pick-card`,
`jrh-pick-card__title`, `jrh-activity-feed__item`, …). Those class names are
the contract: if the site's markup changes, the Pak reports that no games were
found rather than showing half-empty screens. `src/club.c` is the only file
that knows the page's layout.

`task scrape` runs the real parser without the UI and prints what it found,
which is the quickest way to check a markup change:

```
task scrape                   # live page
task scrape FILE=saved.html   # a saved copy
```

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
device (`~/.userdata/prgc-tracker` elsewhere); the log is
`$LOGS_PATH/prgc-tracker.txt`.

## Publishing to the Pak Store

1. Push this repo to GitHub and make sure `repo_url` in `pak.json` matches.
2. Take screenshots and list them under `screenshots` in `pak.json`.
3. Tag a release `v1.0.0` (matching `version`) and attach
   `dist/PRGC.Tracker.pak.zip`, matching `release_filename`.
4. Submit it with the
   [Pak Store issue form](https://github.com/LoveRetro/nextui-pak-store/issues/new?template=new-pak-submission.yml).

## Licence

MIT; see `LICENSE.txt`. Bundled components (Apostrophe, curl, OpenSSL and a
CA bundle) are under their own terms in `THIRD-PARTY-NOTICES.txt`. Game data
and box art are fetched at runtime from joeysretrohandhelds.com and
retroachievements.org and are not distributed with the Pak.

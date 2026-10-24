#ifndef CLUB_H
#define CLUB_H

#include <stdbool.h>
#include <stddef.h>

/* The Play Retro Games Club page, scraped. There is no API: the page is
   server-rendered WordPress, and its jrh-* class names are the contract. If
   they change, parsing finds nothing and Club_Load reports CLUB_ERROR_PARSE
   rather than showing a half-empty screen. */

#define CLUB_PAGE_URL "https://www.joeysretrohandhelds.com/play-retro-games-club/"

#define CLUB_MAX_PICKS 12
#define CLUB_MAX_STATS 8

typedef struct {
    char key[24];   /* "Rating", "Beaten", "Time to beat" */
    char value[24]; /* "7.2/10", "33", "~2 hrs" */
} Club_Stat;

/* One card from the current picks grid. */
typedef struct {
    char event[96];       /* "Game of the Week October 18 - 24, 2026" */
    char next[96];        /* "New week starts October 25, 2026", may be empty */
    char title[128];      /* "Neo Turf Masters (Arcade)" */
    char description[1024];
    char image_url[256];  /* Box art on retroachievements.org, may be empty */
    char ra_url[160];     /* RetroAchievements game page, may be empty */
    char guide_url[256];  /* May be empty */
    Club_Stat stats[CLUB_MAX_STATS];
    int stat_count;
} Club_Pick;

typedef struct {
    Club_Pick picks[CLUB_MAX_PICKS];
    int pick_count;
    bool from_cache;  /* The network failed and this is the saved copy */
    long age_seconds; /* How old the page is; 0 when just fetched */
} Club_Data;

typedef enum {
    CLUB_OK = 0,
    CLUB_ERROR_NETWORK, /* No connection and nothing saved */
    CLUB_ERROR_PARSE,   /* Fetched, but no picks found: the page changed */
} Club_Result;

/* Fetches the page, falling back to the last saved copy when offline. A
   saved copy younger than reuse_seconds is used without asking the network
   at all (0 always fetches). Blocking; call it from a worker. */
Club_Result Club_Load(Club_Data *data, long reuse_seconds);

/* Parses page HTML into data. Exposed so the parser can be tested against a
   saved page without the network. */
Club_Result Club_Parse(const char *html, size_t len, Club_Data *data);

/* Downloads a pick's box art into the image cache if it is not there yet,
   writing its local path to out. Blocking. */
bool Club_FetchImage(const char *url, char *out, size_t out_size);

#endif

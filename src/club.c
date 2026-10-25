#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "club.h"
#include "html.h"
#include "io.h"
#include "paths.h"

/* ── Parsing ─────────────────────────────────────────────────────────────── */

/* Copies in, cut short to fit. Spelled out rather than snprintf so the
   device compiler's -Wformat-truncation does not flag the cut, which is
   intended: chip labels are short, and a longer one is still readable. */
static void CopyString(char *out, size_t out_size, const char *in) {
    if (out_size == 0) return;
    size_t len = strlen(in);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, in, len);
    out[len] = '\0';
}

/* Turns one stat chip into a key and value. The chips read "33 beaten",
   "1 mastered" or "~2 hrs to beat"; the rating chip is just "7.2/10". */
static void ParseStat(Html_Span chip, Club_Stat *stat) {
    char text[64];
    Html_Text(chip, text, sizeof(text));

    Html_Span rating;
    if (Html_FindClass(chip, "jrh-pick-card__stat--rating", &rating) && rating.start == chip.start) {
        CopyString(stat->key, sizeof(stat->key), "Rating");
        CopyString(stat->value, sizeof(stat->value), text);
        return;
    }

    static const char to_beat[] = " to beat";
    size_t len = strlen(text);
    if (len > sizeof(to_beat) - 1 && strcmp(text + len - (sizeof(to_beat) - 1), to_beat) == 0) {
        text[len - (sizeof(to_beat) - 1)] = '\0';
        CopyString(stat->key, sizeof(stat->key), "Time to beat");
        CopyString(stat->value, sizeof(stat->value), text);
        return;
    }

    /* "<count> <what>": the count is the value, the rest names it. */
    char *space = strchr(text, ' ');
    if (!space) {
        CopyString(stat->key, sizeof(stat->key), "Stat");
        CopyString(stat->value, sizeof(stat->value), text);
        return;
    }
    *space = '\0';
    CopyString(stat->value, sizeof(stat->value), text);
    CopyString(stat->key, sizeof(stat->key), space + 1);
    if (stat->key[0] >= 'a' && stat->key[0] <= 'z') stat->key[0] = (char)(stat->key[0] - 'a' + 'A');
}

static void ParsePick(Html_Span card, Club_Pick *pick) {
    memset(pick, 0, sizeof(*pick));

    /* The event label comes first; the "next" label shares its class and
       adds a modifier, so the first match is always the event. */
    Html_ClassText(card, "jrh-pick-card__label", pick->event, sizeof(pick->event));
    Html_ClassText(card, "jrh-pick-card__label--next", pick->next, sizeof(pick->next));
    Html_ClassText(card, "jrh-pick-card__title", pick->title, sizeof(pick->title));
    Html_ClassText(card, "jrh-pick-card__description", pick->description, sizeof(pick->description));

    Html_Span image;
    if (Html_FindClass(card, "jrh-pick-card__image", &image)) {
        Html_StyleUrl(image.start, image.end, pick->image_url, sizeof(pick->image_url));
    }

    Html_Span rest = card, stat;
    while (pick->stat_count < CLUB_MAX_STATS && Html_FindClass(rest, "jrh-pick-card__stat", &stat)) {
        ParseStat(stat, &pick->stats[pick->stat_count]);
        if (pick->stats[pick->stat_count].value[0]) pick->stat_count++;
        rest.start = stat.end;
    }

    /* Links are told apart by their text; there is nothing else on them. */
    Html_Span link;
    rest = card;
    while (Html_FindClass(rest, "jrh-pick-card__link", &link)) {
        char text[32];
        Html_Text(link, text, sizeof(text));
        if (strcmp(text, "RetroAchievements") == 0) {
            Html_Attr(link.start, link.end, "href", pick->ra_url, sizeof(pick->ra_url));
        } else if (strcmp(text, "Guide") == 0) {
            Html_Attr(link.start, link.end, "href", pick->guide_url, sizeof(pick->guide_url));
        }
        rest.start = link.end;
    }
}

Club_Result Club_Parse(const char *html, size_t len, Club_Data *data) {
    Html_Span page = { html, html + len };
    data->pick_count = 0;

    Html_Span grid, card;
    if (Html_FindClass(page, "jrh-picks-grid", &grid)) {
        Html_Span rest = grid;
        while (data->pick_count < CLUB_MAX_PICKS && Html_FindClass(rest, "jrh-pick-card", &card)) {
            Club_Pick *pick = &data->picks[data->pick_count];
            ParsePick(card, pick);
            if (pick->title[0]) data->pick_count++;
            rest.start = card.end;
        }
    }

    return data->pick_count > 0 ? CLUB_OK : CLUB_ERROR_PARSE;
}

/* ── Cache ───────────────────────────────────────────────────────────────── */

static bool CachePath(char *out, size_t out_size) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL) || !Paths_MakeDirs(dir)) return false;
    int written = snprintf(out, out_size, "%s/club.html", dir);
    return written > 0 && (size_t)written < out_size;
}

Club_Result Club_Load(Club_Data *data, long reuse_seconds) {
    char cache_path[512];
    bool have_cache_path = CachePath(cache_path, sizeof(cache_path));

    long age = have_cache_path ? Io_FileAge(cache_path) : -1;
    if (age >= 0 && age < reuse_seconds) {
        size_t len = 0;
        char *cached = Io_ReadFile(cache_path, &len);
        Club_Result result = cached ? Club_Parse(cached, len, data) : CLUB_ERROR_NETWORK;
        free(cached);
        if (result == CLUB_OK) {
            data->from_cache = false; /* recent, not a fallback */
            data->age_seconds = age;
            return CLUB_OK;
        }
    }

    Io_Buffer page = { 0 };
    if (Io_HttpGet(CLUB_PAGE_URL, &page)) {
        Club_Result result = Club_Parse(page.data, page.size, data);
        /* Only a page that parsed is worth keeping for offline use. */
        if (result == CLUB_OK && have_cache_path) {
            Io_WriteFileAtomic(cache_path, page.data, page.size);
        }
        free(page.data);
        data->from_cache = false;
        data->age_seconds = 0;
        return result;
    }
    free(page.data);

    if (!have_cache_path) return CLUB_ERROR_NETWORK;

    size_t len = 0;
    char *cached = Io_ReadFile(cache_path, &len);
    if (!cached) return CLUB_ERROR_NETWORK;

    Club_Result result = Club_Parse(cached, len, data);
    free(cached);
    if (result != CLUB_OK) return CLUB_ERROR_NETWORK;

    data->from_cache = true;
    data->age_seconds = Io_FileAge(cache_path);
    return CLUB_OK;
}

/* ── Images ──────────────────────────────────────────────────────────────── */

bool Club_FetchImage(const char *url, char *out, size_t out_size) {
    if (!url || !url[0]) return false;

    /* Box art lives at retroachievements.org/Images/<id>.png; the id makes a
       stable file name. Anything else is not something this page serves. */
    const char *name = strrchr(url, '/');
    if (!name || !name[1] || strstr(name, "..") || strlen(name) > 64) return false;
    name++;

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images") || !Paths_MakeDirs(dir)) return false;
    int written = snprintf(out, out_size, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= out_size) return false;

    struct stat info;
    if (stat(out, &info) == 0 && info.st_size > 0) return true;

    Io_Buffer image = { 0 };
    bool ok = Io_HttpGet(url, &image) && image.size >= 8 &&
              memcmp(image.data, "\x89PNG", 4) == 0 &&
              Io_WriteFileAtomic(out, image.data, image.size);
    free(image.data);
    return ok;
}

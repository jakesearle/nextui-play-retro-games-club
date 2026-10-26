#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

/* ── When the page changes ────────────────────────────────────────────── */

static const char *const k_months[] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December",
};

/* "2026-10-29T00:00:00+00:00", the countdown's data-target. */
static bool ParseIsoTime(const char *s, time_t *out) {
    int y, mo, d, h, mi, sec, off_h = 0, off_m = 0;
    char sign = '+';
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6) return false;
    const char *tz = s + 19;
    if (*tz == '+' || *tz == '-') {
        sign = *tz;
        if (sscanf(tz + 1, "%2d:%2d", &off_h, &off_m) != 2) return false;
    } else if (*tz != 'Z' && *tz != '\0') {
        return false;
    }
    struct tm tm = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d,
                     .tm_hour = h, .tm_min = mi, .tm_sec = sec };
    time_t t = timegm(&tm);
    long offset = (off_h * 60L + off_m) * 60L;
    *out = sign == '+' ? t - offset : t + offset;
    return true;
}

/* The first "<Month> <day>, <year>" in s, as midnight UTC: the club's
   countdown runs on UTC midnights. */
static bool ParseDayInText(const char *s, time_t *out) {
    for (size_t m = 0; m < sizeof(k_months) / sizeof(k_months[0]); m++) {
        const char *at = strstr(s, k_months[m]);
        int day, year;
        if (!at || sscanf(at + strlen(k_months[m]), " %d, %d", &day, &year) != 2) continue;
        struct tm tm = { .tm_year = year - 1900, .tm_mon = (int)m, .tm_mday = day };
        *out = timegm(&tm);
        return true;
    }
    return false;
}

static void Earliest(time_t *best, time_t candidate, time_t after) {
    if (candidate > after && (*best == 0 || candidate < *best)) *best = candidate;
}

time_t Club_NextChange(const char *html, size_t len, time_t fetched) {
    Html_Span page = { html, html + len };
    time_t next = 0;

    Html_Span countdown;
    char target[64];
    time_t t;
    if (Html_FindClass(page, "jrh-countdown", &countdown) &&
        Html_Attr(countdown.start, countdown.end, "data-target", target, sizeof(target)) &&
        ParseIsoTime(target, &t)) {
        Earliest(&next, t, fetched);
    }

    Html_Span rest = page, label;
    while (Html_FindClass(rest, "jrh-pick-card__label--next", &label)) {
        char text[128];
        Html_Text(label, text, sizeof(text));
        if (ParseDayInText(text, &t)) Earliest(&next, t, fetched);
        rest.start = label.end;
    }

    /* The monthly picks change on the 1st, whether or not anything says so. */
    struct tm now;
    gmtime_r(&fetched, &now);
    struct tm first = { .tm_year = now.tm_year, .tm_mon = now.tm_mon + 1, .tm_mday = 1 };
    Earliest(&next, timegm(&first), fetched);
    return next;
}

bool Club_CacheIsFresh(time_t now, time_t fetched, time_t next_change) {
    if (fetched <= 0 || now < fetched) return false;
    if (now - fetched >= CLUB_MAX_CACHE_AGE_SECONDS) return false;
    return next_change == 0 || now < next_change;
}

/* ── Cache ───────────────────────────────────────────────────────────────── */

/* club.html is the last page that parsed; club.meta, beside it, says when
   the site last confirmed it and its ETag:
     fetched <unix time>
     etag <value> */
static bool CachePaths(char *html, size_t html_size, char *meta, size_t meta_size) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL) || !Paths_MakeDirs(dir)) return false;
    int a = snprintf(html, html_size, "%s/club.html", dir);
    int b = snprintf(meta, meta_size, "%s/club.meta", dir);
    return a > 0 && (size_t)a < html_size && b > 0 && (size_t)b < meta_size;
}

static void ReadMeta(const char *path, time_t *fetched, char *etag, size_t etag_size) {
    *fetched = 0;
    if (etag_size) etag[0] = '\0';
    size_t len = 0;
    char *text = Io_ReadFile(path, &len);
    if (!text) return;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strncmp(line, "fetched ", 8) == 0) *fetched = (time_t)atoll(line + 8);
        else if (strncmp(line, "etag ", 5) == 0) snprintf(etag, etag_size, "%s", line + 5);
    }
    free(text);
}

static void WriteMeta(const char *path, time_t fetched, const char *etag) {
    char text[512];
    int n = snprintf(text, sizeof(text), "fetched %lld\netag %s\n", (long long)fetched, etag ? etag : "");
    if (n > 0 && (size_t)n < sizeof(text)) Io_WriteFileAtomic(path, text, (size_t)n);
}

/* Parses the saved page into data. */
static Club_Result ParseSaved(const char *path, Club_Data *data, char **html_out, size_t *len_out) {
    size_t len = 0;
    char *html = Io_ReadFile(path, &len);
    if (!html) return CLUB_ERROR_NETWORK;
    Club_Result result = Club_Parse(html, len, data);
    if (html_out && result == CLUB_OK) {
        *html_out = html;
        *len_out = len;
    } else {
        free(html);
    }
    return result;
}

Club_Result Club_Load(Club_Data *data, Club_LoadMode mode) {
    char html_path[512], meta_path[512];
    bool have_cache = CachePaths(html_path, sizeof(html_path), meta_path, sizeof(meta_path));
    time_t now = time(NULL), fetched = 0;
    char etag[256] = "";
    if (have_cache) ReadMeta(meta_path, &fetched, etag, sizeof(etag));

    /* Still fresh: no network at all. */
    if (have_cache && mode == CLUB_LOAD_AUTO && fetched > 0) {
        char *html = NULL;
        size_t len = 0;
        if (ParseSaved(html_path, data, &html, &len) == CLUB_OK) {
            bool fresh = Club_CacheIsFresh(now, fetched, Club_NextChange(html, len, fetched));
            free(html);
            if (fresh) {
                data->from_cache = false; /* current, not a fallback */
                data->age_seconds = (long)(now - fetched);
                return CLUB_OK;
            }
        }
    }

    /* Ask the site, offering the saved page's ETag only when that page is
       there to fall back on. */
    bool saved_ok = have_cache && Io_FileAge(html_path) >= 0;
    Io_Buffer page = { 0 };
    char new_etag[256] = "";
    bool not_modified = false;
    if (Io_HttpGetIfChanged(CLUB_PAGE_URL, saved_ok ? etag : NULL, &page,
                            new_etag, sizeof(new_etag), &not_modified)) {
        if (not_modified) {
            free(page.data);
            Club_Result result = ParseSaved(html_path, data, NULL, NULL);
            if (result == CLUB_OK) {
                WriteMeta(meta_path, now, new_etag[0] ? new_etag : etag);
                data->from_cache = false;
                data->age_seconds = 0;
                return CLUB_OK;
            }
            /* The saved copy no longer parses; fetch it whole. */
            memset(&page, 0, sizeof(page));
            if (!Io_HttpGetIfChanged(CLUB_PAGE_URL, NULL, &page, new_etag, sizeof(new_etag), &not_modified)) {
                free(page.data);
                return CLUB_ERROR_NETWORK;
            }
        }
        Club_Result result = Club_Parse(page.data, page.size, data);
        /* Only a page that parsed is worth keeping for offline use. */
        if (result == CLUB_OK && have_cache &&
            Io_WriteFileAtomic(html_path, page.data, page.size)) {
            WriteMeta(meta_path, now, new_etag);
        }
        free(page.data);
        data->from_cache = false;
        data->age_seconds = 0;
        return result;
    }
    free(page.data);

    /* Offline: the saved page, marked as a fallback. */
    if (!have_cache || ParseSaved(html_path, data, NULL, NULL) != CLUB_OK) return CLUB_ERROR_NETWORK;
    data->from_cache = true;
    data->age_seconds = fetched > 0 && now >= fetched ? (long)(now - fetched) : Io_FileAge(html_path);
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

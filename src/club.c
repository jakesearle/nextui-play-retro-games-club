#define CLUB_USER_AGENT "nextui-prgc-tracker/1.0.0"
#define CLUB_CONNECT_TIMEOUT_SECONDS 5L
#define CLUB_TIMEOUT_SECONDS 30L
/* The page is ~200 KB. Anything far beyond that is not the page. */
#define CLUB_MAX_PAGE_BYTES (4L * 1024 * 1024)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <curl/curl.h>

#include "club.h"
#include "html.h"
#include "paths.h"

/* ── Parsing ─────────────────────────────────────────────────────────────── */

static void CopyString(char *out, size_t out_size, const char *in) {
    snprintf(out, out_size, "%s", in);
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

static void ParseActivity(Html_Span item, Club_Activity *activity) {
    memset(activity, 0, sizeof(*activity));
    Html_ClassText(item, "jrh-activity-feed__text", activity->text, sizeof(activity->text));
    Html_ClassText(item, "jrh-activity-feed__pill", activity->event, sizeof(activity->event));
    Html_ClassText(item, "jrh-activity-feed__time", activity->when, sizeof(activity->when));
}

Club_Result Club_Parse(const char *html, size_t len, Club_Data *data) {
    Html_Span page = { html, html + len };
    data->pick_count = 0;
    data->activity_count = 0;

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

    Html_Span feed, item;
    if (Html_FindClass(page, "jrh-activity-feed", &feed)) {
        Html_Span rest = feed;
        while (data->activity_count < CLUB_MAX_ACTIVITY &&
               Html_FindClass(rest, "jrh-activity-feed__item", &item)) {
            Club_Activity *activity = &data->activity[data->activity_count];
            ParseActivity(item, activity);
            if (activity->text[0]) data->activity_count++;
            rest.start = item.end;
        }
    }

    return data->pick_count > 0 ? CLUB_OK : CLUB_ERROR_PARSE;
}

/* ── Network ─────────────────────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t size;
} Buffer;

static size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t chunk = size * nmemb;
    Buffer *buffer = (Buffer *)userdata;
    if (buffer->size + chunk > (size_t)CLUB_MAX_PAGE_BYTES) return 0;

    char *grown = realloc(buffer->data, buffer->size + chunk + 1);
    if (!grown) return 0;

    buffer->data = grown;
    memcpy(buffer->data + buffer->size, ptr, chunk);
    buffer->size += chunk;
    buffer->data[buffer->size] = '\0';
    return chunk;
}

/* GETs url into buffer. Only a 200 counts: an error page parsed as the club
   page would read as "the page changed". */
static bool HttpGet(const char *url, Buffer *buffer) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, CLUB_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CLUB_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CLUB_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);

    /* curl is statically linked against our own OpenSSL on device, which
       knows no system trust store; launch.sh points this at cacert.pem. */
    const char *ca_bundle = getenv("SSL_CERT_FILE");
    if (ca_bundle && ca_bundle[0]) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) fprintf(stderr, "GET %s: %s\n", url, curl_easy_strerror(code));
    else if (status != 200) fprintf(stderr, "GET %s: HTTP %ld\n", url, status);
    return code == CURLE_OK && status == 200;
}

/* ── Cache ───────────────────────────────────────────────────────────────── */

static bool CachePath(char *out, size_t out_size) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL) || !Paths_MakeDirs(dir)) return false;
    int written = snprintf(out, out_size, "%s/club.html", dir);
    return written > 0 && (size_t)written < out_size;
}

/* Writes via a temporary file, so a failed write never replaces a good copy. */
static bool WriteFileAtomic(const char *path, const char *data, size_t len) {
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;

    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) remove(tmp);
    return ok;
}

static char *ReadFile(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    Buffer buffer = { 0 };
    char chunk[16384];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (WriteCallback(chunk, 1, n, &buffer) != n) {
            free(buffer.data);
            fclose(f);
            return NULL;
        }
    }
    fclose(f);
    *out_len = buffer.size;
    return buffer.data;
}

Club_Result Club_Load(Club_Data *data) {
    char cache_path[512];
    bool have_cache_path = CachePath(cache_path, sizeof(cache_path));

    Buffer page = { 0 };
    if (HttpGet(CLUB_PAGE_URL, &page)) {
        Club_Result result = Club_Parse(page.data, page.size, data);
        /* Only a page that parsed is worth keeping for offline use. */
        if (result == CLUB_OK && have_cache_path) {
            WriteFileAtomic(cache_path, page.data, page.size);
        }
        free(page.data);
        data->from_cache = false;
        data->age_seconds = 0;
        return result;
    }
    free(page.data);

    if (!have_cache_path) return CLUB_ERROR_NETWORK;

    size_t len = 0;
    char *cached = ReadFile(cache_path, &len);
    if (!cached) return CLUB_ERROR_NETWORK;

    Club_Result result = Club_Parse(cached, len, data);
    free(cached);
    if (result != CLUB_OK) return CLUB_ERROR_NETWORK;

    struct stat info;
    data->from_cache = true;
    data->age_seconds = stat(cache_path, &info) == 0 ? (long)(time(NULL) - info.st_mtime) : -1;
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

    Buffer image = { 0 };
    bool ok = HttpGet(url, &image) && image.size >= 8 &&
              memcmp(image.data, "\x89PNG", 4) == 0 &&
              WriteFileAtomic(out, image.data, image.size);
    free(image.data);
    return ok;
}

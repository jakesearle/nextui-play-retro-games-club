/* When the saved club page can be trusted without asking the site: the
   page's own next change, the six-hour cap, and the cache's offline paths. */

#include "check.h"
#include "club.h"

/* Seconds since the epoch for a UTC date and time. */
static time_t Utc(int y, int mo, int d, int h, int mi) {
    struct tm tm = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi };
    return timegm(&tm);
}

#define HOUR (60 * 60)

/* A page shaped like the club's, with one pick, a countdown and next lines. */
static void Page(char *out, size_t size, const char *countdown, const char *next1, const char *next2) {
    snprintf(out, size,
             "<html><body>"
             "<a class=\"jrh-countdown\" href=\"x\" data-target=\"%s\">"
             "<span class=\"jrh-countdown__label\">Next: something</span></a>"
             "<div class=\"jrh-picks-grid\"><div class=\"jrh-pick-card\"><div class=\"jrh-pick-card__body\">"
             "<span class=\"jrh-pick-card__label\">Game of the Week October 25 - 31, 2026</span>"
             "<span class=\"jrh-pick-card__label jrh-pick-card__label--next\">%s</span>"
             "<h2 class=\"jrh-pick-card__title\">Galaga (NES)</h2></div></div>"
             "<div class=\"jrh-pick-card\"><div class=\"jrh-pick-card__body\">"
             "<span class=\"jrh-pick-card__label\">Game of the Month October 2026</span>"
             "<span class=\"jrh-pick-card__label jrh-pick-card__label--next\">%s</span>"
             "<h2 class=\"jrh-pick-card__title\">Super Mario Sunshine (GC)</h2></div></div></div>"
             "</body></html>",
             countdown, next1, next2);
}

/* ── The next change ─────────────────────────────────────────────────────── */

static void the_countdown_counts(void) {
    char html[4096];
    Page(html, sizeof(html), "2026-10-29T00:00:00+00:00", "New week starts November 1, 2026",
         "Next month&#039;s voting starts October 30, 2026");
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 10, 26, 15, 0)), Utc(2026, 10, 29, 0, 0));
}

static void countdown_offsets_are_honoured(void) {
    char html[4096];
    Page(html, sizeof(html), "2026-10-28T20:00:00-04:00", "", "");
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 10, 26, 15, 0)), Utc(2026, 10, 29, 0, 0));
}

static void a_next_line_can_be_sooner(void) {
    char html[4096];
    Page(html, sizeof(html), "2026-11-05T00:00:00+00:00", "New week starts October 27, 2026",
         "Next month&#039;s voting starts October 30, 2026");
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 10, 26, 15, 0)), Utc(2026, 10, 27, 0, 0));
}

static void the_first_of_the_month_always_counts(void) {
    char html[4096];
    /* Nothing on the page until mid-November, but the monthly picks change
       on the 1st. */
    Page(html, sizeof(html), "2026-11-15T00:00:00+00:00", "", "");
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 10, 30, 12, 0)), Utc(2026, 11, 1, 0, 0));
    /* December rolls over into January. */
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 12, 20, 12, 0)), Utc(2027, 1, 1, 0, 0));
}

static void times_already_past_are_ignored(void) {
    char html[4096];
    Page(html, sizeof(html), "2026-10-25T00:00:00+00:00", "New week starts October 25, 2026",
         "New week starts November 1, 2026");
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 10, 26, 15, 0)), Utc(2026, 11, 1, 0, 0));
}

static void a_page_without_dates(void) {
    const char *html = "<html><body>nothing here</body></html>";
    CHECK_INT(Club_NextChange(html, strlen(html), Utc(2026, 10, 26, 15, 0)), Utc(2026, 11, 1, 0, 0));
}

/* ── Fresh or not ────────────────────────────────────────────────────────── */

static void fresh_until_six_hours_or_the_next_change(void) {
    time_t fetched = Utc(2026, 10, 26, 12, 0), change = Utc(2026, 10, 29, 0, 0);
    CHECK(Club_CacheIsFresh(fetched, fetched, change));
    CHECK(Club_CacheIsFresh(fetched + 6 * HOUR - 1, fetched, change));
    CHECK(!Club_CacheIsFresh(fetched + 6 * HOUR, fetched, change));

    time_t soon = fetched + HOUR;
    CHECK(Club_CacheIsFresh(soon - 1, fetched, soon));
    CHECK(!Club_CacheIsFresh(soon, fetched, soon));
}

static void a_clock_before_the_fetch_is_not_trusted(void) {
    time_t fetched = Utc(2026, 10, 26, 12, 0);
    CHECK(!Club_CacheIsFresh(fetched - 1, fetched, Utc(2026, 10, 29, 0, 0)));
    CHECK(!Club_CacheIsFresh(Utc(1970, 1, 2, 0, 0), fetched, Utc(2026, 10, 29, 0, 0)));
    CHECK(!Club_CacheIsFresh(fetched, 0, Utc(2026, 10, 29, 0, 0))); /* never fetched */
}

/* ── Loading ─────────────────────────────────────────────────────────────── */

static void SavedPage(time_t fetched) {
    char html[4096];
    Page(html, sizeof(html), "2099-01-01T00:00:00+00:00", "", "");
    WriteText("sd/.userdata/shared/prgc-tracker/club.html", html);
    char meta[128];
    snprintf(meta, sizeof(meta), "fetched %lld\netag \"abc\"\n", (long long)fetched);
    WriteText("sd/.userdata/shared/prgc-tracker/club.meta", meta);
}

/* Any request now goes to a port nothing listens on, so it fails at once:
   a load that succeeds without being a fallback never asked. */
static void NoNetwork(bool off) {
    if (off) {
        setenv("https_proxy", "http://127.0.0.1:9", 1);
        setenv("HTTPS_PROXY", "http://127.0.0.1:9", 1);
    } else {
        unsetenv("https_proxy");
        unsetenv("HTTPS_PROXY");
    }
}

static void a_fresh_page_needs_no_network(void) {
    Fixture();
    SavedPage(time(NULL) - 60);
    NoNetwork(true);
    static Club_Data data;
    CHECK_INT(Club_Load(&data, CLUB_LOAD_AUTO), CLUB_OK);
    CHECK(!data.from_cache); /* used because it is current, not as a fallback */
    CHECK_INT(data.pick_count, 2);
    NoNetwork(false);
}

static void a_stale_page_is_the_offline_fallback(void) {
    Fixture();
    SavedPage(time(NULL) - 7 * HOUR);
    NoNetwork(true);
    static Club_Data data;
    CHECK_INT(Club_Load(&data, CLUB_LOAD_AUTO), CLUB_OK);
    CHECK(data.from_cache);
    CHECK(data.age_seconds >= 7 * HOUR);
    NoNetwork(false);
}

static void refresh_always_asks(void) {
    Fixture();
    SavedPage(time(NULL) - 60);
    NoNetwork(true);
    static Club_Data data;
    /* Fresh, but Refresh asked anyway, failed, and fell back. */
    CHECK_INT(Club_Load(&data, CLUB_LOAD_CHECK), CLUB_OK);
    CHECK(data.from_cache);
    NoNetwork(false);
}

static void nothing_saved_and_offline(void) {
    Fixture();
    NoNetwork(true);
    static Club_Data data;
    CHECK_INT(Club_Load(&data, CLUB_LOAD_AUTO), CLUB_ERROR_NETWORK);
    NoNetwork(false);
}

int main(void) {
    RUN(the_countdown_counts);
    RUN(countdown_offsets_are_honoured);
    RUN(a_next_line_can_be_sooner);
    RUN(the_first_of_the_month_always_counts);
    RUN(times_already_past_are_ignored);
    RUN(a_page_without_dates);
    RUN(fresh_until_six_hours_or_the_next_change);
    RUN(a_clock_before_the_fetch_is_not_trusted);
    RUN(a_fresh_page_needs_no_network);
    RUN(a_stale_page_is_the_offline_fallback);
    RUN(refresh_always_asks);
    RUN(nothing_saved_and_offline);
    CHECK_DONE();
}

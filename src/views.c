#include <stdio.h>
#include <string.h>

#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "club.h"
#include "subtitle_list.h"
#include "views.h"

#define SCREEN_TITLE "Play Retro Games Club"
#define SCREEN_TITLE_MAX 96

/* Shared between the loading worker and the screens. Static because it is
   large (several KB per pick) and there is only ever one. */
static Club_Data g_data;
static Club_Result g_result;
/* Local box art per pick; empty when it could not be fetched. */
static char g_image_paths[CLUB_MAX_PICKS][512];

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void InfoView(const char *message) {
    ap_footer_item footer[] = {
        { .button = AP_BTN_A, .label = "OK", .is_confirm = true },
    };
    ap_message_opts opts = {
        .message = message,
        .footer = footer, .footer_count = 1,
    };
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

/* "5m", "2h", "3d": how far behind a saved page is. */
static void FormatAge(long seconds, char *out, size_t out_size) {
    if (seconds < 0) {
        snprintf(out, out_size, "?");
    } else if (seconds >= 24 * 60 * 60) {
        snprintf(out, out_size, "%ldd", seconds / (24 * 60 * 60));
    } else if (seconds >= 60 * 60) {
        snprintf(out, out_size, "%ldh", seconds / (60 * 60));
    } else if (seconds >= 60) {
        snprintf(out, out_size, "%ldm", seconds / 60);
    } else {
        snprintf(out, out_size, "<1m");
    }
}

/* The screen title, marked when the data is a saved copy so stale picks are
   never mistaken for current ones. */
static void FormatTitle(const char *title, char *out, size_t out_size) {
    if (!g_data.from_cache) {
        snprintf(out, out_size, "%s", title);
        return;
    }
    char age[16];
    FormatAge(g_data.age_seconds, age, sizeof(age));
    snprintf(out, out_size, "%s (saved %s ago)", title, age);
}

/* Box art comes in any shape (317x220, 230x320...) and the image section
   stretches to the size it is given, so scale the PNG's own size to fit a
   box instead. Reads only the IHDR header. */
static bool FitImage(const char *path, int *out_w, int *out_h) {
    unsigned char header[24];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(header, 1, sizeof(header), f) == sizeof(header);
    fclose(f);
    if (!ok || memcmp(header, "\x89PNG", 4) != 0 || memcmp(header + 12, "IHDR", 4) != 0) return false;

    long w = (long)header[16] << 24 | (long)header[17] << 16 | (long)header[18] << 8 | header[19];
    long h = (long)header[20] << 24 | (long)header[21] << 16 | (long)header[22] << 8 | header[23];
    if (w <= 0 || h <= 0) return false;

    long box = ap_scale(220);
    if (w >= h) {
        *out_w = (int)box;
        *out_h = (int)(box * h / w);
    } else {
        *out_h = (int)box;
        *out_w = (int)(box * w / h);
    }
    return *out_w > 0 && *out_h > 0;
}

/* ── Loading ─────────────────────────────────────────────────────────────── */

/* Runs on ap_process_message's worker thread: network and files only, no
   SDL. Box art is fetched here too, so opening a pick never waits. */
static int LoadWorker(void *userdata) {
    (void)userdata;
    g_result = Club_Load(&g_data);
    if (g_result != CLUB_OK) return 0;

    for (int i = 0; i < g_data.pick_count; i++) {
        if (!Club_FetchImage(g_data.picks[i].image_url, g_image_paths[i], sizeof(g_image_paths[i]))) {
            g_image_paths[i][0] = '\0';
        }
    }
    return 0;
}

/* Loads the page, explaining any failure. Returns whether there is data to
   show; on failure any data from an earlier load is left in place. */
static bool Load(void) {
    static Club_Data previous;
    previous = g_data;

    ap_process_opts opts = { .message = "Checking what the club is playing..." };
    ap_process_message(&opts, LoadWorker, NULL);

    switch (g_result) {
    case CLUB_OK:
        if (g_data.from_cache) {
            InfoView("No connection. Showing the games saved last time you checked.");
        }
        return true;
    case CLUB_ERROR_NETWORK:
        InfoView("Could not reach joeysretrohandhelds.com, and nothing is saved "
                 "yet. Check your Wi-Fi and try again.");
        break;
    case CLUB_ERROR_PARSE:
        InfoView("The club page loaded, but no games were found on it. The page "
                 "layout may have changed; look for an update to this Pak.");
        break;
    }

    g_data = previous;
    return g_data.pick_count > 0;
}

/* ── Views ───────────────────────────────────────────────────────────────── */

static void PickView(int index) {
    const Club_Pick *pick = &g_data.picks[index];

    ap_detail_info_pair info[2 + CLUB_MAX_STATS];
    int info_count = 0;
    info[info_count++] = (ap_detail_info_pair){ "Event", pick->event };
    if (pick->next[0]) info[info_count++] = (ap_detail_info_pair){ "Next", pick->next };
    for (int i = 0; i < pick->stat_count; i++) {
        info[info_count++] = (ap_detail_info_pair){ pick->stats[i].key, pick->stats[i].value };
    }

    /* The device cannot open a browser, but the addresses are still useful
       to read off and look up later. */
    ap_detail_info_pair links[2];
    int link_count = 0;
    if (pick->ra_url[0]) links[link_count++] = (ap_detail_info_pair){ "RetroAchievements", pick->ra_url };
    if (pick->guide_url[0]) links[link_count++] = (ap_detail_info_pair){ "Guide", pick->guide_url };

    ap_detail_section sections[4];
    int section_count = 0;

    int image_w, image_h;
    if (g_image_paths[index][0] && FitImage(g_image_paths[index], &image_w, &image_h)) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_IMAGE, .image_path = g_image_paths[index],
            .image_w = image_w, .image_h = image_h };
    }
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_INFO, .title = "This Round",
        .info_pairs = info, .info_count = info_count };
    if (pick->description[0]) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_DESCRIPTION, .title = "About",
            .description = pick->description };
    }
    if (link_count > 0) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_INFO, .title = "Links",
            .info_pairs = links, .info_count = link_count };
    }

    char title[SCREEN_TITLE_MAX];
    FormatTitle(pick->title, title, sizeof(title));

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
    };

    ap_detail_opts opts = {
        .title = title,
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .show_section_separator = true,
    };

    /* The widget always reports A; there is nothing to select here, so
       reopen rather than leaving on it. */
    for (;;) {
        ap_detail_result result;
        ap_detail_screen(&opts, &result);
        if (result.action != AP_DETAIL_ACTION) break;
    }
}

static void ActivityView(void) {
    if (g_data.activity_count == 0) {
        InfoView("No recent activity.");
        return;
    }

    ap_list_item items[CLUB_MAX_ACTIVITY];
    for (int i = 0; i < g_data.activity_count; i++) {
        items[i] = (ap_list_item){
            .label = g_data.activity[i].text,
            .trailing_text = g_data.activity[i].when,
        };
    }

    char title[SCREEN_TITLE_MAX];
    FormatTitle("Recent Activity", title, sizeof(title));

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
    };

    ap_list_opts opts = ap_list_default_opts(title, items, g_data.activity_count);
    opts.footer = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);

    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK) break;
        opts.initial_index = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        /* Long lines are cut off in the list; show the whole entry. */
        const Club_Activity *a = &g_data.activity[result.selected_index];
        char message[sizeof(a->text) + sizeof(a->event) + sizeof(a->when) + 8];
        snprintf(message, sizeof(message), "%s\n\n%s, %s", a->text, a->event, a->when);
        InfoView(message);
    }
}

void MainView(void) {
    if (!Load()) return;

    for (;;) {
        /* Rebuilt each pass, since Y can replace the data underneath. */
        SubtitleListItem items[CLUB_MAX_PICKS + 1];
        int count = 0;

        for (int i = 0; i < g_data.pick_count; i++) {
            items[count++] = (SubtitleListItem){
                .label = g_data.picks[i].title,
                .subtitle = g_data.picks[i].event,
            };
        }
        int activity_index = count;
        items[count++] = (SubtitleListItem){
            .label = "Recent Activity",
            .subtitle = "Who has beaten what lately",
        };

        char title[SCREEN_TITLE_MAX];
        FormatTitle(SCREEN_TITLE, title, sizeof(title));

        ap_footer_item footer[] = {
            { .button = AP_BTN_B, .label = "Quit" },
            { .button = AP_BTN_Y, .label = "Refresh" },
            { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
        };

        static int cursor = 0, scroll = 0;
        SubtitleListOpts opts = {
            .title = title,
            .items = items,
            .item_count = count,
            .footer = footer,
            .footer_count = sizeof(footer) / sizeof(footer[0]),
            .secondary_action_button = AP_BTN_Y,
            .initial_index = cursor < count ? cursor : 0,
            .visible_start_index = scroll,
        };

        ap_list_result result;
        if (SubtitleList(&opts, &result) != AP_OK) break;
        cursor = result.selected_index;
        scroll = result.visible_start_index;

        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            Load();
        } else if (result.action == AP_ACTION_SELECTED) {
            if (result.selected_index == activity_index) ActivityView();
            else if (result.selected_index >= 0) PickView(result.selected_index);
        }
    }
}

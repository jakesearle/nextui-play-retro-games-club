#include <stdio.h>
#include <string.h>

#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "club.h"
#include "launch.h"
#include "roms.h"
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
/* Set once Play has handed a game to launch.sh: every screen unwinds and
   the app exits, so the emulator can start. */
static bool g_launching = false;

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
    char age[24]; /* room for any long, so GCC sees it cannot truncate */
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
    long reuse_seconds = *(const long *)userdata;
    g_result = Club_Load(&g_data, reuse_seconds);
    if (g_result != CLUB_OK) return 0;

    for (int i = 0; i < g_data.pick_count; i++) {
        if (!Club_FetchImage(g_data.picks[i].image_url, g_image_paths[i], sizeof(g_image_paths[i]))) {
            g_image_paths[i][0] = '\0';
        }
    }
    return 0;
}

/* Loads the page, explaining any failure. A saved copy younger than
   reuse_seconds is used as is. Returns whether there is data to show; on
   failure any data from an earlier load is left in place. */
static bool Load(long reuse_seconds) {
    static Club_Data previous;
    previous = g_data;

    ap_process_opts opts = { .message = "Checking what the club is playing..." };
    ap_process_message(&opts, LoadWorker, &reuse_seconds);

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

/* ── Play ────────────────────────────────────────────────────────────────── */

typedef struct {
    int game_id;
    int console_id;
    const char *title;
    Roms_Match match;
    Roms_Result result;
    float progress;
    int cancel;
} FindJob;

/* Runs on ap_process_message's worker thread: files and network only. */
static int FindWorker(void *userdata) {
    FindJob *job = userdata;
    job->result = Roms_Find(job->game_id, job->console_id, job->title, &job->match,
                            &job->progress, &job->cancel);
    return 0;
}

/* Finds the pick on the device and hands it straight to launch.sh, which
   brings the player back here when the game closes. With resume set, the
   game loads the last save state the player used for it, as NextUI's
   menu does with X; a game without one just starts. Otherwise it starts
   fresh. */
static void Play(int index, bool resume) {
    const Club_Pick *pick = &g_data.picks[index];
    char message[512];

    char label[24];
    FindJob job = {
        .game_id = Roms_GameIdFromUrl(pick->ra_url),
        .console_id = Roms_ConsoleFromTitle(pick->title, label, sizeof(label)),
        .title = pick->title,
    };
    if (job.game_id <= 0) {
        InfoView("This pick has no RetroAchievements link, so there is nothing to "
                 "match your games against.");
        return;
    }
    if (job.console_id == 0) {
        snprintf(message, sizeof(message),
                 "This Pak does not know which console \"%s\" is yet.", label);
        InfoView(message);
        return;
    }

    snprintf(message, sizeof(message), "Looking for %s...", pick->title);
    ap_process_opts opts = {
        .message = message,
        .show_progress = true,
        .progress = &job.progress,
        .interrupt_signal = &job.cancel,
        .interrupt_button = AP_BTN_B,
    };
    ap_process_message(&opts, FindWorker, &job);

    switch (job.result) {
    case ROMS_FOUND:
        break;
    case ROMS_CANCELLED:
        return;
    case ROMS_NOT_FOUND:
        snprintf(message, sizeof(message),
                 "None of your %s games is a copy of %s that RetroAchievements "
                 "recognizes.", label, pick->title);
        InfoView(message);
        return;
    case ROMS_NO_FOLDER:
        snprintf(message, sizeof(message), "There is no %s folder in Roms on this device.", label);
        InfoView(message);
        return;
    case ROMS_NO_HASHES:
        InfoView("Could not download RetroAchievements' list of known games. Connect "
                 "to Wi-Fi once and it will be saved for next time.");
        return;
    case ROMS_UNKNOWN_CONSOLE:
        return;
    }

    const char *file = strrchr(job.match.rom_path, '/');
    file = file ? file + 1 : job.match.rom_path;
    if (!job.match.emu_launch[0]) {
        snprintf(message, sizeof(message),
                 "Found %s, but no emulator for it is installed.", file);
        InfoView(message);
        return;
    }

    int slot = LAUNCH_FRESH_SLOT;
    Launch_Resume state;
    if (resume && Launch_FindResume(job.match.rom_path, &state)) slot = state.slot;

    /* Best effort: without it the app just reopens at the top. */
    FILE *selection = fopen(LAUNCH_SELECTION_FILE, "w");
    if (selection) {
        fputs(pick->title, selection);
        fclose(selection);
    }

    if (!Launch_HandOff(&job.match, slot)) {
        InfoView("Could not hand the game over to the launcher.");
        return;
    }
    g_launching = true;
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
        { .button = AP_BTN_X, .label = "Resume" },
        { .button = AP_BTN_A, .label = "Play", .is_confirm = true },
    };

    ap_detail_opts opts = {
        .title = title,
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .show_section_separator = true,
    };

    /* A plays from the start, X resumes. Coming back from a search that
       found nothing reopens the screen, which the widget cannot do without
       starting at the top. (X needs the patch in subprojects/packagefiles:
       stock ap_detail_screen only reports A, B and Y.) */
    while (!g_launching) {
        ap_detail_result result;
        ap_detail_screen(&opts, &result);
        if (result.action == AP_DETAIL_ACTION) Play(index, false);
        else if (result.action == AP_DETAIL_TERTIARY_ACTION) Play(index, true);
        else break;
    }
}

/* The pick launch.sh last played, so coming back from a game lands on it.
   Read once: the file is removed so a later, ordinary start is unaffected. */
static int ReturningCursor(void) {
    char title[sizeof(g_data.picks[0].title)] = "";
    FILE *f = fopen(LAUNCH_SELECTION_FILE, "r");
    if (!f) return 0;
    if (!fgets(title, sizeof(title), f)) title[0] = '\0';
    fclose(f);
    remove(LAUNCH_SELECTION_FILE);

    for (int i = 0; i < g_data.pick_count; i++) {
        if (strcmp(g_data.picks[i].title, title) == 0) return i;
    }
    return 0;
}

void MainView(bool returning) {
    /* Coming back from a game, the page fetched just before it is still
       good, so the list reappears without a network round trip. */
    if (!Load(returning ? 15L * 60 : 0)) return;
    int cursor = returning ? ReturningCursor() : 0, scroll = 0;

    for (;;) {
        /* Rebuilt each pass, since Refresh can replace the data underneath. */
        SubtitleListItem items[CLUB_MAX_PICKS + 1];
        int count = 0;

        for (int i = 0; i < g_data.pick_count; i++) {
            items[count++] = (SubtitleListItem){
                .label = g_data.picks[i].title,
                .subtitle = g_data.picks[i].event,
            };
        }
        int refresh_index = count;
        items[count++] = (SubtitleListItem){
            .label = "Refresh",
            .subtitle = "Check the club page for new picks",
        };

        char title[SCREEN_TITLE_MAX];
        FormatTitle(SCREEN_TITLE, title, sizeof(title));

        ap_footer_item footer[] = {
            { .button = AP_BTN_B, .label = "Quit" },
            { .button = AP_BTN_X, .label = "Resume" },
            { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
        };

        SubtitleListOpts opts = {
            .title = title,
            .items = items,
            .item_count = count,
            .footer = footer,
            .footer_count = sizeof(footer) / sizeof(footer[0]),
            .secondary_action_button = AP_BTN_NONE,
            .tertiary_action_button = AP_BTN_X,
            .initial_index = cursor < count ? cursor : 0,
            .visible_start_index = scroll,
        };

        ap_list_result result;
        if (SubtitleList(&opts, &result) != AP_OK) break;
        cursor = result.selected_index;
        scroll = result.visible_start_index;

        bool on_pick = result.selected_index != refresh_index;
        if (result.action == AP_ACTION_TERTIARY_TRIGGERED) {
            if (on_pick) Play(result.selected_index, true);
        } else if (result.action == AP_ACTION_SELECTED) {
            if (on_pick) PickView(result.selected_index);
            else Load(0);
        }
        if (g_launching) break;
    }
}

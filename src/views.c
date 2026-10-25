#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "club.h"
#include "paths.h"
#include "qr.h"
#include "decide.h"
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

/* "Game of the Week October 25 - 31, 2026" becomes "Game of the Week |
   October 25-31, 2026", and "Game of the Season Q4 2026 (Oct 1 - Dec 31)"
   becomes "Game of the Season | Q4 2026": the event's name, then when it
   runs. The split is the first word that starts a date (a month, a number
   or a quarter); an event with none is left as it is. */
static void FormatEvent(const char *event, char *out, size_t out_size) {
    static const char *const months[] = {
        "January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December",
    };

    const char *period = NULL;
    for (const char *w = event; *w;) {
        while (*w == ' ') w++;
        size_t len = strcspn(w, " ");
        if (len == 0) break;
        bool starts_date = isdigit((unsigned char)w[0]) ||
                           (len == 2 && w[0] == 'Q' && w[1] >= '1' && w[1] <= '4');
        for (size_t m = 0; !starts_date && m < sizeof(months) / sizeof(months[0]); m++) {
            starts_date = strlen(months[m]) == len && strncmp(w, months[m], len) == 0;
        }
        if (starts_date && w != event) { period = w; break; }
        w += len;
    }
    if (!period) {
        snprintf(out, out_size, "%s", event);
        return;
    }

    /* The quarter's date range repeats what "Q4" already says. */
    char when[96];
    snprintf(when, sizeof(when), "%s", period);
    char *paren = strstr(when, " (");
    if (paren) *paren = '\0';

    /* "25 - 31" reads as two things; "25-31" as one range. */
    for (char *p = when; (p = strstr(p, " - ")) != NULL;) {
        if (p > when && isdigit((unsigned char)p[-1]) && isdigit((unsigned char)p[3])) {
            p[0] = '-';
            memmove(p + 1, p + 3, strlen(p + 3) + 1);
        }
        p++;
    }

    int name_len = (int)(period - event);
    while (name_len > 0 && event[name_len - 1] == ' ') name_len--;
    snprintf(out, out_size, "%.*s | %s", name_len, event, when);
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

    /* Three periods, not U+2026: NextUI's font draws that glyph raised. */
    ap_process_opts opts = { .message = "Loading..." };
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
    Roms_Matches matches;
    Roms_Result result;
    float progress;
    int cancel;
} FindJob;

/* Runs on ap_process_message's worker thread: files and network only. */
static int FindWorker(void *userdata) {
    FindJob *job = userdata;
    job->result = Roms_Find(job->game_id, job->console_id, job->title, &job->matches,
                            &job->progress, &job->cancel);
    return 0;
}

static const char *FileName(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Which of several copies of the game to play (decide.c has the rules),
   asking the player when the rules cannot tell. Returns an index into
   matches, or -1 if the player backed out. */
static int ChooseMatch(const Roms_Matches *matches) {
    Decide_CopyInfo copies[ROMS_MAX_MATCHES];
    for (int i = 0; i < matches->count; i++) {
        Launch_Resume state;
        copies[i] = (Decide_CopyInfo){
            .has_save_state = Launch_FindResume(matches->items[i].rom_path, &state),
            .recent_rank = Launch_RecentRank(matches->items[i].rom_path),
        };
    }
    int left[ROMS_MAX_MATCHES], count = 0;
    int chosen = Decide_Copy(copies, matches->count, left, &count);
    if (chosen >= 0) return chosen;

    /* Nothing to go on: ask, showing each copy's folder, since copies can
       share a name across folders. Once one is played it is in Recently
       Played, so this comes up once per game. */
    char root[512] = "";
    Roms_RootPath(root, sizeof(root));
    size_t root_len = strlen(root);
    static char folders[ROMS_MAX_MATCHES][512];
    SubtitleListItem items[ROMS_MAX_MATCHES];
    for (int i = 0; i < count; i++) {
        const char *path = matches->items[left[i]].rom_path;
        const char *rel = strncmp(path, root, root_len) == 0 && path[root_len] == '/'
                        ? path + root_len + 1 : path;
        snprintf(folders[i], sizeof(folders[i]), "%.*s",
                 (int)(FileName(rel) - rel > 0 ? FileName(rel) - rel - 1 : 0), rel);
        items[i] = (SubtitleListItem){ .label = FileName(path), .subtitle = folders[i] };
    }

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Play", .is_confirm = true },
    };
    SubtitleListOpts opts = {
        .title = "Which copy?",
        .items = items,
        .item_count = count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .secondary_action_button = AP_BTN_NONE,
        .tertiary_action_button = AP_BTN_NONE,
    };
    ap_list_result result;
    if (SubtitleList(&opts, &result) != AP_OK || result.action != AP_ACTION_SELECTED) return -1;
    return left[result.selected_index];
}

/* For a console with no emulator installed: where to get one, on a line of
   its own, or "" if there is nowhere. */
static void PakStoreHint(int console_id, char *out, size_t out_size) {
    const char *pak = Roms_PakStoreEmulator(console_id);
    if (pak) snprintf(out, out_size, "\nTry downloading the \"%s\" Pak from the Pak Store.", pak);
    else if (out_size) out[0] = '\0';
}

/* Finds the pick on the device and hands it straight to launch.sh, which
   brings the player back here when the game closes. With resume set, the
   game loads the last save state the player used for it, as NextUI's
   menu does with X; a game without one just starts. Otherwise it starts
   fresh. */
static void Play(int index, bool resume) {
    const Club_Pick *pick = &g_data.picks[index];
    char message[640], hint[128];

    char label[24];
    static FindJob job;
    job = (FindJob){
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
    const char *console = Roms_ConsoleName(job.console_id);
    if (!console) console = label;
    PakStoreHint(job.console_id, hint, sizeof(hint));

    /* The console is implied by the screen it was picked from. */
    char name[sizeof(pick->title)];
    snprintf(name, sizeof(name), "%s", pick->title);
    char *label_open = strrchr(name, '(');
    if (label_open && label_open > name && label_open[-1] == ' ' && strchr(label_open, ')')) {
        label_open[-1] = '\0';
    }
    snprintf(message, sizeof(message), "Looking for %s...", name);
    ap_process_opts opts = {
        .message = message,
        .show_progress = true,
        .progress = &job.progress,
        .interrupt_signal = &job.cancel,
        .interrupt_button = AP_BTN_B,
    };
    ap_process_message(&opts, FindWorker, &job);

    /* Which copy, if any were found; the outcome below depends on its
       emulator. */
    const Roms_Match *match = NULL;
    if (job.result == ROMS_FOUND) {
        int chosen = ChooseMatch(&job.matches);
        if (chosen < 0) return;
        match = &job.matches.items[chosen];
    }

    switch (Decide_ForResult(job.result, job.console_id, match && match->emu_launch[0])) {
    case DECIDE_LAUNCH:
        break;
    case DECIDE_SILENT:
        return;
    case DECIDE_NO_MATCH:
        snprintf(message, sizeof(message),
                 "None of your %s games is a copy of %s that RetroAchievements "
                 "recognizes.", console, pick->title);
        InfoView(message);
        return;
    case DECIDE_NO_GAMES:
        snprintf(message, sizeof(message), "You don't have any %s games on this device yet.",
                 console);
        InfoView(message);
        return;
    case DECIDE_MISSING_FOLDER:
        snprintf(message, sizeof(message), "Couldn't find the \"%s\" folder in Roms.",
                 Roms_NextUIFolder(job.console_id));
        InfoView(message);
        return;
    case DECIDE_GET_EMULATOR:
    case DECIDE_NO_FOLDER:
        snprintf(message, sizeof(message), "There is no %s folder in Roms on this device.%s",
                 console, hint);
        InfoView(message);
        return;
    case DECIDE_CANT_PLAY:
        snprintf(message, sizeof(message), "NextUI can't play %s games.", console);
        InfoView(message);
        return;
    case DECIDE_NO_HASH_LIST:
        InfoView("Could not download RetroAchievements' list of known games. Connect "
                 "to Wi-Fi once and it will be saved for next time.");
        return;
    case DECIDE_NO_EMULATOR:
        snprintf(message, sizeof(message), "Found %s, but no emulator for it is installed.%s",
                 FileName(match->rom_path), hint);
        InfoView(message);
        return;
    }

    int slot = LAUNCH_FRESH_SLOT;
    Launch_Resume state;
    if (resume && Launch_FindResume(match->rom_path, &state)) slot = state.slot;

    /* Best effort: without it the app just reopens at the top. */
    FILE *selection = fopen(LAUNCH_SELECTION_FILE, "w");
    if (selection) {
        fputs(pick->title, selection);
        fclose(selection);
    }

    if (!Launch_HandOff(match, slot)) {
        InfoView("Could not hand the game over to the launcher.");
        return;
    }
    g_launching = true;
}

/* ── Views ───────────────────────────────────────────────────────────────── */

/* Draws a pick's links as QR codes into the image cache, named by what
   they encode and how big, so a pick's codes are drawn once per size. */
static bool LinksImage(const Qr_Link *links, int count, char *out, size_t out_size,
                       int *out_w, int *out_h) {
    /* About a third of the screen across each: two fit side by side with
       room between, and a phone reads them from arm's length. */
    int qr_size = ap_get_screen_width() * 3 / 10;
    int gap = qr_size / 4;

    unsigned long hash = 5381;
    for (int i = 0; i < count; i++) {
        for (const char *p = links[i].caption; *p; p++) hash = hash * 33 + (unsigned char)*p;
        for (const char *p = links[i].url; *p; p++) hash = hash * 33 + (unsigned char)*p;
    }
    hash = hash * 33 + (unsigned long)qr_size;

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images") || !Paths_MakeDirs(dir)) return false;
    int written = snprintf(out, out_size, "%s/links-%08lx.bmp", dir, hash & 0xFFFFFFFFul);
    if (written < 0 || (size_t)written >= out_size) return false;

    ap_color text = ap_get_theme()->text;
    SDL_Color color = { text.r, text.g, text.b, text.a };
    return Qr_RenderLinks(links, count, ap_get_font(AP_FONT_TINY), color, qr_size, gap,
                          out, out_w, out_h);
}

static void PickView(int index) {
    const Club_Pick *pick = &g_data.picks[index];

    /* The event and what comes next read as plain lines; only the stats
       are label and value. */
    char event[128], when[sizeof(event) + sizeof(pick->next) + 2];
    FormatEvent(pick->event, event, sizeof(event));
    snprintf(when, sizeof(when), "%s%s%s", event, pick->next[0] ? "\n" : "", pick->next);

    /* The page's award counts, in plainer words. RetroAchievements'
       "completed" is every achievement in softcore and "mastered" every
       one in hardcore, so the softcore 100% is listed first. */
    static const struct { const char *key; const char *label; } awards[] = {
        { "Beaten", "Beaten" },
        { "Completed", "100%'d" },
        { "Mastered", "100%'d (Hardcore)" },
    };
    static char award_values[CLUB_MAX_STATS][32];
    ap_detail_info_pair info[CLUB_MAX_STATS];
    int info_count = 0;

    /* Everything but the awards keeps the page's order; the awards go
       where the first of them was, in the order above. */
    int award_at = -1;
    for (int i = 0; i < pick->stat_count; i++) {
        bool is_award = false;
        for (size_t a = 0; a < sizeof(awards) / sizeof(awards[0]); a++) {
            is_award = is_award || strcmp(pick->stats[i].key, awards[a].key) == 0;
        }
        if (!is_award) {
            info[info_count++] = (ap_detail_info_pair){ pick->stats[i].key, pick->stats[i].value };
        } else if (award_at < 0) {
            award_at = info_count;
        }
    }
    if (award_at >= 0) {
        ap_detail_info_pair rows[sizeof(awards) / sizeof(awards[0])];
        int row_count = 0;
        for (size_t a = 0; a < sizeof(awards) / sizeof(awards[0]); a++) {
            for (int i = 0; i < pick->stat_count; i++) {
                if (strcmp(pick->stats[i].key, awards[a].key) != 0) continue;
                const char *count = pick->stats[i].value;
                snprintf(award_values[row_count], sizeof(award_values[row_count]), "%s %s",
                         count, strcmp(count, "1") == 0 ? "member" : "members");
                rows[row_count] = (ap_detail_info_pair){ awards[a].label, award_values[row_count] };
                row_count++;
                break;
            }
        }
        memmove(&info[award_at + row_count], &info[award_at],
                (size_t)(info_count - award_at) * sizeof(info[0]));
        memcpy(&info[award_at], rows, (size_t)row_count * sizeof(rows[0]));
        info_count += row_count;
    }

    /* The device cannot open a browser, so links are QR codes to scan
       with a phone. */
    Qr_Link links[2];
    int link_count = 0;
    if (pick->ra_url[0]) links[link_count++] = (Qr_Link){ "RetroAchievements", pick->ra_url };
    if (pick->guide_url[0]) links[link_count++] = (Qr_Link){ "Guide", pick->guide_url };
    char links_image[512];
    int links_w = 0, links_h = 0;
    bool has_links = link_count > 0 &&
                     LinksImage(links, link_count, links_image, sizeof(links_image), &links_w, &links_h);

    ap_detail_section sections[5];
    int section_count = 0;

    int image_w, image_h;
    if (g_image_paths[index][0] && FitImage(g_image_paths[index], &image_w, &image_h)) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_IMAGE, .image_path = g_image_paths[index],
            .image_w = image_w, .image_h = image_h };
    }
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_DESCRIPTION, .description = when };
    if (info_count > 0) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_INFO, .info_pairs = info, .info_count = info_count };
    }
    if (has_links) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_IMAGE, .title = "Links", .image_path = links_image,
            .image_w = links_w, .image_h = links_h };
    }
    if (pick->description[0]) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_DESCRIPTION, .title = "About",
            .description = pick->description };
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
        static char events[CLUB_MAX_PICKS][128];
        int count = 0;

        for (int i = 0; i < g_data.pick_count; i++) {
            FormatEvent(g_data.picks[i].event, events[i], sizeof(events[i]));
            items[count++] = (SubtitleListItem){
                .label = g_data.picks[i].title,
                .subtitle = events[i],
            };
        }
        int refresh_index = count;
        items[count++] = (SubtitleListItem){ .label = "Refresh" };

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

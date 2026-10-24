/* Runs the real scraper without the UI, for checking the parser against the
   live page or a saved copy:

     task scrape                  # fetch live (and save to the cache)
     task scrape FILE=page.html   # parse a saved page
*/

#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "club.h"

static char *ReadAll(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)size + 1);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        data[size] = '\0';
        *len = (size_t)size;
    }
    return data;
}

int main(int argc, char **argv) {
    static Club_Data data;
    Club_Result result;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (argc > 1) {
        size_t len = 0;
        char *html = ReadAll(argv[1], &len);
        if (!html) {
            perror(argv[1]);
            return 2;
        }
        result = Club_Parse(html, len, &data);
        free(html);
    } else {
        result = Club_Load(&data);
    }

    printf("result=%d picks=%d activity=%d from_cache=%d age=%lds\n",
           result, data.pick_count, data.activity_count, data.from_cache, data.age_seconds);

    for (int i = 0; i < data.pick_count; i++) {
        const Club_Pick *p = &data.picks[i];
        printf("\n[%s]\n  title: %s\n  next:  %s\n  image: %s\n  ra:    %s\n  guide: %s\n",
               p->event, p->title, p->next, p->image_url, p->ra_url, p->guide_url);
        for (int s = 0; s < p->stat_count; s++) {
            printf("  %-13s %s\n", p->stats[s].key, p->stats[s].value);
        }
        printf("  desc:  %s\n", p->description);
    }

    printf("\nActivity:\n");
    for (int i = 0; i < data.activity_count; i++) {
        const Club_Activity *a = &data.activity[i];
        printf("  %-60s | %-20s | %s\n", a->text, a->event, a->when);
    }

    curl_global_cleanup();
    return result == CLUB_OK ? 0 : 1;
}

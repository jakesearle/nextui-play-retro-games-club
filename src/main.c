#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include <string.h>
#include <curl/curl.h>

#include "subtitle_list.h"
#include "views.h"

int App_DeviceScaled(int base) {
    return AP_DS(base);
}

int App_DevicePadding(void) {
    return ap__g.device_padding;
}

int main(int argc, char **argv) {
    ap_config cfg = {
        .window_title = "PRGC Tracker",
        .font_path    = AP_PLATFORM_IS_DEVICE ? NULL : "font.ttf",
        .is_nextui    = AP_PLATFORM_IS_DEVICE,
    };
    ap_init(&cfg);

    /* Once, before the loading worker makes its first request. */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* launch.sh reopens the app with --returning once a game it started is
       closed. */
    bool returning = argc > 1 && strcmp(argv[1], "--returning") == 0;
    MainView(returning);

    curl_global_cleanup();
    ap_quit();
    return 0;
}

#define IO_USER_AGENT "nextui-prgc-tracker/1.0.0"
/* Generous for a handheld: its Wi-Fi can take a while to wake, and a cold
   DNS lookup counts against this too. */
#define IO_CONNECT_TIMEOUT_SECONDS 10L
/* A timed-out request is tried once more: the first often only wakes the
   network. Anything else (no route, a refused connection, Wi-Fi off) fails
   at once and is not retried. */
#define IO_ATTEMPTS 2
#define IO_TIMEOUT_SECONDS 30L
/* The largest thing fetched is a console's hash library (~1 MB for the
   biggest); anything far beyond that is not what was asked for. */
#define IO_MAX_BYTES (16L * 1024 * 1024)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <curl/curl.h>

#include "io.h"

static size_t Append(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t chunk = size * nmemb;
    Io_Buffer *buffer = (Io_Buffer *)userdata;
    if (buffer->size + chunk > (size_t)IO_MAX_BYTES) return 0;

    char *grown = realloc(buffer->data, buffer->size + chunk + 1);
    if (!grown) return 0;

    buffer->data = grown;
    memcpy(buffer->data + buffer->size, ptr, chunk);
    buffer->size += chunk;
    buffer->data[buffer->size] = '\0';
    return chunk;
}

typedef struct {
    char *etag;
    size_t size;
} EtagCapture;

/* Keeps the ETag response header, quotes and all, as the server sent it. */
static size_t OnHeader(char *line, size_t size, size_t nitems, void *userdata) {
    size_t len = size * nitems;
    EtagCapture *capture = userdata;
    if (capture->etag && capture->size && len > 5 && strncasecmp(line, "etag:", 5) == 0) {
        const char *value = line + 5;
        size_t n = len - 5;
        while (n && (*value == ' ' || *value == '\t')) { value++; n--; }
        while (n && (value[n - 1] == '\r' || value[n - 1] == '\n' || value[n - 1] == ' ')) n--;
        if (n >= capture->size) n = capture->size - 1;
        memcpy(capture->etag, value, n);
        capture->etag[n] = '\0';
    }
    return len;
}

/* One attempt. *timed_out says whether it is worth another. */
static bool HttpGetOnce(const char *url, const char *etag, Io_Buffer *buffer, EtagCapture *capture,
                        bool *not_modified, bool *timed_out) {
    *timed_out = false;
    *not_modified = false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist *headers = NULL;
    if (etag && etag[0]) {
        char header[300];
        snprintf(header, sizeof(header), "If-None-Match: %s", etag);
        headers = curl_slist_append(headers, header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, OnHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, capture);

    char error[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, IO_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, IO_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, IO_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Append);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);

    /* curl is statically linked against our own OpenSSL on device, which
       knows no system trust store; launch.sh points this at cacert.pem. */
    const char *ca_bundle = getenv("SSL_CERT_FILE");
    if (ca_bundle && ca_bundle[0]) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    double lookup = 0, connect = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &lookup);
    curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    /* curl's own message says which step failed ("Resolving timed out..."
       vs "Connection timed out..."), which curl_easy_strerror does not. */
    if (code != CURLE_OK) {
        fprintf(stderr, "GET %s: %s (dns %.2fs, connect %.2fs)\n", url,
                error[0] ? error : curl_easy_strerror(code), lookup, connect);
        *timed_out = code == CURLE_OPERATION_TIMEDOUT;
    } else if (status == 304 && etag && etag[0]) {
        fprintf(stderr, "GET %s: not modified\n", url);
        *not_modified = true;
        return true;
    } else if (status != 200) {
        fprintf(stderr, "GET %s: HTTP %ld\n", url, status);
    }
    return code == CURLE_OK && status == 200;
}

bool Io_HttpGet(const char *url, Io_Buffer *buffer) {
    bool not_modified;
    return Io_HttpGetIfChanged(url, NULL, buffer, NULL, 0, &not_modified);
}

bool Io_HttpGetIfChanged(const char *url, const char *etag, Io_Buffer *buffer,
                         char *etag_out, size_t etag_size, bool *not_modified) {
    EtagCapture capture = { etag_out, etag_size };
    if (etag_out && etag_size) etag_out[0] = '\0';
    for (int attempt = 1; attempt <= IO_ATTEMPTS; attempt++) {
        bool timed_out;
        if (HttpGetOnce(url, etag, buffer, &capture, not_modified, &timed_out)) return true;
        if (!timed_out || attempt == IO_ATTEMPTS) break;
        /* Start the retry from nothing, not from whatever arrived. */
        free(buffer->data);
        buffer->data = NULL;
        buffer->size = 0;
        fprintf(stderr, "GET %s: retrying\n", url);
    }
    return false;
}

bool Io_WriteFileAtomic(const char *path, const char *data, size_t len) {
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

char *Io_ReadFile(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    Io_Buffer buffer = { 0 };
    char chunk[16384];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (Append(chunk, 1, n, &buffer) != n) {
            free(buffer.data);
            fclose(f);
            return NULL;
        }
    }
    fclose(f);

    /* An empty file still reads as an empty string, not a failure. */
    if (!buffer.data) buffer.data = calloc(1, 1);
    if (buffer.data) *out_len = buffer.size;
    return buffer.data;
}

long Io_FileAge(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    return (long)(time(NULL) - info.st_mtime);
}

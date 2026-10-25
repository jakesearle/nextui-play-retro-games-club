#ifndef CHECK_H
#define CHECK_H

/* Just enough of a test harness for `meson test`: each test program runs
   its cases with RUN(), counts failures, and exits non-zero if any failed.
   Fixtures live in a fresh temporary directory per case. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int check_failures = 0;
static const char *check_case = "";

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s (%s:%d): %s\n", check_case, __FILE__, __LINE__, #cond); \
        check_failures++; \
    } \
} while (0)

#define CHECK_INT(actual, expected) do { \
    long long check_a = (long long)(actual), check_e = (long long)(expected); \
    if (check_a != check_e) { \
        fprintf(stderr, "FAIL %s (%s:%d): %s == %lld, expected %lld\n", \
                check_case, __FILE__, __LINE__, #actual, check_a, check_e); \
        check_failures++; \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    const char *check_a = (actual), *check_e = (expected); \
    if (!check_a || !check_e || strcmp(check_a, check_e) != 0) { \
        fprintf(stderr, "FAIL %s (%s:%d): %s == \"%s\", expected \"%s\"\n", \
                check_case, __FILE__, __LINE__, #actual, \
                check_a ? check_a : "(null)", check_e ? check_e : "(null)"); \
        check_failures++; \
    } \
} while (0)

#define RUN(fn) do { check_case = #fn; fn(); } while (0)

#define CHECK_DONE() do { \
    if (check_failures) fprintf(stderr, "%d check(s) failed\n", check_failures); \
    return check_failures ? 1 : 0; \
} while (0)

/* ── Fixtures ─────────────────────────────────────────────────────────────── */

static char check_root[512];

/* A new, empty directory for this case, and the environment the code under
   test reads, pointed into it: an SD card at <root>/sd and NextUI's shared
   userdata at <root>/sd/.userdata/shared. */
static inline const char *Fixture(void) {
    const char *base = getenv("TMPDIR");
    snprintf(check_root, sizeof(check_root), "%s/prgc-test-XXXXXX", base && base[0] ? base : "/tmp");
    if (!mkdtemp(check_root)) {
        perror("mkdtemp");
        exit(2);
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/sd", check_root);
    setenv("SDCARD_PATH", path, 1);
    snprintf(path, sizeof(path), "%s/sd/.userdata/shared", check_root);
    setenv("SHARED_USERDATA_PATH", path, 1);
    snprintf(path, sizeof(path), "%s/sd/.system/h700", check_root);
    setenv("SYSTEM_PATH", path, 1);
    setenv("PLATFORM", "h700", 1);
    unsetenv("ROMS_PATH");
    return check_root;
}

/* <root>/<rel>, in one of a few rotating buffers so several can be held. */
static inline const char *At(const char *rel) {
    static char bufs[8][768];
    static int next = 0;
    char *buf = bufs[next++ % 8];
    snprintf(buf, sizeof(bufs[0]), "%s/%s", check_root, rel);
    return buf;
}

static inline void MakeDirs(const char *path) {
    char partial[768];
    snprintf(partial, sizeof(partial), "%s", path);
    for (char *p = partial + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(partial, 0755);
        *p = '/';
    }
    mkdir(partial, 0755);
}

/* Writes len bytes to <root>/<rel>, creating its folders. */
static inline void WriteBytes(const char *rel, const void *data, size_t len) {
    const char *path = At(rel);
    char dir[768];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; MakeDirs(dir); }
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, len, f) != len) { perror(path); exit(2); }
    fclose(f);
}

static inline void WriteText(const char *rel, const char *text) {
    WriteBytes(rel, text, strlen(text));
}

/* Deterministic filler, different per seed, so every fake ROM hashes
   differently without depending on a random source. */
static inline void Filler(unsigned char *buf, size_t len, unsigned seed) {
    unsigned x = seed * 2654435761u + 1;
    for (size_t i = 0; i < len; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (unsigned char)(x >> 16);
    }
}

static inline char *ReadText(const char *rel) {
    FILE *f = fopen(At(rel), "rb");
    if (!f) return NULL;
    static char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

#endif

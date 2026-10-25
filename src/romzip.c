/* Signatures and fixed record sizes from the PKWARE APPNOTE. */
#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_CENTRAL_SIG 0x02014b50u
#define ZIP_LOCAL_SIG 0x04034b50u
#define ZIP_EOCD_SIZE 22
#define ZIP_CENTRAL_SIZE 46
#define ZIP_LOCAL_SIZE 30
/* The end record sits at most a 64 KB comment away from the end. */
#define ZIP_EOCD_SEARCH (ZIP_EOCD_SIZE + 0xFFFF)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "romzip.h"

static uint32_t Le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t Le32(const uint8_t *p) { return Le16(p) | Le16(p + 2) << 16; }

/* Entries a ROM zip may carry alongside the ROM. */
static bool IsRomName(const char *name) {
    static const char *const skip[] = { "txt", "nfo", "diz", "xml", "dat", "jpg", "png", "pdf", "url" };
    size_t len = strlen(name);
    if (len == 0 || name[len - 1] == '/') return false; /* folder */
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (base[0] == '.' || strncmp(name, "__MACOSX/", 9) == 0) return false;
    const char *dot = strrchr(base, '.');
    if (!dot) return true;
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
        if (strcasecmp(dot + 1, skip[i]) == 0) return false;
    }
    return true;
}

static bool ReadAt(FILE *f, long offset, void *buf, size_t len) {
    return fseek(f, offset, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

/* Inflates raw deflate data of known sizes. */
static bool Inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
    z.next_in = (Bytef *)in;
    z.avail_in = (uInt)in_size;
    z.next_out = out;
    z.avail_out = (uInt)out_size;
    int rc = inflate(&z, Z_FINISH);
    bool ok = rc == Z_STREAM_END && z.total_out == out_size;
    inflateEnd(&z);
    return ok;
}

uint8_t *RomZip_ExtractFirstRom(const char *path, size_t max_size, size_t *out_size,
                                char *name_out, size_t name_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t *result = NULL;
    uint8_t *tail = NULL, *central = NULL, *packed = NULL;

    if (fseek(f, 0, SEEK_END) != 0) goto done;
    long file_size = ftell(f);
    if (file_size < ZIP_EOCD_SIZE) goto done;

    /* Find the end-of-central-directory record, scanning back from the end. */
    long tail_size = file_size < ZIP_EOCD_SEARCH ? file_size : ZIP_EOCD_SEARCH;
    tail = malloc((size_t)tail_size);
    if (!tail || !ReadAt(f, file_size - tail_size, tail, (size_t)tail_size)) goto done;
    const uint8_t *eocd = NULL;
    for (long i = tail_size - ZIP_EOCD_SIZE; i >= 0; i--) {
        if (Le32(tail + i) == ZIP_EOCD_SIG) { eocd = tail + i; break; }
    }
    if (!eocd) goto done;

    uint32_t entries = Le16(eocd + 10);
    uint32_t dir_size = Le32(eocd + 12);
    uint32_t dir_offset = Le32(eocd + 16);
    if (dir_offset == 0xFFFFFFFFu || (long)dir_offset + (long)dir_size > file_size) goto done;

    central = malloc(dir_size ? dir_size : 1);
    if (!central || !ReadAt(f, (long)dir_offset, central, dir_size)) goto done;

    /* Walk the central directory, which has the real sizes even when the
       local headers defer them to a data descriptor. */
    const uint8_t *p = central, *end = central + dir_size;
    for (uint32_t e = 0; e < entries && p + ZIP_CENTRAL_SIZE <= end; e++) {
        if (Le32(p) != ZIP_CENTRAL_SIG) break;
        uint32_t flags = Le16(p + 8);
        uint32_t method = Le16(p + 10);
        uint32_t comp_size = Le32(p + 20);
        uint32_t size = Le32(p + 24);
        uint32_t name_len = Le16(p + 28);
        uint32_t extra_len = Le16(p + 30);
        uint32_t comment_len = Le16(p + 32);
        uint32_t local_offset = Le32(p + 42);
        if (p + ZIP_CENTRAL_SIZE + name_len > end) break;

        char name[512];
        size_t n = name_len < sizeof(name) - 1 ? name_len : sizeof(name) - 1;
        memcpy(name, p + ZIP_CENTRAL_SIZE, n);
        name[n] = '\0';
        p += ZIP_CENTRAL_SIZE + name_len + extra_len + comment_len;

        if (!IsRomName(name)) continue;

        /* Only the first ROM-like entry counts, as in minarch; anything we
           cannot read means this zip is not one we can hash. */
        if ((flags & 1) || (method != 0 && method != 8) || size > max_size) goto done;

        uint8_t local[ZIP_LOCAL_SIZE];
        if (!ReadAt(f, (long)local_offset, local, sizeof(local)) || Le32(local) != ZIP_LOCAL_SIG) goto done;
        long data_offset = (long)local_offset + ZIP_LOCAL_SIZE + Le16(local + 26) + Le16(local + 28);
        if (data_offset + (long)comp_size > file_size) goto done;

        result = malloc(size ? size : 1);
        if (!result) goto done;
        bool ok;
        if (method == 0) {
            ok = comp_size == size && ReadAt(f, data_offset, result, size);
        } else {
            packed = malloc(comp_size ? comp_size : 1);
            ok = packed && ReadAt(f, data_offset, packed, comp_size) &&
                 Inflate(packed, comp_size, result, size);
        }
        if (!ok) {
            free(result);
            result = NULL;
            goto done;
        }
        *out_size = size;
        if (name_size) snprintf(name_out, name_size, "%s", name);
        goto done;
    }

done:
    free(packed);
    free(central);
    free(tail);
    fclose(f);
    return result;
}

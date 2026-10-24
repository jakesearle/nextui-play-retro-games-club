#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "html.h"

/* Bounded strstr: memmem is not declared without _GNU_SOURCE on the device
   toolchain, and this is too small to be worth the feature macro. */
static const char *Find(const char *s, const char *end, const char *needle) {
    size_t n = strlen(needle);
    for (; s && s + n <= end; s++) {
        if (*s == needle[0] && memcmp(s, needle, n) == 0) return s;
    }
    return NULL;
}

static bool IsNameChar(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* Reads the tag name after '<' (or "</") into out. */
static size_t TagName(const char *p, const char *end, char *out, size_t out_size) {
    size_t n = 0;
    while (p + n < end && IsNameChar(p[n]) && n + 1 < out_size) {
        out[n] = (char)tolower((unsigned char)p[n]);
        n++;
    }
    out[n] = '\0';
    return n;
}

/* The '>' closing the tag that starts at p, or end. Quoted attribute values
   may contain '>', so quotes are skipped over. */
static const char *TagEnd(const char *p, const char *end) {
    char quote = 0;
    for (; p < end; p++) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
    }
    return end;
}

static bool IsSelfClosing(const char *tag_end) {
    return tag_end[-1] == '/';
}

static bool IsVoid(const char *name) {
    static const char *const voids[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "source", "track", "wbr",
    };
    for (size_t i = 0; i < sizeof(voids) / sizeof(voids[0]); i++) {
        if (strcmp(name, voids[i]) == 0) return true;
    }
    return false;
}

/* Just past the tag that closes the element opening at elem, counting nested
   elements of the same name. An unclosed element runs to end. */
static const char *ElementEnd(const char *elem, const char *end) {
    char name[16];
    if (TagName(elem + 1, end, name, sizeof(name)) == 0) return end;

    const char *open_end = TagEnd(elem, end);
    if (open_end >= end) return end;
    if (IsVoid(name) || IsSelfClosing(open_end)) return open_end + 1;

    size_t len = strlen(name);
    int depth = 1;
    const char *p = open_end + 1;
    while ((p = Find(p, end, "<")) != NULL) {
        bool closing = p + 1 < end && p[1] == '/';
        const char *n = p + (closing ? 2 : 1);
        if (n + len < end && strncasecmp(n, name, len) == 0 && !IsNameChar(n[len])) {
            const char *tag_end = TagEnd(p, end);
            if (closing) {
                if (--depth == 0) return tag_end < end ? tag_end + 1 : end;
            } else if (!IsSelfClosing(tag_end)) {
                depth++;
            }
            p = tag_end;
        } else {
            p++;
        }
    }
    return end;
}

bool Html_FindClass(Html_Span span, const char *cls, Html_Span *out) {
    size_t cls_len = strlen(cls);
    const char *p = span.start;

    while ((p = Find(p, span.end, "class=\"")) != NULL) {
        const char *value = p + 7;
        const char *value_end = Find(value, span.end, "\"");
        if (!value_end) return false;

        /* Whole tokens only, so "jrh-pick-card" does not match
           "jrh-pick-card__body". */
        for (const char *t = value; t < value_end;) {
            while (t < value_end && *t == ' ') t++;
            const char *token_end = t;
            while (token_end < value_end && *token_end != ' ') token_end++;

            if ((size_t)(token_end - t) == cls_len && memcmp(t, cls, cls_len) == 0) {
                const char *elem = p;
                while (elem > span.start && *elem != '<') elem--;
                out->start = elem;
                out->end = ElementEnd(elem, span.end);
                return true;
            }
            t = token_end;
        }
        p = value_end;
    }
    return false;
}

/* Appends a code point as UTF-8. */
static size_t PutUtf8(unsigned long cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Decodes the entity at p ('&'). Writes up to 4 bytes to buf and returns how
   many, setting *consumed; returns 0 when p is not a recognised entity, in
   which case the '&' is kept as literal text. */
static size_t DecodeEntity(const char *p, const char *end, char *buf, size_t *consumed) {
    static const struct { const char *name; unsigned long cp; } named[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' },
        { "apos", '\'' }, { "nbsp", ' ' }, { "hellip", 0x2026 },
        { "middot", 0xB7 }, { "rarr", 0x2192 }, { "larr", 0x2190 },
        { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 },
        { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
        { "eacute", 0xE9 }, { "copy", 0xA9 }, { "trade", 0x2122 },
    };

    const char *semi = NULL;
    for (const char *q = p + 1; q < end && q < p + 12; q++) {
        if (*q == ';') { semi = q; break; }
    }
    if (!semi) return 0;
    *consumed = (size_t)(semi - p) + 1;

    if (p[1] == '#') {
        bool hex = p[2] == 'x' || p[2] == 'X';
        char *num_end;
        unsigned long cp = strtoul(p + (hex ? 3 : 2), &num_end, hex ? 16 : 10);
        if (num_end != semi || cp == 0 || cp > 0x10FFFF) return 0;
        /* Variation selectors only follow emoji; drop them with it. */
        if (cp == 0xFE0F || cp == 0xFE0E) return (size_t)-1;
        if (cp == 0xA0) cp = ' ';
        return PutUtf8(cp, buf);
    }

    size_t name_len = (size_t)(semi - p) - 1;
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (strlen(named[i].name) == name_len && memcmp(p + 1, named[i].name, name_len) == 0) {
            return PutUtf8(named[i].cp, buf);
        }
    }
    return 0;
}

/* Accumulates text with whitespace collapsed, never splitting a UTF-8
   sequence when the buffer runs out. */
typedef struct {
    char *out;
    size_t size;
    size_t len;
    bool pending_space;
    bool full;
} TextBuf;

static void PutBytes(TextBuf *b, const char *bytes, size_t n) {
    if (b->full) return;
    if (b->pending_space && b->len > 0) {
        if (b->len + 1 >= b->size) { b->full = true; return; }
        b->out[b->len++] = ' ';
    }
    b->pending_space = false;
    if (b->len + n >= b->size) { b->full = true; return; }
    memcpy(b->out + b->len, bytes, n);
    b->len += n;
}

static bool IsBlock(const char *name) {
    static const char *const blocks[] = {
        "br", "div", "p", "li", "ul", "ol", "h1", "h2", "h3", "h4", "h5", "h6", "tr", "td",
    };
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (strcmp(name, blocks[i]) == 0) return true;
    }
    return false;
}

static void DecodeInto(TextBuf *b, const char *p, const char *end, bool skip_hidden) {
    while (p < end && !b->full) {
        if (*p == '<') {
            bool closing = p + 1 < end && p[1] == '/';
            char name[16];
            TagName(p + (closing ? 2 : 1), end, name, sizeof(name));
            const char *tag_end = TagEnd(p, end);

            if (!closing && skip_hidden) {
                const char *hidden = Find(p, tag_end, "aria-hidden=\"true\"");
                if (hidden) {
                    p = ElementEnd(p, end);
                    continue;
                }
            }
            if (IsBlock(name)) b->pending_space = true;
            p = tag_end < end ? tag_end + 1 : end;
        } else if (*p == '&') {
            char buf[4];
            size_t consumed = 0;
            size_t n = DecodeEntity(p, end, buf, &consumed);
            if (n == 0) {
                PutBytes(b, p, 1);
                p++;
            } else {
                if (n != (size_t)-1) {
                    if (buf[0] == ' ') b->pending_space = true;
                    else PutBytes(b, buf, n);
                }
                p += consumed;
            }
        } else if (isspace((unsigned char)*p)) {
            b->pending_space = true;
            p++;
        } else {
            /* Copy a whole UTF-8 sequence at once so truncation lands on a
               character boundary. */
            size_t n = 1;
            unsigned char c = (unsigned char)*p;
            if (c >= 0xF0) n = 4;
            else if (c >= 0xE0) n = 3;
            else if (c >= 0xC0) n = 2;
            if (p + n > end) n = (size_t)(end - p);
            PutBytes(b, p, n);
            p += n;
        }
    }
}

void Html_Text(Html_Span span, char *out, size_t out_size) {
    if (out_size == 0) return;
    TextBuf b = { .out = out, .size = out_size };

    /* Skip the element's own opening tag, so a hidden element can still be
       read directly. */
    const char *p = span.start;
    if (p < span.end && *p == '<') {
        const char *tag_end = TagEnd(p, span.end);
        p = tag_end < span.end ? tag_end + 1 : span.end;
    }
    DecodeInto(&b, p, span.end, true);
    out[b.len] = '\0';
}

bool Html_ClassText(Html_Span span, const char *cls, char *out, size_t out_size) {
    Html_Span found;
    if (!Html_FindClass(span, cls, &found)) {
        if (out_size) out[0] = '\0';
        return false;
    }
    Html_Text(found, out, out_size);
    return true;
}

bool Html_Attr(const char *elem, const char *end, const char *name,
               char *out, size_t out_size) {
    if (out_size) out[0] = '\0';
    const char *tag_end = TagEnd(elem, end);
    size_t name_len = strlen(name);

    for (const char *p = elem; (p = Find(p, tag_end, name)) != NULL; p += name_len) {
        /* A whole attribute name, directly followed by ="value". */
        if (p > elem && IsNameChar(p[-1])) continue;
        const char *q = p + name_len;
        if (q + 1 >= tag_end || q[0] != '=' || (q[1] != '"' && q[1] != '\'')) continue;

        char quote = q[1];
        const char *value = q + 2;
        const char *value_end = value;
        while (value_end < tag_end && *value_end != quote) value_end++;

        TextBuf b = { .out = out, .size = out_size };
        DecodeInto(&b, value, value_end, false);
        out[b.len] = '\0';
        return true;
    }
    return false;
}

bool Html_StyleUrl(const char *elem, const char *end, char *out, size_t out_size) {
    char style[512];
    if (!Html_Attr(elem, end, "style", style, sizeof(style))) return false;

    const char *url = strstr(style, "url(");
    if (!url) return false;
    url += 4;
    if (*url == '\'' || *url == '"') url++;

    size_t n = strcspn(url, "'\")");
    if (n == 0 || n >= out_size) return false;
    memcpy(out, url, n);
    out[n] = '\0';
    return true;
}

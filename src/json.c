#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_DEPTH 48

static const char *p;

static void ws(void) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
}

static jv *mk(jtype t) {
    jv *v = calloc(1, sizeof(jv));
    if (v) v->t = t;
    return v;
}

static int hex4(const char *s) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static int put_utf8(char *o, int cp) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

/* p points at opening quote. Returns malloc'd string or NULL. */
static char *pstr(void) {
    p++;
    size_t cap = 32, n = 0;
    char *o = malloc(cap);
    if (!o) return NULL;
    while (*p && *p != '"') {
        if (n + 8 >= cap) {
            cap *= 2;
            char *t = realloc(o, cap);
            if (!t) { free(o); return NULL; }
            o = t;
        }
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': o[n++] = '\n'; p++; break;
            case 't': o[n++] = '\t'; p++; break;
            case 'r': o[n++] = '\r'; p++; break;
            case 'b': o[n++] = '\b'; p++; break;
            case 'f': o[n++] = '\f'; p++; break;
            case 'u': {
                int cp = hex4(p + 1);
                if (cp < 0) { free(o); return NULL; }
                p += 5;
                if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                    int lo = hex4(p + 2);
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                n += (size_t)put_utf8(o + n, cp);
                break;
            }
            case 0: free(o); return NULL;
            default: o[n++] = *p++; break; /* \" \\ \/ */
            }
        } else {
            o[n++] = *p++;
        }
    }
    if (*p != '"') { free(o); return NULL; }
    p++;
    o[n] = 0;
    return o;
}

static jv *pval(int depth);

static int push(jv *v, jv *item, char *key) {
    jv **ni = realloc(v->items, sizeof(jv *) * (size_t)(v->len + 1));
    if (!ni) return 0;
    v->items = ni;
    if (v->t == J_OBJ) {
        char **nk = realloc(v->keys, sizeof(char *) * (size_t)(v->len + 1));
        if (!nk) return 0;
        v->keys = nk;
        v->keys[v->len] = key;
    }
    v->items[v->len++] = item;
    return 1;
}

static jv *pval(int depth) {
    if (depth > MAX_DEPTH) return NULL;
    ws();
    if (*p == '{') {
        jv *o = mk(J_OBJ);
        if (!o) return NULL;
        p++; ws();
        if (*p == '}') { p++; return o; }
        for (;;) {
            ws();
            if (*p != '"') { json_free(o); return NULL; }
            char *k = pstr();
            if (!k) { json_free(o); return NULL; }
            ws();
            if (*p != ':') { free(k); json_free(o); return NULL; }
            p++;
            jv *v = pval(depth + 1);
            if (!v || !push(o, v, k)) { free(k); json_free(v); json_free(o); return NULL; }
            ws();
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; return o; }
            json_free(o); return NULL;
        }
    }
    if (*p == '[') {
        jv *a = mk(J_ARR);
        if (!a) return NULL;
        p++; ws();
        if (*p == ']') { p++; return a; }
        for (;;) {
            jv *v = pval(depth + 1);
            if (!v || !push(a, v, NULL)) { json_free(v); json_free(a); return NULL; }
            ws();
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return a; }
            json_free(a); return NULL;
        }
    }
    if (*p == '"') {
        jv *v = mk(J_STR);
        if (!v) return NULL;
        v->s = pstr();
        if (!v->s) { free(v); return NULL; }
        return v;
    }
    if (!strncmp(p, "true", 4)) { p += 4; jv *v = mk(J_BOOL); if (v) v->n = 1; return v; }
    if (!strncmp(p, "false", 5)) { p += 5; return mk(J_BOOL); }
    if (!strncmp(p, "null", 4)) { p += 4; return mk(J_NULL); }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        char *end;
        double d = strtod(p, &end);
        if (end == p) return NULL;
        p = end;
        jv *v = mk(J_NUM);
        if (v) v->n = d;
        return v;
    }
    return NULL;
}

jv *json_parse(const char *src) {
    if (!src) return NULL;
    p = src;
    /* skip UTF-8 BOM */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    jv *v = pval(0);
    return v;
}

void json_free(jv *v) {
    if (!v) return;
    for (int i = 0; i < v->len; i++) {
        json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->s);
    free(v);
}

jv *jget(const jv *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (int i = 0; i < o->len; i++)
        if (!strcmp(o->keys[i], key)) return o->items[i];
    return NULL;
}

jv *jat(const jv *a, int i) {
    if (!a || a->t != J_ARR || i < 0 || i >= a->len) return NULL;
    return a->items[i];
}

int jlen(const jv *v) { return (v && (v->t == J_ARR || v->t == J_OBJ)) ? v->len : 0; }

const char *jstr(const jv *v, const char *def) { return (v && v->t == J_STR && v->s) ? v->s : def; }

double jnum(const jv *v, double def) {
    if (!v) return def;
    if (v->t == J_NUM) return v->n;
    if (v->t == J_STR && v->s) return atof(v->s);
    return def;
}

int jtrue(const jv *v) { return v && v->t == J_BOOL && v->n != 0; }

char *json_quote(const char *s) {
    size_t n = strlen(s);
    char *o = malloc(n * 6 + 3), *w = o;
    if (!o) return NULL;
    *w++ = '"';
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
        case '"': *w++ = '\\'; *w++ = '"'; break;
        case '\\': *w++ = '\\'; *w++ = '\\'; break;
        case '\n': *w++ = '\\'; *w++ = 'n'; break;
        case '\r': *w++ = '\\'; *w++ = 'r'; break;
        case '\t': *w++ = '\\'; *w++ = 't'; break;
        default:
            if (ch < 0x20) w += sprintf(w, "\\u%04x", ch);
            else *w++ = (char)ch;
        }
    }
    *w++ = '"';
    *w = 0;
    return o;
}

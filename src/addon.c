#include "addon.h"
#include "json.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 60
#define MAX_VIDEOS 2000

Addon *g_addons = NULL;
int g_naddons = 0;

static char *dup(const char *s) { return strdup(s ? s : ""); }

static char *dupn(const char *s, size_t n) {
    char *o = malloc(n + 1);
    if (!o) return NULL;
    memcpy(o, s, n);
    o[n] = 0;
    return o;
}

/* "stremio://host/x/manifest.json" -> "https://host/x" */
static char *normalize_base(const char *line) {
    char *b;
    if (!strncmp(line, "stremio://", 10)) {
        b = malloc(strlen(line) + 8);
        sprintf(b, "https://%s", line + 10);
    } else {
        b = dup(line);
    }
    size_t n = strlen(b);
    const char *mf = "/manifest.json";
    size_t ml = strlen(mf);
    if (n >= ml && !strcmp(b + n - ml, mf)) b[n - ml] = 0;
    n = strlen(b);
    while (n > 0 && b[n - 1] == '/') b[--n] = 0;
    return b;
}

static char *join(const char *a, const char *b, const char *c, const char *d, const char *e) {
    size_t n = strlen(a) + strlen(b) + strlen(c) + strlen(d) + strlen(e) + 1;
    char *o = malloc(n);
    snprintf(o, n, "%s%s%s%s%s", a, b, c, d, e);
    return o;
}

/* Build a ",a,b," csv from a JSON string array, or NULL if absent. */
static char *types_csv(const jv *arr) {
    if (!arr || arr->t != J_ARR || arr->len == 0) return NULL;
    size_t cap = 3;
    for (int i = 0; i < arr->len; i++) cap += strlen(jstr(arr->items[i], "")) + 1;
    char *o = malloc(cap + 1);
    strcpy(o, ",");
    for (int i = 0; i < arr->len; i++) {
        strcat(o, jstr(arr->items[i], ""));
        strcat(o, ",");
    }
    return o;
}

static void parse_manifest(Addon *a, const jv *m) {
    a->name = dup(jstr(jget(m, "name"), "Addon"));

    const jv *cats = jget(m, "catalogs");
    for (int i = 0; i < jlen(cats); i++) {
        const jv *c = jat(cats, i);
        const char *type = jstr(jget(c, "type"), NULL), *id = jstr(jget(c, "id"), NULL);
        if (!type || !id) continue;
        /* Catalogs needing input other than "search" can't be listed here. */
        int required_other = 0, has_search = 0, search_required = 0;
        const jv *extra = jget(c, "extra");
        for (int k = 0; k < jlen(extra); k++) {
            const jv *x = jat(extra, k);
            int req = jtrue(jget(x, "isRequired"));
            if (!strcmp(jstr(jget(x, "name"), ""), "search")) { has_search = 1; if (req) search_required = 1; }
            else if (req) required_other = 1;
        }
        const jv *er = jget(c, "extraRequired");
        for (int k = 0; k < jlen(er); k++) {
            if (!strcmp(jstr(jat(er, k), ""), "search")) search_required = 1;
            else required_other = 1;
        }
        const jv *es = jget(c, "extraSupported");
        for (int k = 0; k < jlen(es); k++)
            if (!strcmp(jstr(jat(es, k), ""), "search")) has_search = 1;
        if (required_other) continue;

        a->cats = realloc(a->cats, sizeof(Cat) * (size_t)(a->ncat + 1));
        Cat *nc = &a->cats[a->ncat++];
        nc->type = dup(type);
        nc->id = dup(id);
        nc->name = dup(jstr(jget(c, "name"), id));
        nc->searchable = has_search;
        nc->browsable = !search_required;
    }

    const jv *res = jget(m, "resources");
    char *mtypes = types_csv(jget(m, "types"));
    for (int i = 0; i < jlen(res); i++) {
        const jv *r = jat(res, i);
        if (r->t == J_STR) {
            if (!strcmp(r->s, "stream")) {
                a->stream = 1;
                if (!a->stream_types && mtypes) a->stream_types = dup(mtypes);
            }
        } else if (r->t == J_OBJ && !strcmp(jstr(jget(r, "name"), ""), "stream")) {
            a->stream = 1;
            char *t = types_csv(jget(r, "types"));
            free(a->stream_types);
            a->stream_types = t ? t : (mtypes ? dup(mtypes) : NULL);
        }
    }
    free(mtypes);
}

int addons_load(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
        if (!n || s[0] == '#') continue;

        char *base = normalize_base(s);
        int seen = 0;
        for (int i = 0; i < g_naddons; i++) if (!strcmp(g_addons[i].base, base)) seen = 1;
        if (seen) { free(base); continue; }
        char *url = join(base, "/manifest.json", "", "", "");
        char *body = net_get(url);
        free(url);
        jv *m = body ? json_parse(body) : NULL;
        free(body);
        if (!m || m->t != J_OBJ) {
            fprintf(stderr, "addon: cannot load manifest for %s\n", base);
            json_free(m);
            free(base);
            continue;
        }
        g_addons = realloc(g_addons, sizeof(Addon) * (size_t)(g_naddons + 1));
        Addon *a = &g_addons[g_naddons++];
        memset(a, 0, sizeof *a);
        a->base = base;
        parse_manifest(a, m);
        json_free(m);
    }
    fclose(f);
    return g_naddons;
}

void addons_clear(void) {
    for (int i = 0; i < g_naddons; i++) {
        Addon *a = &g_addons[i];
        for (int k = 0; k < a->ncat; k++) { free(a->cats[k].type); free(a->cats[k].id); free(a->cats[k].name); }
        free(a->cats); free(a->base); free(a->name); free(a->stream_types);
    }
    free(g_addons);
    g_addons = NULL;
    g_naddons = 0;
}

/* ---- catalog ---- */

static void item_fill(Item *it, const jv *o, const char *deftype) {
    memset(it, 0, sizeof *it);
    it->id = dup(jstr(jget(o, "id"), ""));
    it->type = dup(jstr(jget(o, "type"), deftype));
    it->name = dup(jstr(jget(o, "name"), "?"));
    it->poster = dup(jstr(jget(o, "poster"), ""));
    const jv *y = jget(o, "releaseInfo");
    if (!y) y = jget(o, "year");
    if (y && y->t == J_NUM) {
        char b[16];
        snprintf(b, sizeof b, "%d", (int)y->n);
        it->year = dup(b);
    } else {
        it->year = dup(jstr(y, ""));
    }
    it->desc = dup(jstr(jget(o, "description"), ""));
}

void item_clear(Item *m) {
    if (!m) return;
    free(m->id); free(m->type); free(m->name); free(m->poster); free(m->year); free(m->desc);
    memset(m, 0, sizeof *m);
}

void items_free(Item *it, int n) {
    for (int i = 0; i < n; i++) item_clear(&it[i]);
    free(it);
}

Item *catalog_fetch(const Addon *a, const Cat *c, const char *query, int *n) {
    *n = 0;
    char *id = net_urlenc(c->id);
    char *type = net_urlenc(c->type);
    char *url = join(a->base, "/catalog/", type, "/", id);
    char *url2;
    if (query) {
        char *eq = net_urlenc(query);
        char *u = join(url, "/search=", eq, ".json", "");
        free(eq);
        url2 = u;
    } else {
        url2 = join(url, ".json", "", "", "");
    }
    free(id); free(type); free(url);
    char *body = net_get(url2);
    free(url2);
    jv *r = body ? json_parse(body) : NULL;
    free(body);
    const jv *metas = jget(r, "metas");
    int cnt = jlen(metas);
    if (cnt > MAX_ITEMS) cnt = MAX_ITEMS;
    Item *out = NULL;
    if (cnt > 0) {
        out = calloc((size_t)cnt, sizeof(Item));
        int k = 0;
        for (int i = 0; i < cnt; i++) {
            const jv *o = jat(metas, i);
            if (!o || o->t != J_OBJ || !jstr(jget(o, "id"), NULL)) continue;
            item_fill(&out[k], o, c->type);
            out[k++].owner = a;
        }
        *n = k;
        if (k == 0) { free(out); out = NULL; }
    }
    json_free(r);
    return out;
}

/* ---- meta ---- */

void videos_free(Video *v, int n) {
    for (int i = 0; i < n; i++) { free(v[i].id); free(v[i].title); }
    free(v);
}

static int meta_try(const Addon *a, const char *type, const char *id,
                    Item *m, Video **vids, int *nv) {
    char *eid = net_urlenc(id), *et = net_urlenc(type);
    char *url = join(a->base, "/meta/", et, "/", eid);
    char *url2 = join(url, ".json", "", "", "");
    free(eid); free(et); free(url);
    char *body = net_get(url2);
    free(url2);
    jv *r = body ? json_parse(body) : NULL;
    free(body);
    const jv *meta = jget(r, "meta");
    if (!meta || meta->t != J_OBJ) { json_free(r); return 0; }

    item_fill(m, meta, type);
    *vids = NULL; *nv = 0;
    const jv *vs = jget(meta, "videos");
    int cnt = jlen(vs);
    if (cnt > MAX_VIDEOS) cnt = MAX_VIDEOS;
    if (cnt > 0) {
        Video *out = calloc((size_t)cnt, sizeof(Video));
        int k = 0;
        for (int i = 0; i < cnt; i++) {
            const jv *o = jat(vs, i);
            const char *vid = jstr(jget(o, "id"), NULL);
            if (!vid) continue;
            out[k].id = dup(vid);
            out[k].title = dup(jstr(jget(o, "title"), jstr(jget(o, "name"), "")));
            out[k].season = (int)jnum(jget(o, "season"), 0);
            out[k].episode = (int)jnum(jget(o, "episode"), jnum(jget(o, "number"), 0));
            k++;
        }
        *vids = out; *nv = k;
    }
    json_free(r);
    return 1;
}

int meta_fetch(const Addon *prefer, const char *type, const char *id,
               Item *m, Video **vids, int *nv) {
    if (prefer && meta_try(prefer, type, id, m, vids, nv)) return 1;
    for (int i = 0; i < g_naddons; i++) {
        if (&g_addons[i] == prefer) continue;
        if (meta_try(&g_addons[i], type, id, m, vids, nv)) return 1;
    }
    return 0;
}

/* ---- streams ---- */

void streams_free(Stream *s, int n) {
    for (int i = 0; i < n; i++) { free(s[i].url); free(s[i].label); free(s[i].hash); free(s[i].tr_query); }
    free(s);
}

static int is_http(const char *u) {
    return u && (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8));
}

Stream *streams_fetch(const char *type, const char *video_id, int *n, int *skipped) {
    *n = 0; *skipped = 0;
    Stream *out = NULL;
    char *eid = net_urlenc(video_id), *et = net_urlenc(type);
    char needle[64];
    snprintf(needle, sizeof needle, ",%s,", type);

    for (int i = 0; i < g_naddons; i++) {
        const Addon *a = &g_addons[i];
        if (!a->stream) continue;
        if (a->stream_types && !strstr(a->stream_types, needle)) continue;

        char *url = join(a->base, "/stream/", et, "/", eid);
        char *url2 = join(url, ".json", "", "", "");
        free(url);
        char *body = net_get(url2);
        free(url2);
        jv *r = body ? json_parse(body) : NULL;
        free(body);
        const jv *ss = jget(r, "streams");
        for (int k = 0; k < jlen(ss); k++) {
            const jv *s = jat(ss, k);
            const char *u = jstr(jget(s, "url"), NULL);
            const char *ih = jstr(jget(s, "infoHash"), NULL);
            int torrent = 0;
            if (is_http(u)) {
                /* direct link */
            } else if (ih && strlen(ih) == 40) {
                torrent = 1;
            } else {
                (*skipped)++;
                continue;
            }

            const char *name = jstr(jget(s, "name"), a->name);
            const char *desc = jstr(jget(s, "title"), jstr(jget(s, "description"), ""));
            size_t dl = strcspn(desc, "\n");
            char *first = dupn(desc, dl);
            /* name may be multi-line too */
            char *nm = dupn(name, strcspn(name, "\n"));
            size_t ln = strlen(nm) + dl + 16;
            char *label = malloc(ln);
            const char *tag = torrent ? "[torrent] " : "";
            if (dl) snprintf(label, ln, "%s%s - %s", tag, nm, first);
            else snprintf(label, ln, "%s%s", tag, nm);
            free(nm); free(first);

            out = realloc(out, sizeof(Stream) * (size_t)(*n + 1));
            Stream *st = &out[*n];
            memset(st, 0, sizeof *st);
            st->label = label;
            st->file_idx = -1;
            if (torrent) {
                st->kind = STREAM_TORRENT;
                st->hash = dup(ih);
                for (char *c = st->hash; *c; c++) if (*c >= 'A' && *c <= 'F') *c += 32;
                if (jget(s, "fileIdx")) st->file_idx = (int)jnum(jget(s, "fileIdx"), -1);
                /* sources: ["tracker:udp://...", "dht:..."] */
                const jv *src = jget(s, "sources");
                size_t cap = 1, used = 0;
                char *tq = calloc(1, 1);
                int ntr = 0;
                for (int q = 0; q < jlen(src) && ntr < 8; q++) {
                    const char *sv = jstr(jat(src, q), "");
                    if (strncmp(sv, "tracker:", 8)) continue;
                    char *enc = net_urlenc(sv + 8);
                    size_t add = strlen(enc) + 5;
                    tq = realloc(tq, cap + used + add);
                    used += (size_t)sprintf(tq + used, "%str=%s", ntr ? "&" : "", enc);
                    cap = 1;
                    free(enc);
                    ntr++;
                }
                if (ntr) st->tr_query = tq; else free(tq);
            } else {
                st->kind = STREAM_URL;
                st->url = dup(u);
            }
            (*n)++;
        }
        json_free(r);
    }
    free(eid); free(et);
    return out;
}

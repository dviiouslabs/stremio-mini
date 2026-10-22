/* Stremio addon protocol client (manifest / catalog / search / meta / stream).
 * Spec: https://github.com/Stremio/stremio-addon-sdk/tree/master/docs/api */
#ifndef ADDON_H
#define ADDON_H

typedef struct Addon Addon;

typedef struct {
    char *type, *id, *name;
    int browsable;   /* can be listed without input */
    int searchable;  /* supports the "search" extra */
} Cat;

struct Addon {
    char *base, *name;
    Cat *cats; int ncat;
    int stream;            /* offers the "stream" resource */
    char *stream_types;    /* ",movie,series," or NULL = any */
};

typedef struct {
    char *id, *type, *name, *poster, *year, *desc;
    const Addon *owner;    /* addon that listed it (used for meta lookup) */
    void *img;             /* UI-owned poster surface */
    int tried;             /* poster fetch attempted */
} Item;

typedef struct { char *id, *title; int season, episode; } Video;

typedef enum { STREAM_URL, STREAM_TORRENT } StreamKind;

typedef struct {
    StreamKind kind;
    char *label;
    char *url;        /* STREAM_URL: direct http(s) link */
    char *hash;       /* STREAM_TORRENT: 40-hex infoHash */
    int file_idx;     /* -1 when the addon didn't say */
    char *tr_query;   /* "tr=a&tr=b" or NULL */
} Stream;

extern Addon *g_addons;
extern int g_naddons;

/* Append addons listed in `file` (manifest URLs, one per line, '#' comments).
 * May be called several times (e.g. addons.txt + account_addons.txt). */
int addons_load(const char *file);
void addons_clear(void);

/* query == NULL: plain catalog; otherwise search within that catalog. */
Item *catalog_fetch(const Addon *a, const Cat *c, const char *query, int *n);
void items_free(Item *it, int n);

/* Fills *m (strings owned by caller via item_clear) and a video list (series). */
int meta_fetch(const Addon *prefer, const char *type, const char *id,
               Item *m, Video **vids, int *nv);
void item_clear(Item *m);
void videos_free(Video *v, int n);

/* Collect playable streams (direct URLs and torrent infoHashes) from all stream
 * addons. *skipped counts other kinds (YouTube ids, external links, ...). */
Stream *streams_fetch(const char *type, const char *video_id, int *n, int *skipped);
void streams_free(Stream *s, int n);

#endif

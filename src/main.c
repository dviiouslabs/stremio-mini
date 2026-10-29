/* Stremio Mini - a Stremio addon client for the Miyoo Mini (Plus), Onion OS.
 * SDL 1.2, 640x480. Run from the app folder (launch.sh does the cd). */
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "account.h"
#include "addon.h"
#include "net.h"
#include "torrent.h"

#define SW 640
#define SH 480
#define FONT_PATH "font.ttf"
#define CACHE_DIR "cache"

#define COLS 5
#define ROWS 2
#define PW 116
#define PH 170

static SDL_Surface *screen;
static TTF_Font *f_big, *f_mid, *f_small;
static int g_quit;

static const SDL_Color C_TXT = {235, 235, 242, 0};
static const SDL_Color C_DIM = {140, 140, 158, 0};
static const SDL_Color C_ACC = {255, 255, 255, 0};

static inline int rotate_and_flip(SDL_Surface *surf) {
    if (surf && surf->pixels) {
        SDL_LockSurface(surf);
        Uint32 *p = (Uint32 *)surf->pixels;
        int total = surf->w * surf->h;
        for (int i = 0; i < total / 2; i++) {
            Uint32 temp = p[i];
            p[i] = p[total - 1 - i];
            p[total - 1 - i] = temp;
        }
        SDL_UnlockSurface(surf);
    }
    return SDL_Flip(surf);
}
#define SDL_Flip rotate_and_flip

/* ---------------------------------------------------------------- UI core */

static int ui_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 0;
    SDL_ShowCursor(SDL_DISABLE);
    screen = SDL_SetVideoMode(SW, SH, 32, SDL_SWSURFACE);
    if (!screen) return 0;
    if (!TTF_WasInit() && TTF_Init() < 0) return 0;
    if (!f_big) {
        f_big = TTF_OpenFont(FONT_PATH, 26);
        f_mid = TTF_OpenFont(FONT_PATH, 20);
        f_small = TTF_OpenFont(FONT_PATH, 14);
        if (!f_big || !f_mid || !f_small) return 0;
    }
    SDL_EnableKeyRepeat(350, 90);
    return 1;
}

static void ui_shutdown(void) {
    SDL_Quit(); /* releases the framebuffer so the external player can use it */
}

static Uint32 rgb(int r, int g, int b) { return SDL_MapRGB(screen->format, (Uint8)r, (Uint8)g, (Uint8)b); }

static void fill(int x, int y, int w, int h, Uint32 c) {
    SDL_Rect r = {(Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h};
    SDL_FillRect(screen, &r, c);
}

static void text(TTF_Font *f, const char *s, int x, int y, SDL_Color c) {
    if (!s || !*s) return;
    SDL_Surface *t = TTF_RenderUTF8_Blended(f, s, c);
    if (!t) return;
    SDL_Rect d = {(Sint16)x, (Sint16)y, 0, 0};
    SDL_BlitSurface(t, NULL, screen, &d);
    SDL_FreeSurface(t);
}

/* Draw s clipped with an ellipsis to fit max_w pixels. */
static void text_fit(TTF_Font *f, const char *s, int x, int y, int max_w, SDL_Color c) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", s);
    int w = 0, h = 0;
    TTF_SizeUTF8(f, buf, &w, &h);
    if (w > max_w) {
        size_t n = strlen(buf);
        while (n > 0) {
            n--;
            while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--; /* utf-8 boundary */
            buf[n] = 0;
            char tmp[520];
            snprintf(tmp, sizeof tmp, "%s...", buf);
            TTF_SizeUTF8(f, tmp, &w, &h);
            if (w <= max_w) { snprintf(buf, sizeof buf, "%s", tmp); break; }
        }
    }
    text(f, buf, x, y, c);
}

/* Greedy word wrap; returns lines drawn. */
static int text_wrap(TTF_Font *f, const char *s, int x, int y, int max_w, int max_lines, SDL_Color c) {
    int lh = TTF_FontLineSkip(f), lines = 0;
    char line[512] = "";
    const char *p = s;
    while (*p && lines < max_lines) {
        const char *e = p;
        while (*e && *e != ' ' && *e != '\n') e++;
        char word[256];
        size_t wl = (size_t)(e - p);
        if (wl >= sizeof word) wl = sizeof word - 1;
        memcpy(word, p, wl);
        word[wl] = 0;

        char trial[800];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        int w = 0, h = 0;
        TTF_SizeUTF8(f, trial, &w, &h);
        if (w > max_w && line[0]) {
            text(f, line, x, y + lines * lh, c);
            lines++;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
        if (*e == '\n' && lines < max_lines) {
            text(f, line, x, y + lines * lh, c);
            lines++;
            line[0] = 0;
        }
        p = *e ? e + 1 : e;
    }
    if (line[0] && lines < max_lines) {
        text_fit(f, line, x, y + lines * lh, max_w, c);
        lines++;
    }
    return lines;
}

static void clear_screen(void) { fill(0, 0, SW, SH, rgb(16, 16, 22)); }

static void header(const char *title) {
    fill(0, 0, SW, 44, rgb(32, 26, 58));
    fill(0, 44, SW, 2, rgb(123, 91, 245));
    text_fit(f_mid, title, 14, 9, SW - 28, C_ACC);
}

static void footer(const char *hint) {
    fill(0, SH - 26, SW, 26, rgb(24, 24, 34));
    text_fit(f_small, hint, 12, SH - 22, SW - 24, C_DIM);
}

enum { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_A, K_B, K_X, K_Y, K_L, K_R, K_START, K_SELECT, K_QUIT };

/* Miyoo Mini under Onion/SDL1.2: A=SPACE B=LCTRL X=LSHIFT Y=LALT L1=e R1=t
 * START=RETURN SELECT=RCTRL. z/x/c/v for desktop testing. */
static int wait_key(void) {
    SDL_Event e;
    while (SDL_WaitEvent(&e)) {
        if (e.type == SDL_QUIT) return K_QUIT;
        if (e.type != SDL_KEYDOWN) continue;
        switch (e.key.keysym.sym) {
        case SDLK_UP: return K_UP;
        case SDLK_DOWN: return K_DOWN;
        case SDLK_LEFT: return K_LEFT;
        case SDLK_RIGHT: return K_RIGHT;
        case SDLK_SPACE: case SDLK_z: return K_A;
        case SDLK_LCTRL: case SDLK_x: return K_B;
        case SDLK_LSHIFT: case SDLK_c: return K_X;
        case SDLK_LALT: case SDLK_v: return K_Y;
        case SDLK_RETURN: return K_START;
        case SDLK_RCTRL: return K_SELECT;
        case SDLK_e: return K_L;
        case SDLK_t: return K_R;
        case SDLK_ESCAPE: return K_QUIT;
        default: break;
        }
    }
    return K_QUIT;
}

static void status(const char *msg) {
    clear_screen();
    int w = 0, h = 0;
    TTF_SizeUTF8(f_mid, msg, &w, &h);
    text(f_mid, msg, (SW - w) / 2, SH / 2 - h / 2, C_TXT);
    SDL_Flip(screen);
}

static void message(const char *title, const char *msg) {
    for (;;) {
        clear_screen();
        header(title);
        text_wrap(f_mid, msg, 24, 70, SW - 48, 14, C_TXT);
        footer("A / B  OK");
        SDL_Flip(screen);
        int k = wait_key();
        if (k == K_QUIT) g_quit = 1;
        if (k == K_A || k == K_B || k == K_QUIT) return;
    }
}

/* Scrollable single-column list. Returns chosen index or -1 on back. */
static int pick_list(const char *title, char **labels, int n, int sel) {
    const int rows = 11, rh = 35, y0 = 54;
    int top = 0;
    if (sel < 0 || sel >= n) sel = 0;
    if (n == 0) { message(title, "Nothing here."); return -1; }
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        clear_screen();
        header(title);
        for (int i = 0; i < rows && top + i < n; i++) {
            int y = y0 + i * rh;
            if (top + i == sel) fill(8, y, SW - 16, rh - 3, rgb(123, 91, 245));
            text_fit(f_mid, labels[top + i], 18, y + 5, SW - 60, top + i == sel ? C_ACC : C_TXT);
        }
        if (n > rows) {
            int bh = (rows * rh) * rows / n; if (bh < 16) bh = 16;
            int by = y0 + (rows * rh - bh) * top / (n - rows ? n - rows : 1);
            fill(SW - 10, by, 4, bh, rgb(90, 90, 120));
        }
        footer("A select    B back    L/R page");
        SDL_Flip(screen);
        switch (wait_key()) {
        case K_UP: sel = (sel + n - 1) % n; break;
        case K_DOWN: sel = (sel + 1) % n; break;
        case K_L: sel -= rows; if (sel < 0) sel = 0; break;
        case K_R: sel += rows; if (sel >= n) sel = n - 1; break;
        case K_A: return sel;
        case K_B: return -1;
        case K_QUIT: g_quit = 1; return -1;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- posters */

static unsigned hash_str(const char *s) {
    unsigned h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

static SDL_Surface *load_poster(const char *path) {
    SDL_Surface *src = IMG_Load(path);
    if (!src) {
        FILE *f = fopen(path, "rb");
        if (f) {
            int w = 0, h = 0;
            if (fread(&w, sizeof(int), 1, f) == 1 && fread(&h, sizeof(int), 1, f) == 1 && w > 0 && h > 0) {
                src = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 24, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
                if (src) {
                    size_t expected = (size_t)w * h * 3;
                    if (fread(src->pixels, 1, expected, f) != expected) {
                        SDL_FreeSurface(src);
                        src = NULL;
                    }
                }
            }
            fclose(f);
        }
    }
    if (!src) return NULL;
    SDL_Surface *s32 = SDL_CreateRGBSurface(SDL_SWSURFACE, src->w, src->h, 32,
                                            0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    SDL_Surface *dst = SDL_CreateRGBSurface(SDL_SWSURFACE, PW, PH, 32,
                                            0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    if (!s32 || !dst) { 
        if (src) SDL_FreeSurface(src); 
        if (s32) SDL_FreeSurface(s32); 
        if (dst) SDL_FreeSurface(dst); 
        return NULL; 
    }
    SDL_SetAlpha(src, 0, 255);
    SDL_BlitSurface(src, NULL, s32, NULL);
    SDL_FreeSurface(src);
    
    // Perform high-quality scaling down to poster width/height dimensions
    for (int y = 0; y < PH; y++) {
        int sy = y * s32->h / PH;
        for (int x = 0; x < PW; x++) {
            int sx = x * s32->w / PW;
            ((Uint32 *)((Uint8 *)dst->pixels + y * dst->pitch))[x] =
                ((Uint32 *)((Uint8 *)s32->pixels + sy * s32->pitch))[sx];
        }
    }
    SDL_FreeSurface(s32);
    return dst;
}


static void poster_path(const Item *it, char *out, size_t n) {
    snprintf(out, n, CACHE_DIR "cache/%08x.png", hash_str(it->poster));
}

/* Fetch (in parallel) and decode posters for items [from, to). */
static void ensure_posters(Item *items, int from, int to) {
    const char *urls[COLS * ROWS * 2];
    char paths_buf[COLS * ROWS * 2][64];
    const char *paths[COLS * ROWS * 2];
    int idx[COLS * ROWS * 2], m = 0;

    for (int i = from; i < to && m < COLS * ROWS * 2; i++) {
        if (items[i].tried || !items[i].poster || !items[i].poster[0]) continue;
        items[i].tried = 1;
        poster_path(&items[i], paths_buf[m], sizeof paths_buf[m]);
        urls[m] = items[i].poster;
        paths[m] = paths_buf[m];
        idx[m] = i;
        m++;
    }
    if (!m) return;
    status("Loading posters...");
    mkdir(CACHE_DIR, 0755);

    const char *dl_urls[COLS * ROWS * 2], *dl_paths[COLS * ROWS * 2];
    int nd = 0;
    for (int k = 0; k < m; k++) {
        if (access(paths[k], R_OK) != 0) { dl_urls[nd] = urls[k]; dl_paths[nd] = paths[k]; nd++; }
    }
    net_download_many(dl_urls, dl_paths, nd);
    for (int k = 0; k < m; k++) items[idx[k]].img = load_poster(paths[k]);
}

static void draw_poster(const Item *it, int x, int y) {
    if (it->img) {
        SDL_Rect d = {(Sint16)x, (Sint16)y, 0, 0};
        SDL_BlitSurface((SDL_Surface *)it->img, NULL, screen, &d);
    } else {
        fill(x, y, PW, PH, rgb(40, 40, 58));
        text_wrap(f_small, it->name, x + 6, y + 8, PW - 12, 6, C_DIM);
    }
}

/* ---------------------------------------------------------------- keyboard */

static const char *LAYERS[3][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm.@_"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM.@_"},
    {"!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\|", "~`/:;%#?&="},
};

/* On-screen keyboard. Returns 1 if confirmed, 0 if cancelled. */
static int osk(const char *title, const char *initial, char *buf, size_t cap, int mask) {
    const int KW = 58, KH = 46, KX = 30, KY = 126;
    static const char *SPECIAL[5] = {"Shift", "Sym", "Space", "Del", "OK"};
    snprintf(buf, cap, "%s", initial ? initial : "");
    int layer = 0, row = 0, col = 0;

    for (;;) {
        clear_screen();
        header(title);

        /* text field (shows the tail if it is too long) */
        fill(20, 58, SW - 40, 44, rgb(30, 30, 44));
        char shown[1100];
        size_t len = strlen(buf);
        if (mask) {
            size_t i;
            for (i = 0; i < len && i < sizeof shown - 1; i++) shown[i] = '*';
            shown[i] = 0;
        } else {
            snprintf(shown, sizeof shown, "%s", buf);
        }
        int w = 0, h = 0;
        const char *p = shown;
        TTF_SizeUTF8(f_mid, p, &w, &h);
        while (*p && w > SW - 90) { p++; TTF_SizeUTF8(f_mid, p, &w, &h); }
        text(f_mid, p, 30, 68, C_TXT);
        fill(30 + w + 2, 68, 2, 24, rgb(123, 91, 245));

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 10; c++) {
                int x = KX + c * KW, y = KY + r * KH;
                int selc = (row == r && col == c);
                fill(x, y, KW - 4, KH - 4, selc ? rgb(123, 91, 245) : rgb(36, 36, 52));
                char ch[2] = {LAYERS[layer][r][c], 0};
                TTF_SizeUTF8(f_mid, ch, &w, &h);
                text(f_mid, ch, x + (KW - 4 - w) / 2, y + (KH - 4 - h) / 2, selc ? C_ACC : C_TXT);
            }
        }
        for (int b = 0; b < 5; b++) {
            int x = KX + b * 2 * KW, y = KY + 4 * KH;
            int selc = (row == 4 && col == b);
            fill(x, y, 2 * KW - 4, KH - 4, selc ? rgb(123, 91, 245) : rgb(48, 40, 84));
            const char *lab = SPECIAL[b];
            if (b == 0) lab = layer == 1 ? "abc" : "ABC";
            TTF_SizeUTF8(f_mid, lab, &w, &h);
            text(f_mid, lab, x + (2 * KW - 4 - w) / 2, y + (KH - 4 - h) / 2, selc ? C_ACC : C_TXT);
        }
        footer("A type   B delete   X shift   Y space   START ok   SELECT cancel");
        SDL_Flip(screen);

        int maxcol = (row == 4) ? 4 : 9;
        int k = wait_key();
        switch (k) {
        case K_LEFT: col = (col + maxcol) % (maxcol + 1); break;
        case K_RIGHT: col = (col + 1) % (maxcol + 1); break;
        case K_UP:
        case K_DOWN: {
            int nr = (row + (k == K_UP ? 4 : 1)) % 5;
            if (nr == 4) col = col / 2;          /* 10 key columns -> 5 wide buttons */
            else if (row == 4) col = col * 2;    /* and back */
            row = nr;
            break;
        }
        case K_A:
            if (row < 4) {
                size_t l = strlen(buf);
                if (l + 1 < cap) { buf[l] = LAYERS[layer][row][col]; buf[l + 1] = 0; }
            } else if (col == 0) layer = (layer == 0) ? 1 : 0;
            else if (col == 1) layer = (layer == 2) ? 0 : 2;
            else if (col == 2) { size_t l = strlen(buf); if (l + 1 < cap) { buf[l] = ' '; buf[l + 1] = 0; } }
            else if (col == 3) { size_t l = strlen(buf); if (l) buf[l - 1] = 0; }
            else return 1;
            break;
        case K_B: { size_t l = strlen(buf); if (!l) return 0; buf[l - 1] = 0; break; }
        case K_X: layer = (layer + 1) % 3; break;
        case K_Y: { size_t l = strlen(buf); if (l + 1 < cap) { buf[l] = ' '; buf[l + 1] = 0; } break; }
        case K_START: return 1;
        case K_SELECT: return 0;
        case K_QUIT: g_quit = 1; return 0;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- playback */

/* Hands the URL to player.sh (ffplay scaled to 480p). The UI releases the
 * screen first and takes it back when the player exits. */
static void play(const char *url) {
    char *q = net_sq(url);
    char cmd[2600];
    snprintf(cmd, sizeof cmd, "./player.sh %s", q);
    free(q);
    ui_shutdown();
    int rc = system(cmd);
    if (!ui_init()) { g_quit = 1; return; }
    if (rc != 0) {
        message("Playback",
                "The player exited with an error.\n\nCheck that ffplay is installed (see README) and that the stream is reachable.");
    }
}

static void play_torrent(const char *hash, int idx, const char *tr_query) {
    char *base = torrent_server();
    char *url = NULL;
    if (base) {
        url = torrent_url(base, hash, idx, tr_query);
        free(base);
    } else {
        status("Starting torrent...");
        url = torrent_helper_url(hash, tr_query);
    }
    if (!url) {
        message("Torrent",
                "No torrent server set.\n\nTorrents are streamed by a Stremio streaming server on your network (for example stremio-service on a PC, Pi or NAS).\n\nSettings > Torrent server, then enter http://<its-ip>:11470");
        return;
    }
    status("Connecting to torrent server...");
    play(url);
    free(url);
}

static void play_stream(const Stream *s) {
    if (s->kind == STREAM_URL) play(s->url);
    else play_torrent(s->hash, s->file_idx, s->tr_query);
}

/* ---------------------------------------------------------------- screens */

static void streams_screen(const char *type, const char *vid, const char *title) {
    status("Finding streams...");
    int n = 0, skipped = 0;
    Stream *s = streams_fetch(type, vid, &n, &skipped);
    if (n == 0) {
        char msg[400];
        if (skipped)
            snprintf(msg, sizeof msg, "No playable streams.\n\n%d other stream(s) were offered in formats this app can't play (YouTube / external links).", skipped);
        else
            snprintf(msg, sizeof msg, "No streams found.\n\nSign in under Settings to sync the addons on your Stremio account, or add a stream addon by URL.");
        message(title, msg);
        streams_free(s, n);
        return;
    }
    char **labels = malloc(sizeof(char *) * (size_t)n);
    for (int i = 0; i < n; i++) labels[i] = s[i].label;
    char hdr[300];
    if (skipped) snprintf(hdr, sizeof hdr, "%s  (%d hidden)", title, skipped);
    else snprintf(hdr, sizeof hdr, "%s", title);

    int sel = 0;
    while (!g_quit) {
        sel = pick_list(hdr, labels, n, sel);
        if (sel < 0) break;
        play_stream(&s[sel]);
    }
    free(labels);
    streams_free(s, n);
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void episodes_flow(const Item *it, const Item *meta, Video *vs, int nv) {
    int *seasons = malloc(sizeof(int) * (size_t)nv), ns = 0;
    for (int i = 0; i < nv; i++) {
        int found = 0;
        for (int k = 0; k < ns; k++) if (seasons[k] == vs[i].season) { found = 1; break; }
        if (!found) seasons[ns++] = vs[i].season;
    }
    qsort(seasons, (size_t)ns, sizeof(int), cmp_int);

    int ssel = 0;
    while (!g_quit) {
        int season;
        if (ns == 1) {
            season = seasons[0];
        } else {
            char **sl = malloc(sizeof(char *) * (size_t)ns);
            for (int i = 0; i < ns; i++) {
                sl[i] = malloc(32);
                if (seasons[i] == 0) snprintf(sl[i], 32, "Specials");
                else snprintf(sl[i], 32, "Season %d", seasons[i]);
            }
            ssel = pick_list(meta->name, sl, ns, ssel);
            for (int i = 0; i < ns; i++) free(sl[i]);
            free(sl);
            if (ssel < 0) break;
            season = seasons[ssel];
        }

        int *map = malloc(sizeof(int) * (size_t)nv), ne = 0;
        for (int i = 0; i < nv; i++) if (vs[i].season == season) map[ne++] = i;
        char **el = malloc(sizeof(char *) * (size_t)ne);
        for (int i = 0; i < ne; i++) {
            const Video *v = &vs[map[i]];
            el[i] = malloc(300);
            snprintf(el[i], 300, "%02d  %s", v->episode, v->title[0] ? v->title : "Episode");
        }
        int esel = 0;
        while (!g_quit) {
            esel = pick_list(season == 0 ? "Specials" : "Episodes", el, ne, esel);
            if (esel < 0) break;
            const Video *v = &vs[map[esel]];
            char t[300];
            snprintf(t, sizeof t, "%s  S%02dE%02d", meta->name, v->season, v->episode);
            streams_screen(it->type, v->id, t);
        }
        for (int i = 0; i < ne; i++) free(el[i]);
        free(el); free(map);
        if (ns == 1) break;
    }
    free(seasons);
}

static void detail_screen(Item *it) {
    status("Loading details...");
    Item meta;
    Video *vs = NULL;
    int nv = 0;
    memset(&meta, 0, sizeof meta);
    if (!meta_fetch(it->owner, it->type, it->id, &meta, &vs, &nv)) {
        meta.name = strdup(it->name);
        meta.desc = strdup("");
        meta.year = strdup(it->year ? it->year : "");
        meta.id = strdup(it->id);
        meta.type = strdup(it->type);
    }
    int is_series = (nv > 0 && strcmp(it->type, "movie") != 0);

    for (;;) {
        clear_screen();
        header(meta.name);
        draw_poster(it, 16, 62);
        int x = 16 + PW + 20, w = SW - x - 16;
        int y = 62;
        if (meta.year[0]) { text(f_mid, meta.year, x, y, C_DIM); y += 30; }
        if (meta.desc[0]) text_wrap(f_small, meta.desc, x, y, w, 15, C_TXT);
        else text(f_small, "No description.", x, y, C_DIM);
        footer(is_series ? "A episodes    B back" : "A watch    B back");
        SDL_Flip(screen);

        int k = wait_key();
        if (k == K_QUIT) { g_quit = 1; break; }
        if (k == K_B) break;
        if (k == K_A) {
            if (is_series) episodes_flow(it, &meta, vs, nv);
            else streams_screen(it->type, it->id, meta.name);
            if (g_quit) break;
        }
    }
    item_clear(&meta);
    videos_free(vs, nv);
}

static void draw_grid(const char *title, Item *items, int n, int sel, int top) {
    clear_screen();
    header(title);
    const int x0 = 10, y0 = 52, cw = 124, ch = 196;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int i = (top + r) * COLS + c;
            if (i >= n) break;
            int x = x0 + c * cw, y = y0 + r * ch;
            if (i == sel) fill(x - 3, y - 3, PW + 6, ch - 4, rgb(123, 91, 245));
            draw_poster(&items[i], x, y);
            text_fit(f_small, items[i].name, x, y + PH + 3, PW, i == sel ? C_ACC : C_TXT);
        }
    }
    char pos[64];
    snprintf(pos, sizeof pos, "%d / %d    A open    B back", sel + 1, n);
    footer(pos);
    SDL_Flip(screen);
}

static void free_items_with_posters(Item *items, int n) {
    for (int i = 0; i < n; i++) if (items[i].img) SDL_FreeSurface((SDL_Surface *)items[i].img);
    items_free(items, n);
}

/* Poster grid over an already-fetched item list (does not free it). */
static void grid_view(const char *title, Item *items, int n) {
    int sel = 0, top = 0;
    ensure_posters(items, 0, n < COLS * ROWS ? n : COLS * ROWS);
    while (!g_quit) {
        draw_grid(title, items, n, sel, top);
        switch (wait_key()) {
        case K_LEFT: if (sel > 0) sel--; break;
        case K_RIGHT: if (sel < n - 1) sel++; break;
        case K_UP: if (sel >= COLS) sel -= COLS; break;
        case K_DOWN: sel += COLS; if (sel >= n) sel = n - 1; break;
        case K_L: sel -= COLS * ROWS; if (sel < 0) sel = 0; break;
        case K_R: sel += COLS * ROWS; if (sel >= n) sel = n - 1; break;
        case K_A: detail_screen(&items[sel]); break;
        case K_B: return;
        case K_QUIT: g_quit = 1; return;
        default: break;
        }
        int row = sel / COLS;
        if (row < top) top = row;
        if (row >= top + ROWS) top = row - ROWS + 1;
        int from = top * COLS, to = (top + ROWS) * COLS;
        if (to > n) to = n;
        ensure_posters(items, from, to);
    }
}

static void catalog_screen(const Addon *a, const Cat *c) {
    status("Loading catalog...");
    int n = 0;
    Item *items = catalog_fetch(a, c, NULL, &n);
    if (!items || n == 0) {
        message("Catalog", "Couldn't load this catalog.\n\nCheck Wi-Fi and the addon in Settings.");
        items_free(items, n);
        return;
    }
    char title[300];
    snprintf(title, sizeof title, "%s - %s (%s)", a->name, c->name, c->type);
    grid_view(title, items, n);
    free_items_with_posters(items, n);
}

static void search_flow(void) {
    char q[128];
    if (!osk("Search", "", q, sizeof q, 0) || !q[0]) return;
    status("Searching...");

    Item *all = NULL;
    int total = 0;
    for (int i = 0; i < g_naddons; i++) {
        for (int j = 0; j < g_addons[i].ncat; j++) {
            const Cat *c = &g_addons[i].cats[j];
            if (!c->searchable) continue;
            int k = 0;
            Item *r = catalog_fetch(&g_addons[i], c, q, &k);
            for (int m = 0; m < k; m++) {
                int dup = 0;
                for (int t = 0; t < total; t++)
                    if (!strcmp(all[t].id, r[m].id) && !strcmp(all[t].type, r[m].type)) { dup = 1; break; }
                if (dup || total >= 60) { item_clear(&r[m]); continue; }
                all = realloc(all, sizeof(Item) * (size_t)(total + 1));
                all[total++] = r[m];   /* strings move with the struct */
            }
            free(r);                   /* array only; contents moved or cleared */
        }
    }
    if (total == 0) {
        char msg[300];
        snprintf(msg, sizeof msg, "No results for \"%s\".", q);
        message("Search", msg);
        return;
    }
    char title[200];
    snprintf(title, sizeof title, "Search: %s", q);
    grid_view(title, all, total);
    free_items_with_posters(all, total);
}

static void magnet_flow(void) {
    char in[1024];
    if (!osk("Magnet link or 40-char infoHash", "magnet:?xt=urn:btih:", in, sizeof in, 0) || !in[0]) return;
    char hash[64] = "";
    char *tr = NULL;
    if (!magnet_parse(in, hash, &tr)) {
        size_t l = strlen(in);
        int ok = (l == 40);
        for (size_t i = 0; ok && i < l; i++) {
            char ch = in[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) ok = 0;
        }
        if (!ok) { message("Torrent", "That doesn't look like a magnet link or infoHash."); return; }
        for (size_t i = 0; i < 40; i++) hash[i] = (char)((in[i] >= 'A' && in[i] <= 'F') ? in[i] + 32 : in[i]);
        hash[40] = 0;
    }
    play_torrent(hash, -1, tr);
    free(tr);
}

/* ---------------------------------------------------------------- settings */

static void reload_addons(void) {
    status("Loading addons...");
    addons_clear();
    addons_load(MANUAL_ADDONS_FILE);
    addons_load(ACCOUNT_ADDONS_FILE);
}

static void do_sync(void) {
    char err[200];
    status("Syncing addons from your account...");
    int n = account_sync(err, sizeof err);
    if (n < 0) { message("Sync failed", err); return; }
    reload_addons();
    char msg[200];
    snprintf(msg, sizeof msg, "Synced %d addon(s) from your Stremio account.", n);
    message("Sync", msg);
}

static void sign_in(void) {
    char email[200], pass[200], err[200];
    if (!osk("Stremio email", "", email, sizeof email, 0) || !email[0]) return;
    if (!osk("Stremio password", "", pass, sizeof pass, 1)) return;
    status("Signing in...");
    int ok = account_login(email, pass, err, sizeof err);
    memset(pass, 0, sizeof pass);
    if (!ok) { message("Sign in failed", err); return; }
    do_sync();
}

static void settings_screen(void) {
    while (!g_quit) {
        char email[256] = "";
        int signed_in = account_load(email, sizeof email);
        char *srv = torrent_server();

        char *labels[5];
        for (int i = 0; i < 5; i++) labels[i] = malloc(400);
        if (signed_in) snprintf(labels[0], 400, "Stremio account: %s  (sign out)", email[0] ? email : "signed in");
        else snprintf(labels[0], 400, "Sign in to your Stremio account");
        snprintf(labels[1], 400, signed_in ? "Sync addons from account" : "Sync addons (sign in first)");
        snprintf(labels[2], 400, "Add addon by URL");
        snprintf(labels[3], 400, "Torrent server: %s", srv ? srv : "not set");
        snprintf(labels[4], 400, "Installed addons (%d)", g_naddons);

        static int sel = 0;
        int pick = pick_list("Settings", labels, 5, sel);
        for (int i = 0; i < 5; i++) free(labels[i]);
        free(srv);
        if (pick < 0) return;
        sel = pick;

        if (pick == 0) {
            if (!signed_in) sign_in();
            else {
                char *opts[2] = {"Sign out and forget synced addons", "Cancel"};
                if (pick_list("Sign out?", opts, 2, 1) == 0) { account_logout(); reload_addons(); }
            }
        } else if (pick == 1) {
            if (!signed_in) message("Sync", "Sign in first.");
            else do_sync();
        } else if (pick == 2) {
            char url[512];
            if (osk("Addon manifest URL", "https://", url, sizeof url, 0) && strlen(url) > 9) {
                if (addon_add_url(url)) { reload_addons(); message("Addon", "Added. It appears in the menu if it has catalogs."); }
                else message("Addon", "Must start with http://, https:// or stremio://");
            }
        } else if (pick == 3) {
            char cur[300] = "http://";
            char *old = torrent_server();
            if (old) { snprintf(cur, sizeof cur, "%s", old); free(old); }
            char url[300];
            if (osk("Torrent server URL (stremio-service)", cur, url, sizeof url, 0)) {
                if (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)) torrent_server_save(url);
                else message("Torrent server", "Must start with http:// or https://, e.g. http://192.168.1.20:11470");
            }
        } else if (pick == 4) {
            char msg[1500] = "";
            for (int i = 0; i < g_naddons && strlen(msg) < 1200; i++) {
                strncat(msg, g_addons[i].name, 80);
                strcat(msg, g_addons[i].stream ? "  [streams]\n" : "\n");
            }
            message("Installed addons", g_naddons ? msg : "None yet.");
        }
    }
}

/* ---------------------------------------------------------------- main */

typedef struct { int kind; const Addon *a; const Cat *c; } Entry; /* 0 search 1 magnet 2 settings 3 catalog */

int main(void) {
    if (!ui_init()) {
        fprintf(stderr, "SDL/TTF init failed (is %s present?)\n", FONT_PATH);
        return 1;
    }
    reload_addons();

    int sel = 0;
    while (!g_quit) {
        int total = 3;
        for (int i = 0; i < g_naddons; i++)
            for (int j = 0; j < g_addons[i].ncat; j++) if (g_addons[i].cats[j].browsable) total++;

        char **labels = malloc(sizeof(char *) * (size_t)total);
        Entry *en = malloc(sizeof(Entry) * (size_t)total);
        int k = 0;
        const char *fixed[3] = {"Search", "Play magnet link / infoHash", "Settings (account, addons, torrent server)"};
        for (int f = 0; f < 3; f++) {
            labels[k] = strdup(fixed[f]);
            en[k].kind = f; en[k].a = NULL; en[k].c = NULL;
            k++;
        }
        for (int i = 0; i < g_naddons; i++) {
            for (int j = 0; j < g_addons[i].ncat; j++) {
                const Cat *c = &g_addons[i].cats[j];
                if (!c->browsable) continue;
                labels[k] = malloc(400);
                snprintf(labels[k], 400, "%s  -  %s  (%s)", g_addons[i].name, c->name, c->type);
                en[k].kind = 3; en[k].a = &g_addons[i]; en[k].c = c;
                k++;
            }
        }

        sel = pick_list("Stremio Mini", labels, total, sel);
        for (int i = 0; i < total; i++) free(labels[i]);
        free(labels);
        if (sel < 0) { free(en); break; }

        Entry e = en[sel];
        free(en);
        if (e.kind == 0) search_flow();
        else if (e.kind == 1) magnet_flow();
        else if (e.kind == 2) settings_screen();   /* may reload addons; menu is rebuilt */
        else catalog_screen(e.a, e.c);
    }

    ui_shutdown();
    return 0;
}

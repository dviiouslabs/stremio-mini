#define _GNU_SOURCE
#include "torrent.h"
#include "net.h"
#include <ctype.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int b32val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

int magnet_parse(const char *m, char *hash, char **tr_query) {
    if (tr_query) *tr_query = NULL;
    if (!m || strncasecmp(m, "magnet:", 7)) return 0;
    const char *p = strcasestr(m, "xt=urn:btih:");
    if (!p) return 0;
    p += 12;
    size_t n = strcspn(p, "&");
    if (n == 40) {
        for (size_t i = 0; i < 40; i++) {
            if (hexval(p[i]) < 0) return 0;
            hash[i] = (char)tolower((unsigned char)p[i]);
        }
        hash[40] = 0;
    } else if (n == 32) {
        unsigned char raw[20] = {0};
        int bits = 0, nbytes = 0;
        unsigned acc = 0;
        for (size_t i = 0; i < 32; i++) {
            int v = b32val(p[i]);
            if (v < 0) return 0;
            acc = (acc << 5) | (unsigned)v;
            bits += 5;
            if (bits >= 8) { bits -= 8; if (nbytes < 20) raw[nbytes++] = (unsigned char)((acc >> bits) & 0xFF); }
        }
        for (int i = 0; i < 20; i++) sprintf(hash + i * 2, "%02x", raw[i]);
    } else {
        return 0;
    }

    if (tr_query) {
        size_t cap = strlen(m) + 8, used = 0;
        char *out = calloc(1, cap);
        int cnt = 0;
        for (const char *q = m; (q = strchr(q, '&')) != NULL; ) {
            q++;
            if (!strncmp(q, "tr=", 3) && cnt < 8) {
                size_t l = strcspn(q, "&");
                used += (size_t)snprintf(out + used, cap - used, "%s%.*s", cnt ? "&" : "", (int)l, q);
                cnt++;
            }
        }
        if (cnt) *tr_query = out; else free(out);
    }
    return 1;
}

char *torrent_server(void) {
    FILE *f = fopen(SERVER_FILE, "r");
    if (!f) return NULL;
    char line[512];
    char *res = NULL;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t n = strlen(s);
        while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
        if (!n || s[0] == '#') continue;
        while (n && s[n - 1] == '/') s[--n] = 0;
        if (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8)) res = strdup(s);
        break;
    }
    fclose(f);
    return res;
}

int torrent_server_save(const char *base) {
    FILE *f = fopen(SERVER_FILE, "w");
    if (!f) return 0;
    fprintf(f, "# Stremio streaming server on your network (stremio-service, default port 11470)\n%s\n", base);
    fclose(f);
    return 1;
}

char *torrent_url(const char *base, const char *hash, int idx, const char *tr_query) {
    size_t n = strlen(base) + strlen(hash) + (tr_query ? strlen(tr_query) : 0) + 32;
    char *u = malloc(n);
    snprintf(u, n, "%s/%s/%d%s%s", base, hash, idx, tr_query ? "?" : "", tr_query ? tr_query : "");
    return u;
}

char *torrent_helper_url(const char *hash, const char *tr_query) {
    if (access("./torrent.sh", X_OK) != 0) return NULL;
    char magnet[2048];
    snprintf(magnet, sizeof magnet, "magnet:?xt=urn:btih:%s", hash);
    if (tr_query) {
        /* tr values are already percent-encoded */
        size_t l = strlen(magnet);
        snprintf(magnet + l, sizeof magnet - l, "&%s", tr_query);
    }
    char *q = net_sq(magnet);
    char cmd[2300];
    snprintf(cmd, sizeof cmd, "./torrent.sh %s 2>/dev/null", q);
    free(q);
    FILE *f = popen(cmd, "r");
    if (!f) return NULL;
    char line[1024] = "";
    char *res = NULL;
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "http", 4)) res = strdup(line);
    }
    pclose(f);
    return res;
}

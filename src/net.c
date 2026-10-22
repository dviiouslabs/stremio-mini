#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_BODY (8u * 1024u * 1024u)

static int use_curl = -1;
static int insecure = 0;

static void detect(void) {
    if (use_curl != -1) return;
    use_curl = (system("command -v curl >/dev/null 2>&1") == 0);
    const char *e = getenv("STREMIO_INSECURE");
    insecure = (e && e[0] == '1');
}

char *net_sq(const char *s) {
    size_t n = strlen(s), extra = 0;
    for (size_t i = 0; i < n; i++) if (s[i] == '\'') extra += 3;
    char *o = malloc(n + extra + 3);
    if (!o) return NULL;
    char *w = o;
    *w++ = '\'';
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\'') { memcpy(w, "'\\''", 4); w += 4; }
        else *w++ = s[i];
    }
    *w++ = '\'';
    *w = 0;
    return o;
}

char *net_urlenc(const char *s) {
    static const char *keep = "-_.!~*'()";
    size_t n = strlen(s);
    char *o = malloc(n * 3 + 1), *w = o;
    if (!o) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            (c && strchr(keep, c))) {
            *w++ = (char)c;
        } else {
            w += sprintf(w, "%%%02X", c);
        }
    }
    *w = 0;
    return o;
}

/* Dynamic string builder */
typedef struct { char *b; size_t n, cap; } sb;

static void sb_add(sb *s, const char *t) {
    size_t l = strlen(t);
    if (s->n + l + 1 > s->cap) {
        s->cap = (s->n + l + 1) * 2;
        s->b = realloc(s->b, s->cap);
    }
    memcpy(s->b + s->n, t, l + 1);
    s->n += l;
}

static void fetch_prefix(sb *s, int timeout, const char *outfile_q) {
    char tmp[160];
    if (use_curl) {
        snprintf(tmp, sizeof tmp, "curl -fsSL%s --max-time %d -A 'StremioMini/0.1' ",
                 insecure ? " -k" : "", timeout);
        sb_add(s, tmp);
        if (outfile_q) { sb_add(s, "-o "); sb_add(s, outfile_q); sb_add(s, " "); }
    } else {
        snprintf(tmp, sizeof tmp, "wget -q%s -T %d -U 'StremioMini/0.1' ",
                 insecure ? " --no-check-certificate" : "", timeout);
        sb_add(s, tmp);
        sb_add(s, "-O ");
        sb_add(s, outfile_q ? outfile_q : "-");
        sb_add(s, " ");
    }
}

/* Run a shell command and capture stdout. */
static char *capture(const char *cmd, int require_ok) {
    FILE *f = popen(cmd, "r");
    if (!f) return NULL;
    size_t cap = 16384, n = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(f); return NULL; }
    for (;;) {
        if (n + 4096 + 1 > cap) {
            if (cap >= MAX_BODY) break;
            cap *= 2;
            char *t = realloc(buf, cap);
            if (!t) break;
            buf = t;
        }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        if (r == 0) break;
        n += r;
    }
    int rc = pclose(f);
    buf[n] = 0;
    if (n == 0 || (require_ok && rc != 0)) { free(buf); return NULL; }
    return buf;
}

char *net_get(const char *url) {
    detect();
    char *q = net_sq(url);
    if (!q) return NULL;
    sb cmd = {0};
    fetch_prefix(&cmd, 20, NULL);
    sb_add(&cmd, q);
    sb_add(&cmd, " 2>/dev/null");
    free(q);
    char *r = capture(cmd.b, 1);
    free(cmd.b);
    return r;
}

char *net_post_json(const char *url, const char *body) {
    detect();
    const char *tmp = ".post.tmp"; /* body may hold a password: 0600, deleted right after */
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return NULL;
    size_t bl = strlen(body);
    if (write(fd, body, bl) != (ssize_t)bl) { close(fd); unlink(tmp); return NULL; }
    close(fd);

    char *q = net_sq(url);
    sb cmd = {0};
    char tmpbuf[200];
    if (use_curl) {
        snprintf(tmpbuf, sizeof tmpbuf,
                 "curl -sSL%s --max-time 25 -A 'StremioMini/0.1' -H 'Content-Type: application/json' --data '@%s' ",
                 insecure ? " -k" : "", tmp);
    } else {
        snprintf(tmpbuf, sizeof tmpbuf,
                 "wget -q%s -T 25 -U 'StremioMini/0.1' -O- --header='Content-Type: application/json' --post-file='%s' ",
                 insecure ? " --no-check-certificate" : "", tmp);
    }
    sb_add(&cmd, tmpbuf);
    sb_add(&cmd, q);
    sb_add(&cmd, " 2>/dev/null");
    free(q);
    char *r = capture(cmd.b, 0);
    free(cmd.b);
    unlink(tmp);
    return r;
}

void net_download_many(const char **urls, const char **paths, int n) {
    detect();
    if (n <= 0) return;
    sb cmd = {0};
    sb_add(&cmd, "( ");
    for (int i = 0; i < n; i++) {
        char *qu = net_sq(urls[i]);
        char tmpname[1100];
        snprintf(tmpname, sizeof tmpname, "%s.tmp", paths[i]);
        char *qt = net_sq(tmpname);
        char *qp = net_sq(paths[i]);
        sb_add(&cmd, "( ");
        fetch_prefix(&cmd, 12, qt);
        sb_add(&cmd, qu);
        sb_add(&cmd, " && mv ");
        sb_add(&cmd, qt);
        sb_add(&cmd, " ");
        sb_add(&cmd, qp);
        sb_add(&cmd, " || rm -f ");
        sb_add(&cmd, qt);
        sb_add(&cmd, " ) & ");
        free(qu); free(qt); free(qp);
    }
    sb_add(&cmd, "wait ) >/dev/null 2>&1");
    (void)system(cmd.b);
    free(cmd.b);
}

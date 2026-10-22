#include "account.h"
#include "json.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *api_base(void) {
    const char *e = getenv("STREMIO_API");
    return (e && *e) ? e : "https://api.strem.io";
}

static void set_err(char *err, size_t en, const char *msg) {
    if (err && en) snprintf(err, en, "%s", msg);
}

/* Pull a readable message out of {"error":{"message":..}} / {"error":".."} */
static void api_error(const jv *r, char *err, size_t en, const char *fallback) {
    const jv *e = r ? jget(r, "error") : NULL;
    const char *m = NULL;
    if (e) m = (e->t == J_STR) ? e->s : jstr(jget(e, "message"), NULL);
    set_err(err, en, m ? m : fallback);
}

int account_load(char *email, size_t n) {
    FILE *f = fopen(AUTH_FILE, "r");
    if (!f) return 0;
    char key[512] = "", mail[256] = "";
    if (!fgets(key, sizeof key, f) || key[0] == '\n') { fclose(f); return 0; }
    if (fgets(mail, sizeof mail, f)) mail[strcspn(mail, "\r\n")] = 0;
    fclose(f);
    if (email && n) snprintf(email, n, "%s", mail);
    return 1;
}

static int read_auth_key(char *key, size_t n) {
    FILE *f = fopen(AUTH_FILE, "r");
    if (!f) return 0;
    int ok = fgets(key, (int)n, f) != NULL;
    fclose(f);
    if (ok) key[strcspn(key, "\r\n")] = 0;
    return ok && key[0];
}

int account_login(const char *email, const char *password, char *err, size_t en) {
    char *qe = json_quote(email), *qp = json_quote(password);
    size_t bl = strlen(qe) + strlen(qp) + 96;
    char *body = malloc(bl);
    snprintf(body, bl, "{\"type\":\"Login\",\"email\":%s,\"password\":%s,\"facebook\":false}", qe, qp);
    free(qe); free(qp);

    char url[300];
    snprintf(url, sizeof url, "%s/api/login", api_base());
    char *resp = net_post_json(url, body);
    memset(body, 0, bl);   /* don't leave the password lying around */
    free(body);
    if (!resp) { set_err(err, en, "No response (check Wi-Fi)"); return 0; }

    jv *r = json_parse(resp);
    free(resp);
    const char *key = jstr(jget(jget(r, "result"), "authKey"), NULL);
    if (!key || !*key) {
        api_error(r, err, en, "Login failed");
        json_free(r);
        return 0;
    }
    int fd = open(AUTH_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int ok = 0;
    if (fd >= 0) {
        FILE *f = fdopen(fd, "w");
        if (f) { fprintf(f, "%s\n%s\n", key, email); fclose(f); ok = 1; }
        else close(fd);
    }
    if (!ok) set_err(err, en, "Couldn't save session");
    json_free(r);
    return ok;
}

void account_logout(void) {
    unlink(AUTH_FILE);
    unlink(ACCOUNT_ADDONS_FILE);
}

int account_sync(char *err, size_t en) {
    char key[512];
    if (!read_auth_key(key, sizeof key)) { set_err(err, en, "Not signed in"); return -1; }
    char *qk = json_quote(key);
    size_t bl = strlen(qk) + 64;
    char *body = malloc(bl);
    snprintf(body, bl, "{\"type\":\"AddonCollectionGet\",\"authKey\":%s,\"update\":true}", qk);
    free(qk);

    char url[300];
    snprintf(url, sizeof url, "%s/api/addonCollectionGet", api_base());
    char *resp = net_post_json(url, body);
    free(body);
    if (!resp) { set_err(err, en, "No response (check Wi-Fi)"); return -1; }
    jv *r = json_parse(resp);
    free(resp);

    const jv *addons = jget(jget(r, "result"), "addons");
    if (!addons || addons->t != J_ARR) {
        api_error(r, err, en, "Unexpected reply from Stremio");
        json_free(r);
        return -1;
    }
    FILE *f = fopen(ACCOUNT_ADDONS_FILE, "w");
    if (!f) { set_err(err, en, "Can't write addon list"); json_free(r); return -1; }
    fputs("# Synced from your Stremio account - overwritten on every sync.\n", f);
    int n = 0;
    for (int i = 0; i < addons->len; i++) {
        const char *u = jstr(jget(jat(addons, i), "transportUrl"), NULL);
        if (!u || (strncmp(u, "http", 4) && strncmp(u, "stremio://", 10))) continue;
        fprintf(f, "%s\n", u);
        n++;
    }
    fclose(f);
    json_free(r);
    return n;
}

int addon_add_url(const char *url) {
    if (!url || (strncmp(url, "http://", 7) && strncmp(url, "https://", 8) && strncmp(url, "stremio://", 10)))
        return 0;
    FILE *f = fopen(MANUAL_ADDONS_FILE, "a");
    if (!f) return 0;
    fprintf(f, "%s\n", url);
    fclose(f);
    return 1;
}

/* Host-side tests of json/net/addon/account/torrent against test/mock_addon.py.
 * Run with: make test */
#include "../src/account.h"
#include "../src/addon.h"
#include "../src/json.h"
#include "../src/net.h"
#include "../src/torrent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void write_file(const char *path, const char *txt) { FILE *f = fopen(path, "w"); fputs(txt, f); fclose(f); }

int main(void) {
    system("rm -rf /tmp/claude-0/work && mkdir -p /tmp/claude-0/work");
    CHECK(chdir("/tmp/claude-0/work") == 0);
    setenv("STREMIO_API", "http://127.0.0.1:8765", 1);

    /* ---- json ---- */
    jv *j = json_parse("{\"a\":[1,2.5,\"x\\u00e9\\ud83d\\ude00\\n\"],\"b\":true,\"c\":null,\"d\":{\"e\":\"q\\\"\"}}");
    CHECK(j && jlen(j) == 4);
    CHECK(jnum(jat(jget(j, "a"), 1), 0) == 2.5);
    CHECK(!strcmp(jstr(jat(jget(j, "a"), 2), ""), "x\xc3\xa9\xf0\x9f\x98\x80\n"));
    CHECK(jtrue(jget(j, "b")));
    CHECK(!strcmp(jstr(jget(jget(j, "d"), "e"), ""), "q\""));
    json_free(j);
    CHECK(json_parse("{\"a\":") == NULL && json_parse("[1,2,") == NULL && json_parse("") == NULL);
    char *qq = json_quote("p\"w\\d\n\t\x01");
    CHECK(!strcmp(qq, "\"p\\\"w\\\\d\\n\\t\\u0001\""));
    j = json_parse(qq);                      /* round trip */
    CHECK(j && !strcmp(jstr(j, ""), "p\"w\\d\n\t\x01"));
    json_free(j); free(qq);

    /* ---- net helpers ---- */
    char *e = net_urlenc("tt1:1:2 a/b");
    CHECK(!strcmp(e, "tt1%3A1%3A2%20a%2Fb")); free(e);
    char *q = net_sq("it's"); CHECK(!strcmp(q, "'it'\\''s'")); free(q);

    /* ---- addon protocol ---- */
    write_file("addons.txt", "# comment\n\nhttp://127.0.0.1:8765/manifest.json\nhttp://127.0.0.1:9/dead\n");
    CHECK(addons_load("addons.txt") == 1);
    CHECK(addons_load("addons.txt") == 1);   /* duplicate base is skipped */
    CHECK(!strcmp(g_addons[0].name, "Mock \xc3\xa9" "ddon"));
    CHECK(g_addons[0].ncat == 3);            /* NeedsGenre skipped */
    CHECK(g_addons[0].cats[0].browsable && g_addons[0].cats[0].searchable);
    CHECK(!g_addons[0].cats[1].searchable);
    CHECK(!g_addons[0].cats[2].browsable && g_addons[0].cats[2].searchable);
    CHECK(g_addons[0].stream == 1);

    int n = 0;
    Item *it = catalog_fetch(&g_addons[0], &g_addons[0].cats[0], NULL, &n);
    CHECK(n == 2 && it[0].owner == &g_addons[0]);
    CHECK(!strcmp(it[0].year, "2020") && !strcmp(it[1].year, "2021"));
    CHECK(!strcmp(it[1].name, "O'Reilly \"Quoted\" Movie"));

    const char *urls[2] = {it[0].poster, "http://127.0.0.1:8765/missing"};
    const char *paths[2] = {"p1.img", "p2.img"};
    net_download_many(urls, paths, 2);
    CHECK(access("p1.img", R_OK) == 0 && access("p2.img", R_OK) != 0 && access("p2.img.tmp", R_OK) != 0);
    items_free(it, n);

    /* search incl. spaces/special chars must be percent-encoded and decode server-side */
    it = catalog_fetch(&g_addons[0], &g_addons[0].cats[0], "star wars & co", &n);
    CHECK(n == 1 && !strcmp(it[0].name, "Result for star wars & co"));
    items_free(it, n);
    it = catalog_fetch(&g_addons[0], &g_addons[0].cats[2], "x", &n);
    CHECK(n == 1);
    items_free(it, n);

    Item m; Video *vs = NULL; int nv = 0;
    CHECK(meta_fetch(&g_addons[0], "movie", "tt1", &m, &vs, &nv));
    CHECK(nv == 0 && !strcmp(m.desc, "A film.\nWith lines."));
    item_clear(&m);
    CHECK(meta_fetch(&g_addons[0], "series", "ttS", &m, &vs, &nv));
    CHECK(nv == 3 && vs[1].season == 1 && vs[1].episode == 2 && !strcmp(vs[1].title, "Second"));
    item_clear(&m); videos_free(vs, nv);
    CHECK(!meta_fetch(&g_addons[0], "movie", "nope", &m, &vs, &nv));

    int sk = 0;
    Stream *s = streams_fetch("movie", "tt1", &n, &sk);
    CHECK(n == 3 && sk == 2);                /* ytId + ftp skipped */
    CHECK(s[0].kind == STREAM_URL && !strcmp(s[0].url, "https://example.com/a.mp4"));
    CHECK(!strcmp(s[0].label, "Direct - First line"));
    CHECK(s[1].kind == STREAM_TORRENT && !strcmp(s[1].hash, "abcdef0123456789abcdef0123456789abcdef01")); /* lower-cased */
    CHECK(s[1].file_idx == 2);
    CHECK(s[1].tr_query && !strcmp(s[1].tr_query, "tr=udp%3A%2F%2Ft.example%3A1337%2Fannounce"));
    CHECK(!strcmp(s[1].label, "[torrent] Tor - Seeds 50"));
    CHECK(s[2].kind == STREAM_URL && !strcmp(s[2].label, "Mock \xc3\xa9" "ddon"));
    streams_free(s, n);
    s = streams_fetch("series", "ttS:1:2", &n, &sk);
    CHECK(n == 3);
    streams_free(s, n);
    s = streams_fetch("tv", "x", &n, &sk);
    CHECK(n == 0); streams_free(s, n);

    /* ---- torrent helpers ---- */
    char hash[64]; char *tr = NULL;
    CHECK(magnet_parse("magnet:?xt=urn:btih:ABCDEF0123456789ABCDEF0123456789ABCDEF01&dn=x&tr=udp%3A%2F%2Fa%3A1&tr=http%3A%2F%2Fb%2Fann", hash, &tr));
    CHECK(!strcmp(hash, "abcdef0123456789abcdef0123456789abcdef01"));
    CHECK(tr && !strcmp(tr, "tr=udp%3A%2F%2Fa%3A1&tr=http%3A%2F%2Fb%2Fann"));
    free(tr);
    CHECK(magnet_parse("magnet:?xt=urn:btih:AERUKZ4JVPG66AJDIVTYTK6N54ASGRLH", hash, &tr));  /* base32 */
    CHECK(!strcmp(hash, "0123456789abcdef0123456789abcdef01234567") && tr == NULL);
    CHECK(!magnet_parse("magnet:?xt=urn:btih:short", hash, &tr));
    CHECK(!magnet_parse("http://nope", hash, &tr));
    CHECK(torrent_server() == NULL);
    CHECK(torrent_server_save("http://192.168.1.20:11470/"));
    char *base = torrent_server();
    CHECK(base && !strcmp(base, "http://192.168.1.20:11470"));
    char *tu = torrent_url(base, hash, -1, "tr=x");
    CHECK(!strcmp(tu, "http://192.168.1.20:11470/0123456789abcdef0123456789abcdef01234567/-1?tr=x"));
    free(tu); free(base);
    CHECK(torrent_helper_url(hash, NULL) == NULL);   /* no torrent.sh */

    /* ---- account ---- */
    char err[200] = "", mail[100] = "";
    CHECK(!account_load(mail, sizeof mail));
    CHECK(!account_login("a@b.c", "wrong", err, sizeof err) && !strcmp(err, "Wrong e-mail or password"));
    CHECK(access(".post.tmp", F_OK) != 0);   /* password temp file removed */
    CHECK(account_sync(err, sizeof err) == -1 && !strcmp(err, "Not signed in"));
    CHECK(account_login("a@b.c", "p\"w\\d", err, sizeof err));
    struct stat st; CHECK(stat(AUTH_FILE, &st) == 0 && (st.st_mode & 077) == 0);
    CHECK(account_load(mail, sizeof mail) && !strcmp(mail, "a@b.c"));
    CHECK(access(".post.tmp", F_OK) != 0);
    CHECK(account_sync(err, sizeof err) == 2);   /* ftp:// entry dropped */
    char *all = NULL;
    { FILE *f = fopen(ACCOUNT_ADDONS_FILE, "r"); char buf[2000]; size_t r = fread(buf, 1, sizeof buf - 1, f); buf[r] = 0; fclose(f); all = strdup(buf); }
    CHECK(strstr(all, "http://127.0.0.1:8765/manifest.json") && strstr(all, "stremio://example.com/foo/manifest.json") && !strstr(all, "ftp:"));
    free(all);
    CHECK(addon_add_url("https://x.example/manifest.json") && !addon_add_url("javascript:alert(1)"));
    account_logout();
    CHECK(access(AUTH_FILE, F_OK) != 0 && access(ACCOUNT_ADDONS_FILE, F_OK) != 0);

    addons_clear();
    CHECK(g_naddons == 0 && addons_load("addons.txt") == 1);   /* reload works after clear */
    addons_clear();

    printf(fails ? "%d FAILURES\n" : "ALL PASSED\n", fails);
    return fails != 0;
}

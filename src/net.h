/* Tiny HTTP layer: shells out to curl (or wget) so we get HTTPS + CA handling
 * from whatever the device already has, with no extra libraries to link. */
#ifndef NET_H
#define NET_H

#include <stddef.h>

/* GET url, return malloc'd NUL-terminated body (max 8MB) or NULL. */
char *net_get(const char *url);

/* POST a JSON body (written to a 0600 temp file, never put on argv).
 * Returns the response body even for HTTP error statuses (curl), or NULL. */
char *net_post_json(const char *url, const char *body);

/* Download n files in parallel. Failed downloads leave no file behind. */
void net_download_many(const char **urls, const char **paths, int n);

/* Single-quote a string for /bin/sh. Caller frees. */
char *net_sq(const char *s);

/* encodeURIComponent-style escaping. Caller frees. */
char *net_urlenc(const char *s);

#endif

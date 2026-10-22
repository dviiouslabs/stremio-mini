/* Stremio account sync: sign in, pull the addons installed on your account.
 * Only the session authKey is stored (auth.txt) - never the password. */
#ifndef ACCOUNT_H
#define ACCOUNT_H
#include <stddef.h>

#define AUTH_FILE "auth.txt"
#define ACCOUNT_ADDONS_FILE "account_addons.txt"
#define MANUAL_ADDONS_FILE "addons.txt"

/* 1 if signed in (email copied out). */
int account_load(char *email, size_t n);
/* 1 on success; otherwise err holds a short reason. */
int account_login(const char *email, const char *password, char *err, size_t en);
void account_logout(void);
/* Pull addon collection into account_addons.txt. Returns count or -1 (err set). */
int account_sync(char *err, size_t en);
/* Append a manifest URL to addons.txt. 1 on success. */
int addon_add_url(const char *url);

#endif

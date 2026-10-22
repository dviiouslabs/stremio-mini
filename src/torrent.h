/* Torrent playback bridge.
 *
 * Running a full BitTorrent engine on the Miyoo Mini's CPU/RAM isn't realistic,
 * so torrents are streamed by a Stremio streaming server on your network
 * (stremio-service on a PC / Pi / NAS, default port 11470). We turn an
 * infoHash into the server's HTTP URL and hand that to ffplay.
 * Optional fallback: ./torrent.sh <magnet> may print a playable URL itself. */
#ifndef TORRENT_H
#define TORRENT_H

#define SERVER_FILE "server.txt"

/* Parse btih (40 hex or 32 base32) + trackers from a magnet link.
 * hash must hold 41 bytes. *tr_query malloc'd ("tr=a&tr=b") or NULL. */
int magnet_parse(const char *magnet, char *hash, char **tr_query);

/* Base URL from server.txt (malloc'd, no trailing slash) or NULL. */
char *torrent_server(void);
int torrent_server_save(const char *base);

/* base + "/<hash>/<idx>[?tr=..]" ; idx < 0 -> "-1" (server picks the main file). */
char *torrent_url(const char *base, const char *hash, int idx, const char *tr_query);

/* Fallback helper: run ./torrent.sh <magnet-or-hash>, take its first stdout line. */
char *torrent_helper_url(const char *hash, const char *tr_query);

#endif

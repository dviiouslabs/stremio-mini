# Stremio Mini (Miyoo Mini Plus / Onion OS)

A Stremio **addon client** in C + SDL 1.2: poster-grid catalogs, search with an on-screen
keyboard, series/episode picking, direct streams and torrent streams, played with ffplay
scaled to fit 640x480. Speaks the public Stremio addon protocol.

## Controls
D-pad move - A select - B back - L1/R1 page.
Keyboard: A type, B delete (cancel if empty), X shift/symbols, Y space, START ok, SELECT cancel.

## Menu
- **Search** - type a query; searches every addon catalog that supports search.
- **Play magnet link / infoHash** - type or paste-by-typing a magnet; plays via the torrent server.
- **Settings**
  - *Sign in to your Stremio account* - email + password via the keyboard. Only the session key
    is stored (`auth.txt`, mode 600); the password is never saved. Afterwards the addons you
    installed on your account at stremio.com are pulled into `account_addons.txt`
    (re-run *Sync* any time you change them on the website).
  - *Add addon by URL* - manifest URL -> appended to `addons.txt`.
  - *Torrent server* - see below.
- Then one entry per addon catalog.

## Playback (ffplay @ 480)
`player.sh` runs `ffplay -fs -x 640 -y 480 -vf scale=...:force_original_aspect_ratio=decrease`,
so video is never larger than 640x480. **ffplay is not bundled** - put a build that works on your
firmware at `App/Stremio/bin/ffplay` (or anywhere on PATH). Edit `W`/`H` in `player.sh` to change
the output size. Software decoding on this CPU is realistic for low-bitrate H.264 only.

## Torrents
A full BitTorrent engine doesn't fit the Mini's CPU/RAM, so torrent streams are played through a
**Stremio streaming server on your network** (e.g. stremio-service on a PC / Pi / NAS, port 11470).
Set it in Settings > Torrent server (or `server.txt`). The app builds
`http://server:11470/<infoHash>/<fileIdx>` and hands it to ffplay. Optional fallback: rename
`torrent.sh.example` to `torrent.sh` to plug in your own on-device helper.

## Build (device binary)
I couldn't cross-compile in the authoring sandbox. Use the Miyoo Mini toolchain:
- **GitHub Actions:** push this folder; `.github/workflows/build.yml` builds in
  `ghcr.io/shauninman/union-miyoomini-toolchain` and uploads `Stremio-Mini.zip`.
- **Docker:** `docker run --rm -v "$PWD":/root/workspace ghcr.io/shauninman/union-miyoomini-toolchain \
  sh -c 'cd /root/workspace && make && ./package.sh'`

Unzip into `SD:/App/` -> `SD:/App/Stremio/`. Needs SDL_image (JPEG) + SDL_ttf; `package.sh`
copies them from the toolchain into `lib/`. If it won't start, read `stremio.log`.
If HTTPS gives certificate errors, `launch.sh` has a (commented) switch that disables checking.

## Tests (host)
`make test`: JSON, HTTP, addon protocol, search, torrent URL/magnet parsing, and the account
login/sync flow, under ASan+UBSan against `test/mock_addon.py`. The SDL UI was only
syntax-checked, and the account/torrent endpoints were written from public docs and
tested against the mock, not the live Stremio service.

#!/bin/sh
# Called by Stremio Mini with the stream URL as $1.
# Plays with ffplay, scaled down to fit the Miyoo's 640x480 screen
# (frames are scaled to at most 640x480, keeping aspect ratio, so decoding
# and output stay cheap). The app releases the screen before running this
# script and takes it back when it exits.
#
# ffplay is NOT bundled: put a build for your firmware at bin/ffplay in this
# folder (or anywhere in PATH). See README.
URL="$1"
[ -z "$URL" ] && exit 2
cd "$(dirname "$0")" || exit 1
echo "$URL" > last_stream.txt

# Output size - edit for another resolution.
W=640
H=480
VF="scale=w=${W}:h=${H}:force_original_aspect_ratio=decrease"

for P in \
    "$PWD/bin/ffplay" \
    /mnt/SDCARD/.tmp_update/bin/ffplay \
    /mnt/SDCARD/App/*/ffplay \
    "$(command -v ffplay)"
do
    [ -n "$P" ] && [ -x "$P" ] || continue
    exec "$P" -fs -x "$W" -y "$H" -vf "$VF" -autoexit -framedrop -loglevel error \
         -fflags +genpts "$URL"
done

echo "ffplay not found. Stream URL saved to last_stream.txt" >&2
exit 3

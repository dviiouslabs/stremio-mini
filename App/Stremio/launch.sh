#!/bin/sh
# Onion OS launcher for Stremio Mini
cd "$(dirname "$0")" || exit 1

# Bundled libs first (see README), then the usual Onion / stock locations.
export LD_LIBRARY_PATH="$PWD/lib:/mnt/SDCARD/.tmp_update/lib:/mnt/SDCARD/miyoo/lib:/config/lib:/customer/lib:$LD_LIBRARY_PATH"

# Only if HTTPS fails with certificate errors because the device has no CA
# bundle. This turns OFF certificate checking - leave commented otherwise.
# export STREMIO_INSECURE=1

./stremio-mini > stremio.log 2>&1

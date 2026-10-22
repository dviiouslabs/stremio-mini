#!/bin/sh
# Builds Stremio-Mini.zip containing App/Stremio ready to unzip onto the SD card.
# Best-effort: copies SDL_image/SDL_ttf/freetype/png/jpeg/zlib shared libs from
# the toolchain sysroot into lib/ in case the device doesn't have them.
set -e
cd "$(dirname "$0")"
APP=App/Stremio
mkdir -p "$APP/lib" "$APP/cache"
SYSROOT="${SYSROOT:-$(${CROSS_COMPILE}gcc -print-sysroot 2>/dev/null || true)}"
if [ -n "$SYSROOT" ] && [ -d "$SYSROOT" ]; then
  for pat in 'libSDL_image-1.2.so*' 'libSDL_ttf-2.0.so*' 'libfreetype.so*' \
             'libpng1*.so*' 'libjpeg.so*' 'libz.so*'; do
    find "$SYSROOT" -name "$pat" \( -type f -o -type l \) 2>/dev/null | while read -r f; do
      cp -an "$f" "$APP/lib/" 2>/dev/null || true
    done
  done
fi
rm -f Stremio-Mini.zip
zip -qr Stremio-Mini.zip App/Stremio -x 'App/Stremio/stremio.log' 'App/Stremio/last_stream.txt' 'App/Stremio/cache/*'
echo "Wrote Stremio-Mini.zip"

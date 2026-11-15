#!/bin/sh
# Builds gfxtest, 32-bit like the game, into DIR (default build/gfxtest).
# Debian and Ubuntu install only the 64-bit SDL2 headers. They work for a 32-bit build with the 32-bit runtime
# library; the one architecture-specific header is copied next to the build, as the Makefile does.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:-$root/build/gfxtest}
mkdir -p "$out/sdl-include/SDL2"
if [ -d /usr/include/x86_64-linux-gnu/SDL2 ]; then
    cp /usr/include/x86_64-linux-gnu/SDL2/* "$out/sdl-include/SDL2/"
fi
gcc -m32 -msse2 -mfpmath=sse -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -I"$out/sdl-include" -I"$root/src/gfx" \
    "$here/gfxtest.c" "$root"/src/gfx/*.c \
    -o "$out/gfxtest" -L/usr/lib/i386-linux-gnu -l:libSDL2-2.0.so.0 -lm
echo "$out/gfxtest"

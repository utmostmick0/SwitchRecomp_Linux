#!/bin/sh
# SwitchRecomp (the tool) for Linux: needs SDL3, SDL3_ttf and SDL3_image development packages
cd "$(dirname "$0")"
cc -std=gnu17 -O2 -Wall -Wno-missing-braces main.c gfx.c music.c glibc_compat.c -o "${1:-../../SwitchRecomp}" $(pkg-config --cflags --libs sdl3 sdl3-ttf sdl3-image) -lm

#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || ! printf '%s' "$1" | grep -Eq '^[0-9]{3}$'; then
    echo "Usage: $0 NNN (three-digit milestone number)" >&2
    exit 2
fi

root=$(git rev-parse --show-toplevel)
branch=$(git -C "$root" branch --show-current)
if [ "$branch" != beta ]; then
    echo "Run this script on the beta branch; current branch is $branch" >&2
    exit 1
fi

git -C "$root" rev-parse --verify main >/dev/null
git -C "$root" rev-parse --verify beta >/dev/null
mkdir -p "$root/patches"
out="$root/patches/milestone-$1-main-to-beta.patch"
git -C "$root" diff --binary main..beta -- . ':(exclude)patches/*.patch' > "$out"
echo "Wrote $out"

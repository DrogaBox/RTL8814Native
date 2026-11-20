#!/bin/zsh
# Builds llwchan. A UDP flow needs no entitlement, so an ad-hoc signature does.
set -e
dir="${0:A:h}"
cd "$dir"
mkdir -p build
clang -O2 -Wall -Wextra -fblocks -mmacosx-version-min=14.0 -framework Network \
	-o build/llwchan llwchan.c
echo "built: $dir/build/llwchan"

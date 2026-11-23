#!/bin/zsh
# Builds awdlknob. Reading the knobs takes nothing; changing one takes root,
# which the layer checks, not the binary: an ad-hoc signature does.
set -e
dir="${0:A:h}"
cd "$dir"
mkdir -p build
clang -O2 -Wall -Wextra -mmacosx-version-min=14.0 -framework IOKit -framework CoreFoundation \
	-o build/awdlknob awdlknob.c
echo "built: $dir/build/awdlknob"

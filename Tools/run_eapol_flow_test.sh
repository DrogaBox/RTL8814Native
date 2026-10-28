#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p Build
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -ITools -ISource/Driver \
  Tools/eapol_flow_test.c Source/Driver/SAE.c Source/Driver/SecurityIE.c \
  Source/Driver/PMF.c Source/Driver/ReplayCheck.c \
  -o Build/eapol_flow_test
exec ./Build/eapol_flow_test

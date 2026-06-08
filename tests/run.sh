#!/usr/bin/env bash
#
# Build and run the KeyEngine unit tests.
#
# There is no host toolchain, so this compiles inside the org.kde.Sdk (the same SDK the
# flatpak build uses). KeyEngine is dependency-free (no Qt, no X11), so plain g++ is enough.
#
# Usage: tests/run.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/splitkeyboard/src"

flatpak run --devel --filesystem="$REPO" --command=bash org.kde.Sdk//6.10 -c "
  set -e
  g++ -std=c++17 -Wall -Wextra -O1 \
    '$SRC/keyengine.cpp' '$REPO/tests/test_keyengine.cpp' \
    -I'$SRC' -o /tmp/keyengine_test
  /tmp/keyengine_test
"

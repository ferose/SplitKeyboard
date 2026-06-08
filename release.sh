#!/usr/bin/env bash
#
# Build the signed flatpak repo from the current source tree and publish it to the
# gh-pages branch (served by GitHub Pages at https://ferose.github.io/SplitKeyboard/).
# Users install/update from there via splitkeyboard.flatpakref.
#
# This only rebuilds + republishes the *binary* repo. Commit and push your source
# changes to `main` separately first.
#
# Requires: the repo signing key in your GPG keyring (override the id with
# SPLITKB_GPG_KEYID=...), and a clone of the gh-pages branch at $GHPAGES with a
# `github` remote (git clone, git checkout gh-pages).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
KEYID="${SPLITKB_GPG_KEYID:-92C51F7D6B33B3D7}"
MANIFEST="$REPO_ROOT/online.ferose.SplitKeyboard.yml"

# Build outputs must share a filesystem with the .flatpak-builder state dir (the repo),
# so keep them under $HOME rather than /tmp.
OSTREE_REPO="${SPLITKB_OSTREE_REPO:-$HOME/splitkb-pages-repo}"
BUILD_DIR="${SPLITKB_BUILD_DIR:-$HOME/splitkb-pages-build}"
GHPAGES="${SPLITKB_GHPAGES:-$HOME/splitkb-ghpages}"

echo ">> building + signing the repo from $MANIFEST"
cd "$REPO_ROOT"
flatpak run org.flatpak.Builder --force-clean --user --install-deps-from=flathub --ccache \
  --repo="$OSTREE_REPO" --gpg-sign="$KEYID" "$BUILD_DIR" "$MANIFEST"
flatpak build-update-repo --gpg-sign="$KEYID" "$OSTREE_REPO"

echo ">> syncing the repo into the gh-pages working copy ($GHPAGES)"
[ -d "$GHPAGES/.git" ] || { echo "!! $GHPAGES is not a git clone of the gh-pages branch"; exit 1; }
rm -rf "$GHPAGES/repo"
cp -r "$OSTREE_REPO" "$GHPAGES/repo"

echo ">> committing + pushing gh-pages"
cd "$GHPAGES"
git add -A
if git diff --cached --quiet; then
  echo ">> nothing changed; repo already up to date"
  exit 0
fi
git commit -q -m "Update flatpak repo ($(date -u +%Y-%m-%d))"
git push github gh-pages

echo ">> published. Users update with:  flatpak update online.ferose.SplitKeyboard"

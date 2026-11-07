#!/usr/bin/env sh
# Builds and packs LeviMusic.levipack (LeviLaunchroid mod package).
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DIST="$ROOT/dist"
MOD_ID="LeviMusic"
LIB=liblevi_music.so
VERSION="$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$ROOT/manifest.json")"
OUT="${1:-$ROOT/LeviMusic_V$VERSION.levipack}"

"$ROOT/build_arm64.sh"

rm -rf "$DIST"
mkdir -p "$DIST/$MOD_ID/config" "$DIST/$MOD_ID/resources/ui"
cp "$ROOT/build/$LIB" "$DIST/$MOD_ID/$LIB"
cp "$ROOT/config/config.json" "$ROOT/config/config.schema.json" "$DIST/$MOD_ID/config/"
cp "$ROOT/resources/ui/"*.rgba "$ROOT/resources/ui/assets.json" "$DIST/$MOD_ID/resources/ui/"
# Manifest last so LeviLaunchroid's directory observer never sees a manifest
# whose entry library is still being installed.
cp "$ROOT/manifest.json" "$DIST/$MOD_ID/manifest.json"

rm -f "$OUT"
(
  cd "$DIST"
  # Entry library first, manifest last in the ZIP stream.
  zip -q "$OUT" "$MOD_ID/$LIB"
  zip -q "$OUT" "$MOD_ID/config/config.json" "$MOD_ID/config/config.schema.json"
  for f in "$MOD_ID"/resources/ui/*.rgba; do zip -q "$OUT" "$f"; done
  zip -q "$OUT" "$MOD_ID/resources/ui/assets.json"
  zip -q "$OUT" "$MOD_ID/manifest.json"
)
echo "$OUT"

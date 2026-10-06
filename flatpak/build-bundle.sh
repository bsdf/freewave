#!/usr/bin/env bash
# Build a single-file Flatpak bundle of Freewave to hand off to someone.
#
#   flatpak/build-bundle.sh <build-dir> [output.flatpak]
#
# The build directory is required. The output defaults to ./freewave.flatpak
# next to the freewave source; override it with the second argument.
# Recipient installs with:  flatpak install --user ./freewave.flatpak
set -euo pipefail

APP_ID="org.xeyes.Freewave"
FLATHUB_REPO="https://flathub.org/repo/flathub.flatpakrepo"

# Resolve paths relative to this script so it runs from anywhere, independent
# of how the repo is checked out (standalone clone or workspace submodule).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # freewave/flatpak
SOURCE_DIR="$(dirname "$SCRIPT_DIR")"                          # freewave/

BUILD_DIR="${1:-}"
[ -n "$BUILD_DIR" ] || { echo "usage: $0 <build-dir> [output.flatpak]" >&2; exit 1; }
OUTPUT="${2:-$SOURCE_DIR/freewave.flatpak}"

command -v flatpak-builder >/dev/null || { echo "error: flatpak-builder not installed" >&2; exit 1; }

# 1. Generate the manifest from the template (configure_file substitution).
cmake -B "$BUILD_DIR" "$SOURCE_DIR" >/dev/null
MANIFEST="$BUILD_DIR/flatpak/$APP_ID.yml"

# 2. Build into a local OSTree repo.
flatpak-builder --force-clean --repo="$BUILD_DIR/flatpak/repo" \
    "$BUILD_DIR/flatpak/flatpak-build" "$MANIFEST"

# 3. Bundle the repo into one portable file (pulls runtime from Flathub on install).
flatpak build-bundle "$BUILD_DIR/flatpak/repo" "$OUTPUT" "$APP_ID" \
    --runtime-repo="$FLATHUB_REPO"

echo "Created $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
echo "Recipient installs with: flatpak install --user $OUTPUT"

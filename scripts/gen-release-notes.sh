#!/usr/bin/env bash
#
# Regenerates the <releases> block in the AppStream metainfo template from
# CHANGELOG, so the public changelog is the single source of truth for
# release version/date/notes (they can no longer drift from a hand-edited
# metainfo entry).
#
# CHANGELOG is plain-text GNOME NEWS format:
#
#   Version X.Y.Z
#   ~~~~~~~~~~~~~
#   Released: YYYY-MM-DD
#
#   <intro paragraph>
#
#   Features:
#    - item
#
#   Bugfixes:
#    - item
#
# appstreamcli's news-to-metainfo rewrites and reformats an entire metainfo
# file when given one directly (dropping comments, reordering elements), so
# this script instead asks it for the <releases> fragment alone (by pointing
# it at a metainfo path that doesn't exist) and splices that fragment into
# the real file between the existing <releases>/</releases> markers, leaving
# the rest of the file, comments included, untouched.
#
# Usage:  scripts/gen-release-notes.sh
#
# Run at release time and commit the regenerated metainfo. Not part of the
# CMake build — no appstreamcli build-time dependency.

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)

changelog="$repo_root/CHANGELOG"
metainfo="$repo_root/data/org.xeyes.Freewave.metainfo.xml.in"

if ! command -v appstreamcli >/dev/null 2>&1; then
  echo "error: appstreamcli not found (package: appstream)" >&2
  exit 2
fi
if [[ ! -f "$changelog" ]]; then
  echo "error: changelog not found: $changelog" >&2
  exit 2
fi
if [[ ! -f "$metainfo" ]]; then
  echo "error: metainfo not found: $metainfo" >&2
  exit 2
fi

fragment=$(appstreamcli news-to-metainfo --format text "$changelog" -)

awk -v frag="$fragment" '
  /<releases>/  { print frag; skip = 1; next }
  /<\/releases>/{ skip = 0; next }
  skip          { next }
  { print }
' "$metainfo" > "$metainfo.tmp"

mv "$metainfo.tmp" "$metainfo"

echo "Regenerated <releases> in $metainfo from $changelog."

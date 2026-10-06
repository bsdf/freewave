#!/usr/bin/env bash
#
# Removes Navidrome's default "no cover" placeholder from freewave's on-disk
# album art cache.
#
# Navidrome has no "artwork missing" response: every album gets a coverArt
# id, and one with no real art silently serves a bundled placeholder image
# instead of 404ing (see subsonicbackend.cc for the fetch-time fix — an
# exact SHA-256 match — that now stops new placeholders from ever being
# cached). This script cleans up placeholders that were cached *before* that
# fix landed, so those albums fall back to freewave's own no-cover text
# jacket instead of showing Navidrome's generic cover.
#
# Why pixel sampling instead of a hash: by the time an image is on disk it
# has already been scaled to <=600px and re-encoded as JPEG-80 by
# AlbumArtManager, so it no longer matches the placeholder's original bytes.
# Re-encoding also drops the alpha channel — Qt flattens the placeholder's
# transparent background to black — which happens to make the cached result
# an extremely distinctive, resolution-independent fingerprint: a black
# corner (was transparent), a dark neutral-gray center (the "navidrome"
# label hub), and saturated blue at top-center/left-center (the vinyl
# record). No real album cover plausibly matches all four at once, so this
# has no realistic false-positive path onto real art.
#
# Usage:
#   scripts/clean-navidrome-placeholder-cache.sh              # dry run, lists matches
#   scripts/clean-navidrome-placeholder-cache.sh --apply       # actually deletes them
#   scripts/clean-navidrome-placeholder-cache.sh --cache-dir DIR [--apply]
#   scripts/clean-navidrome-placeholder-cache.sh -j N [--apply] # parallel jobs (default 4)
#
# Deleting a match is safe and reversible in practice: freewave just
# refetches art for that album next time it's shown, and after the
# subsonicbackend.cc fix it will correctly skip re-caching the placeholder.
#
# Requires: ImageMagick (identify).

set -euo pipefail

apply=0
jobs=4
cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/xeyes.org/freewave"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) apply=1; shift ;;
    --cache-dir) cache_dir=$2; shift 2 ;;
    -j) jobs=$2; shift 2 ;;
    -h|--help)
      sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if ! command -v identify >/dev/null; then
  echo "error: ImageMagick's 'identify' is required but not on PATH." >&2
  exit 2
fi

if [[ ! -d "$cache_dir" ]]; then
  echo "error: cache dir not found: $cache_dir" >&2
  exit 2
fi

# One `identify -format` call per file samples all four fingerprint points at
# once (percentage-of-size coordinates via fx: — identify's own p{x%,y%}
# syntax doesn't accept percentages). Output: "w h r,g,b r,g,b r,g,b r,g,b"
# for corner, center, top, left.
fmt='%w %h'
for pt in "0.01 0.01" "0.5 0.5" "0.5 0.08" "0.08 0.5"; do
  read -r px py <<<"$pt"
  fmt+=" %[fx:int(255*p{int(w*$px),int(h*$py)}.r)],%[fx:int(255*p{int(w*$px),int(h*$py)}.g)],%[fx:int(255*p{int(w*$px),int(h*$py)}.b)]"
done
fmt+='\n'

is_neutral_dark() { # r g b -> dark and roughly colorless (the label hub)
  local r=$1 g=$2 b=$3
  (( r < 70 && g < 70 && b < 70 )) || return 1
  local dr=$((r - g)); dr=${dr#-}
  local dg=$((g - b)); dg=${dg#-}
  (( dr < 25 && dg < 25 ))
}

is_blue() { # r g b -> saturated blue (the vinyl record)
  local r=$1 g=$2 b=$3
  (( r < 40 && g >= 40 && g <= 150 && b > r + 120 ))
}

check_one() {
  local f=$1
  local line
  line=$(identify -format "$fmt" "$f" 2>/dev/null) || return 1
  read -r _w _h corner center top left <<<"$line"
  IFS=, read -r cr cg cb <<<"$corner"
  IFS=, read -r mr mg mb <<<"$center"
  IFS=, read -r tr tg tb <<<"$top"
  IFS=, read -r lr lg lb <<<"$left"

  (( cr < 20 && cg < 20 && cb < 20 )) || return 1
  is_neutral_dark "$mr" "$mg" "$mb" || return 1
  is_blue "$tr" "$tg" "$tb" || return 1
  is_blue "$lr" "$lg" "$lb" || return 1

  echo "$f"
}
export -f check_one is_neutral_dark is_blue
export fmt

mapfile -t matches < <(
  find "$cache_dir" -maxdepth 1 -name '*.jpg' -print0 \
    | xargs -0 -P "$jobs" -I{} bash -c 'check_one "$1"' _ {}
)

if [[ ${#matches[@]} -eq 0 ]]; then
  echo "No cached Navidrome placeholders found in $cache_dir."
  exit 0
fi

total_bytes=0
for f in "${matches[@]}"; do
  total_bytes=$((total_bytes + $(stat -c%s "$f")))
done

echo "${#matches[@]} cached placeholder(s) found ($((total_bytes / 1024)) KiB):"
printf '  %s\n' "${matches[@]}"

if [[ $apply -eq 1 ]]; then
  rm -f -- "${matches[@]}"
  echo "Deleted ${#matches[@]} file(s). freewave will refetch real art for these albums."
else
  echo
  echo "Dry run — nothing deleted. Re-run with --apply to remove these files."
fi

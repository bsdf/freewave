#!/usr/bin/env bash
#
# OpenSubsonic conformance drift guard.
#
# Keeps docs/opensubsonic-conformance.md honest against the code: the set of
# endpoints marked **Used** in the matrix must equal the set of endpoints the
# code actually calls via SubsonicBackend::build_url("…"). If they drift
# (a new endpoint wired up but not documented, or a documented-Used endpoint
# removed from the code), this fails with the diff.
#
# It is a text check by design — endpoint names are string literals on a
# single call line, and the doc's status column is a plain table cell, so a
# grep over both is exact and needs no build. star/unstar share one ternary
# build_url() call; both string literals sit on that line, so both are caught.
# The starred endpoints reach build_url() through fetch_starred_endpoint()'s
# `endpoint` parameter, so those call lines are grepped as well.
#
# Usage:  scripts/check-subsonic-endpoints.sh [SOURCE_DIR]
#   SOURCE_DIR  defaults to the repo root (parent of this script's dir).
#
# The conformance doc is part of the maintainers' development-notes tree and
# is not shipped with this repo; when it isn't found the check is skipped
# rather than failed, so the script is safe to run from any checkout.
#
# Exit: 0 = in sync (or doc absent, skipped), 1 = drift, 2 = setup error.

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
src_dir=${1:-$(cd "$script_dir/.." && pwd)}

code="$src_dir/controller/subsonicbackend.cc"

# Look for the conformance doc in a parent development tree or in-repo.
doc=""
for cand in "$src_dir/../docs/opensubsonic-conformance.md" \
            "$src_dir/docs/opensubsonic-conformance.md"; do
  if [[ -f "$cand" ]]; then doc="$cand"; break; fi
done

if [[ ! -f "$code" ]]; then
  echo "error: source not found: $code" >&2
  exit 2
fi
if [[ -z "$doc" ]]; then
  echo "skip: conformance doc not present in this checkout; nothing to verify."
  exit 0
fi

# Every endpoint the doc knows about: the first backtick-quoted token on each
# table row. Used to filter code string literals down to real endpoint names
# (so build_url params like "id"/"format" don't leak in as false endpoints).
mapfile -t universe < <(grep -oE '^\| `[a-zA-Z0-9]+`' "$doc" \
  | grep -oE '[a-zA-Z0-9]+' | sort -u)

# Endpoints the doc marks Used: rows whose status cell is exactly "Used".
# Anchored to "| Used |" so the extensions matrix's "Used (unnegotiated)" row
# (apiKeyAuthentication, not an endpoint) is not counted.
doc_used=$(grep -oE '^\| `[a-zA-Z0-9]+` \| Used \|' "$doc" \
  | grep -oE '`[a-zA-Z0-9]+`' | tr -d '`' | sort -u)

# Endpoints the code calls: quoted tokens on any build_url(...) or
# fetch_starred_endpoint(...) line, intersected with the doc's universe.
code_tokens=$(grep -E 'build_url\(|fetch_starred_endpoint\(' "$code" \
  | grep -oE '"[a-zA-Z0-9]+"' | tr -d '"' | sort -u)
code_used=$(comm -12 \
  <(printf '%s\n' "$code_tokens") \
  <(printf '%s\n' "${universe[@]}"))

if [[ "$doc_used" == "$code_used" ]]; then
  n=$(printf '%s\n' "$code_used" | grep -c . || true)
  echo "OK: $n endpoints marked Used match build_url() calls in the code."
  exit 0
fi

echo "DRIFT: docs/opensubsonic-conformance.md and subsonicbackend.cc disagree." >&2
echo >&2
missing_doc=$(comm -23 <(printf '%s\n' "$code_used") <(printf '%s\n' "$doc_used"))
missing_code=$(comm -13 <(printf '%s\n' "$code_used") <(printf '%s\n' "$doc_used"))
if [[ -n "$missing_doc" ]]; then
  echo "Called in code but not marked Used in the doc:" >&2
  printf '  %s\n' $missing_doc >&2
fi
if [[ -n "$missing_code" ]]; then
  echo "Marked Used in the doc but not called in the code:" >&2
  printf '  %s\n' $missing_code >&2
fi
exit 1

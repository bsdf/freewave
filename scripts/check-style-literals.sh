#!/usr/bin/env bash
#
# Styling guardrail.
#
# Fails if any setStyleSheet(...) call in ui/ (outside ui/theme/) embeds a
# hardcoded style literal that is supposed to come from the theme layer:
#   - a hex color   (#rgb / #rrggbb)
#   - a font-family declaration
#   - a font-size declaration
#
# Colors/fonts must flow through QPalette, theme::tok::, the theme::type::
# catalog, or the theme::ui:: component factories. theme/ is the allow-list —
# it is the one place these literals legitimately live.
#
# Why an AST check and not grep: setStyleSheet calls span multiple lines, are
# wrapped in QString(...), and some are authored in .ui files (becoming string
# literals only after uic generates ui_*.h). A clang AST matcher sees the real
# call in the translation unit; grep over .cc files misses all three cases.
#
# Usage:  scripts/check-style-literals.sh [SOURCE_DIR] [BUILD_DIR]
#   SOURCE_DIR  defaults to the repo root (parent of this script's dir)
#   BUILD_DIR   defaults to $SOURCE_DIR/../build, else $SOURCE_DIR/build
#               (must contain compile_commands.json)
# Env: CLANG_QUERY overrides the clang-query binary.
#
# Exit: 0 = clean, 1 = violations found, 2 = setup error (no tool / no DB).

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
src_dir=${1:-$(cd "$script_dir/.." && pwd)}

# Locate the build dir holding compile_commands.json.
build_dir=${2:-}
if [[ -z "$build_dir" ]]; then
  for cand in "$src_dir/../build" "$src_dir/build" "$src_dir/.."; do
    if [[ -f "$cand/compile_commands.json" ]]; then
      build_dir=$(cd "$cand" && pwd)
      break
    fi
  done
fi
if [[ -z "$build_dir" || ! -f "$build_dir/compile_commands.json" ]]; then
  echo "error: no compile_commands.json found (looked near $src_dir)." >&2
  echo "       configure first: cmake -B build freewave" >&2
  exit 2
fi

# Locate a clang-query binary (versioned names included).
clang_query=${CLANG_QUERY:-}
if [[ -z "$clang_query" ]]; then
  for c in clang-query clang-query-21 clang-query-20 clang-query-19 clang-query-18; do
    if command -v "$c" >/dev/null 2>&1; then clang_query=$c; break; fi
  done
fi
if [[ -z "$clang_query" ]]; then
  echo "error: clang-query not found (set CLANG_QUERY or install clang-tools)." >&2
  exit 2
fi

matcher=$(mktemp)
work_dir=$(mktemp -d)
raw=$(mktemp)
trap 'rm -rf "$matcher" "$work_dir" "$raw"' EXIT
cat > "$matcher" <<'EOF'
set output dump
match cxxMemberCallExpr(callee(cxxMethodDecl(hasName("setStyleSheet"))), hasDescendant(stringLiteral().bind("lit")))
EOF

# Scan every ui/ translation unit except the theme allow-list. Each .cc pulls in
# its generated ui_*.h, so .ui-authored styles are covered transitively.
#
# clang-query parses each TU in full (≈4 s with all the Qt headers), and the
# files are independent, so we fan them across cores rather than scanning one at
# a time — roughly an Nx speedup. Passing every file to a single clang-query
# invocation would NOT help: it still parses them sequentially in one process.
# Each job writes to its own file under $work_dir so parallel output can never
# interleave; we concatenate afterwards.
mapfile -d '' -t ui_files < <(find "$src_dir/ui" -name '*.cc' -not -path '*/theme/*' -print0 | sort -z)
if [[ ${#ui_files[@]} -eq 0 ]]; then
  echo "error: no ui/*.cc files found under $src_dir/ui" >&2
  exit 2
fi

jobs=$(nproc 2>/dev/null || echo 4)
export CQ="$clang_query" BUILD_DIR="$build_dir" MATCHER="$matcher" WORK="$work_dir"
printf '%s\0' "${ui_files[@]}" \
  | xargs -0 -P"$jobs" -I{} sh -c '
      f="$1"
      out="$WORK/$(printf "%s" "$f" | tr "/" "_").out"
      "$CQ" -p "$BUILD_DIR" -f "$MATCHER" "$f" 2>/dev/null \
        | grep -E "^StringLiteral " > "$out" || true
    ' sh {}

cat "$work_dir"/*.out > "$raw" 2>/dev/null || true

# A StringLiteral dump line is:  StringLiteral 0x.. <loc> 'type' lvalue "content"
# Strip the address so identical nodes from re-parses dedup; keep <loc> + content.
# Then flag only nodes whose content carries a forbidden literal.
violations=$(
  sed -E 's/^StringLiteral 0x[0-9a-f]+ //' "$raw" \
    | sort -u \
    | grep -E 'font-family|font-size|#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?' || true
)

if [[ -z "$violations" ]]; then
  echo "style-literal check: clean — no hardcoded hex/font-family/font-size in ui/ setStyleSheet (outside theme/)."
  exit 0
fi

echo "style-literal check: VIOLATIONS found." >&2
echo "Hardcoded style literals belong in the theme layer (palette / theme::tok / theme::type / theme::ui)." >&2
echo >&2

count=0
while IFS= read -r line; do
  [[ -z "$line" ]] && continue
  count=$((count + 1))
  # Extract "<path:line...>" and the quoted content.
  loc=$(printf '%s' "$line" | grep -oE '<[^>]+>' | head -1 | tr -d '<>')
  path=${loc%%:*}
  rest=${loc#*:}
  lineno=${rest%%:*}
  content=$(printf '%s' "$line" | grep -oE '"[^"]*"$' || true)

  # Map generated uic headers back to their .ui authoring site.
  hint=""
  if [[ "$path" == *_autogen* || "$path" == *"ui_"*.h ]]; then
    base=$(basename "$path")          # ui_mainwindow.h
    stem=${base#ui_}; stem=${stem%.h} # mainwindow
    if [[ -f "$src_dir/ui/$stem.ui" ]]; then
      hint=" (generated — fix in ui/$stem.ui)"
    else
      hint=" (generated header — fix the authoring .ui)"
    fi
  else
    # Make in-tree paths repo-relative for readable output.
    path=${path#"$src_dir"/}
  fi

  # Which patterns tripped, for a quick diagnosis.
  why=""
  printf '%s' "$content" | grep -q 'font-family' && why+="font-family "
  printf '%s' "$content" | grep -q 'font-size' && why+="font-size "
  printf '%s' "$content" | grep -qE '#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?' && why+="hex "

  echo "  $path:$lineno$hint" >&2
  echo "      forbidden: ${why% }" >&2
done <<< "$violations"

echo >&2
echo "$count violation(s)." >&2
exit 1

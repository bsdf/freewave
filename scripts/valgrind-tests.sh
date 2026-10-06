#!/bin/sh
# Valgrind fallback for when a sanitizer rebuild isn't wanted: runs a test
# binary (or any binary) from the regular build tree under memcheck.
#
#   scripts/valgrind-tests.sh <binary> [args...]
#
# e.g. scripts/valgrind-tests.sh ../build/freewave_qt_tests --gtest_filter='ClockMan*'
#
# Roughly 20-50x slower than native, so prefer scripts/asan-tests.sh for full
# runs and reserve this for targeted investigation. Note: do NOT point it at an
# ASan-instrumented binary — the two tools conflict. Timing-sensitive suites
# (GstIntegrationTest, SubsonicBackendNetTest) can fail spuriously under the
# slowdown — a gtest FAILED line without a memcheck report is a flake, not a
# memory bug; rerun the test in isolation before chasing it.
set -eu

if [ $# -lt 1 ]; then
  echo "usage: $0 <binary> [args...]" >&2
  exit 2
fi

src_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# --errors-for-leak-kinds matches --show-leak-kinds: possibly-lost records
# (glib thread stacks) neither show nor fail the run, only definite/indirect.
exec valgrind \
  --leak-check=full \
  --show-leak-kinds=definite,indirect \
  --errors-for-leak-kinds=definite,indirect \
  --track-origins=yes \
  --num-callers=25 \
  --error-exitcode=1 \
  --suppressions="$src_dir/scripts/valgrind-qt.supp" \
  "$@"

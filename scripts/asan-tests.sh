#!/bin/sh
# Memory-safety harness: build + run the test suite under ASan/UBSan/LSan.
#
#   scripts/asan-tests.sh [ctest args...]
#
# Uses a dedicated build tree (build-asan, sibling of the source checkout's
# parent workspace build dir) so the regular build stays fast. Extra args are
# passed to ctest, e.g.:
#
#   scripts/asan-tests.sh -R ClockMan          # one suite
#   scripts/asan-tests.sh -E GstIntegration    # skip the network tests
set -eu

src_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${ASAN_BUILD_DIR:-"$src_dir/../build-asan"}

cmake -B "$build_dir" "$src_dir" -DBUILD_TESTS=ON -DENABLE_ASAN=ON
cmake --build "$build_dir"

# halt_on_error=0 so one report doesn't mask later ones in the same test binary;
# detect_leaks stays on (LSan) with known Qt/platform noise suppressed.
ASAN_OPTIONS="halt_on_error=0:detect_stack_use_after_return=1"
LSAN_OPTIONS="suppressions=$src_dir/scripts/lsan.supp:print_suppressions=0"
UBSAN_OPTIONS="print_stacktrace=1"
export ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS

exec ctest --test-dir "$build_dir" --output-on-failure "$@"

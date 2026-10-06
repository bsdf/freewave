# HACKING

notes for working on freewave itself. building is covered in
[INSTALL.md](INSTALL.md).

## TESTS

two Google Test targets: `freewave_tests` (pure C++, no Qt) and
`freewave_qt_tests` (Qt-aware).

```
cmake -B build . -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

the Subsonic playback tests need the GStreamer runtime plugins, not just the
`-dev` packages, and an audio sink with a real clock. headless, use a PulseAudio
null sink; an ALSA `null` device never paces playback and the seek tests
overshoot.

## SANITIZERS

`-DENABLE_ASAN=ON` builds the app, the in-tree libraries and the tests with
ASan+UBSan, in a separate `build-asan` tree:

```
scripts/asan-tests.sh                  # configure, build, run everything
scripts/asan-tests.sh -R ClockMan      # extra args go to ctest
scripts/valgrind-tests.sh build/freewave_qt_tests   # memcheck, no rebuild
```

run it before changes that touch ownership, threading or raw buffers. known
platform-library noise is suppressed in `scripts/lsan.supp` and
`scripts/valgrind-qt.supp`; freewave's own frames never go there.

## STYLE GUARDRAIL

all colors, fonts and font sizes flow through the theme layer (`QPalette`,
`ui/theme/tokens.hh`, the `theme::type::` catalog and the `theme::ui::`
factories). `ui/theme/` is the only place raw style literals live.
`scripts/check-style-literals.sh` enforces this: it fails if any
`setStyleSheet(...)` call in `ui/` outside `ui/theme/` embeds a hardcoded hex
color, `font-family` or `font-size`.

it is an AST check (clang-query) rather than grep, because `setStyleSheet` calls
span multiple lines, are wrapped in `QString(...)`, and some styles are authored
in `.ui` files, becoming string literals only in the generated `ui_*.h`. it is
not wired into the build; run it by hand after touching `ui/` styling:

```
cmake -B build .                       # needs build/compile_commands.json
scripts/check-style-literals.sh        # exit 0 clean, 1 violations, 2 setup error
```

requires `clang-query` (set `CLANG_QUERY` to pick a versioned binary, e.g.
`CLANG_QUERY=clang-query-19`). every `ui/*.cc` translation unit is parsed, about
4s each, in parallel across cores, so a full scan takes 10-15s.

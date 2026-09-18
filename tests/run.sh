#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$repo_root/src/dsp" \
    "$repo_root/tests/test_scratch_engine.c" \
    "$repo_root/src/dsp/scratch_engine.c" \
    -lm -o "$repo_root/tests/test_scratch_engine"
"$repo_root/tests/test_scratch_engine"

cc -std=gnu11 -Wall -Wextra -Werror \
    -I"$repo_root/src/dsp/vendor/xwax" \
    "$repo_root/tests/test_timecode.c" \
    "$repo_root/src/dsp/vendor/xwax/timecoder.c" \
    "$repo_root/src/dsp/vendor/xwax/lut.c" \
    -lm -o "$repo_root/tests/test_timecode"
"$repo_root/tests/test_timecode"

node "$repo_root/tests/test_monitor.mjs"
node "$repo_root/tests/test_scratch_view.mjs"
node "$repo_root/tests/test_jog_scratch.mjs"

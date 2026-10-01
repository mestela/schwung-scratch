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

cc -std=gnu11 -Wall -Wextra -Werror \
    -I"$repo_root/src/dsp" -I"$repo_root/src/dsp/vendor/xwax" \
    "$repo_root/tests/test_default_sample.c" \
    "$repo_root/src/dsp/scratch.c" \
    "$repo_root/src/dsp/scratch_engine.c" \
    "$repo_root/src/dsp/vendor/xwax/timecoder.c" \
    "$repo_root/src/dsp/vendor/xwax/lut.c" \
    -lm -lpthread -o "$repo_root/tests/test_default_sample"
"$repo_root/tests/test_default_sample" "$repo_root"

node "$repo_root/tests/test_monitor.mjs"
node "$repo_root/tests/test_scratch_view.mjs"
node "$repo_root/tests/test_jog_scratch.mjs"

multi_test=$(mktemp "${TMPDIR:-/tmp}/scratch-multi.XXXXXX")
trap 'rm -f "$multi_test"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror \
    -I"$repo_root/src/dsp" -I"$repo_root/src/dsp/vendor/xwax" \
    "$repo_root/tests/test_multi_instance.c" \
    "$repo_root/src/dsp/scratch.c" \
    "$repo_root/src/dsp/scratch_engine.c" \
    "$repo_root/src/dsp/vendor/xwax/timecoder.c" \
    "$repo_root/src/dsp/vendor/xwax/lut.c" \
    -lm -lpthread -o "$multi_test"
"$multi_test" "$repo_root"

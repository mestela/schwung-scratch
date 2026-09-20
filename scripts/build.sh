#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(dirname "$script_dir")"
cd "$repo_root"

if [ ! -f /.dockerenv ]; then
    docker build -t schwung-scratch-builder -f scripts/Dockerfile .
    docker run --rm -v "$repo_root:/build" -u "$(id -u):$(id -g)" \
        -w /build schwung-scratch-builder ./scripts/build.sh
    exit 0
fi

cc="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"
mkdir -p build
rm -rf dist/scratch
mkdir -p dist/scratch/samples
"$cc" -std=gnu11 -O3 -shared -fPIC -Wall -Wextra \
    -Isrc/dsp -Isrc/dsp/vendor/xwax \
    src/dsp/scratch.c src/dsp/scratch_engine.c \
    src/dsp/vendor/xwax/timecoder.c src/dsp/vendor/xwax/lut.c \
    -o build/dsp.so -lm -lpthread
cp build/dsp.so src/module.json src/monitor.js src/scratch_view.js src/help.json dist/scratch/
cp samples/ahh-fresh.wav dist/scratch/samples/
cp COPYING THIRD_PARTY_LICENSES.md SAMPLE_LICENSE.md dist/scratch/
tar -C dist -czf dist/scratch-module.tar.gz scratch
echo "Built dist/scratch-module.tar.gz"

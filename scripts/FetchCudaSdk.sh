#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${DEST:-$SCRIPT_DIR/../.deps/cuda}"
CUDA_MAJOR="${CUDA_MAJOR:-12}"

WHEEL_DIR="$DEST/wheels"
mkdir -p "$WHEEL_DIR"

python3 -m pip download --no-deps --only-binary=:all: \
    "nvidia-cuda-nvrtc-cu$CUDA_MAJOR" "nvidia-cuda-runtime-cu$CUDA_MAJOR" \
    -d "$WHEEL_DIR"

UNPACKED="$DEST/unpacked"
mkdir -p "$UNPACKED"
for whl in "$WHEEL_DIR"/*.whl; do
    [ -e "$whl" ] || continue
    base="$(basename "$whl" .whl)"
    out="$UNPACKED/$base"
    if [ ! -d "$out" ]; then
        python3 - "$whl" "$out" <<'PY'
import os, sys, zipfile
whl, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
with zipfile.ZipFile(whl) as z:
    z.extractall(out)
PY
    fi
done

SDK="$DEST/sdk"
mkdir -p "$SDK/include" "$SDK/lib"

for d in "$UNPACKED"/*/; do
    [ -d "$d" ] || continue
    nvidia_root="$d/nvidia"
    [ -d "$nvidia_root" ] || continue
    for pkgdir in "$nvidia_root"/*/; do
        [ -d "$pkgdir" ] || continue
        if [ -d "$pkgdir/include" ]; then
            cp -r "$pkgdir/include/." "$SDK/include/"
        fi
        if [ -d "$pkgdir/lib" ]; then
            find "$pkgdir/lib" -type f -name '*.so*' -exec cp -a {} "$SDK/lib/" \;
        fi
    done
done

echo "CUDA SDK subset ready at $SDK"

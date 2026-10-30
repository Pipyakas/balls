#!/usr/bin/env bash
# Fetches the ROCR-Runtime (HSA) headers matching ROCm 7.1.1 from GitHub.
# Mirrors scripts/FetchCudaSdk.sh. Downloads into .deps/rocm/include.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${DEST:-$SCRIPT_DIR/../.deps/rocm}"
ROCm_VERSION="${ROCM_VERSION:-7.1.1}"
# ROCR-Runtime tag for ROCm 7.1.1
TAG="${ROCM_TAG:-rocm-7.1.1}"

INCLUDE="$DEST/include"
mkdir -p "$INCLUDE"

if [ -f "$INCLUDE/hsa/hsa.h" ]; then
    echo "ROCm headers already present at $INCLUDE"
    exit 0
fi

WORK="$DEST/_fetch"
rm -rf "$WORK"
mkdir -p "$WORK"

# Sparse checkout of the runtime headers only.
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/ROCm/ROCR-Runtime.git "$WORK/ROCR-Runtime" \
    --branch "$TAG" 2>&1 | tail -5
cd "$WORK/ROCR-Runtime"
git sparse-checkout set runtime/hsa-runtime/inc 2>&1 | tail -3

SRC="$WORK/ROCR-Runtime/runtime/hsa-runtime/inc"
if [ ! -d "$SRC" ]; then
    echo "headers not found at $SRC" >&2
    exit 1
fi
cp -r "$SRC/." "$INCLUDE/"
echo "ROCm headers ready at $INCLUDE"
ls "$INCLUDE" | head
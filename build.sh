#!/usr/bin/env sh
# Configure and build Balls on Linux or macOS. Windows uses build.bat.
#
#   --debug            build with symbols instead of Release
#   --renderer NAME    nvrhi (default) or sdlgpu
#   --cuda             enable the CUDA physics backend (needs .deps/cuda/sdk)
#   --dir PATH         build directory (default: build)
#   --clean            delete the build directory first
#   -j N               parallel jobs (default: all CPUs)
#   -h, --help         show this help
#
# Anything after -- goes to CMake unchanged.

set -eu

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$root/build"
config=Release
renderer=nvrhi
cuda=OFF
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

while [ $# -gt 0 ]; do
    case "$1" in
        --debug) config=Debug ;;
        --renderer) shift; renderer=${1:?--renderer needs a value} ;;
        --cuda) cuda=ON ;;
        --dir) shift; build_dir=${1:?--dir needs a value} ;;
        --clean) rm -rf "$build_dir" ;;
        -j) shift; jobs=${1:?-j needs a value} ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found on PATH" >&2
    exit 1
fi

generator=""
if command -v ninja >/dev/null 2>&1; then
    generator="-G Ninja"
fi

# shellcheck disable=SC2086
cmake -S "$root" -B "$build_dir" $generator \
    -DCMAKE_BUILD_TYPE="$config" \
    -DBALLS_RENDERER="$renderer" \
    -DBALLS_ENABLE_CUDA="$cuda" "$@"

cmake --build "$build_dir" --parallel "$jobs"

echo "Built: $build_dir/bin/ColorBlastBalls"

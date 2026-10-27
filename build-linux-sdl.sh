#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

build_dir=build/linux
dist_dir=dist/linux-sdl

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi

if [ ! -f "$build_dir/CMakeCache.txt" ]; then
    cmake -S . -B "$build_dir" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DBALLS_FRONTEND=sdl
fi
cmake --build "$build_dir" --parallel --target ColorBlastBalls

pkill -f "$(pwd)/$dist_dir/ColorBlastBalls" 2>/dev/null || true
mkdir -p "$dist_dir"
cp "$build_dir/bin/ColorBlastBalls" "$dist_dir/"
cp "$build_dir"/bin/*.so "$dist_dir/" 2>/dev/null || true
rm -rf "$dist_dir/assets"
cp -r "$build_dir/bin/assets" "$dist_dir/assets"

echo "Built $dist_dir/ColorBlastBalls"

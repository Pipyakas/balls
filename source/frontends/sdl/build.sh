#!/usr/bin/env bash
# Builds one variant of the SDL front end on Linux and stages a runnable copy under dist/.
#   build.sh               the default build (linux-nvrhi)
#   build.sh <variant>     one of the variants below
#   build.sh --variants    lists them as id;description, which the root build.sh menu reads
#   build.sh --setup [variant]   only checks this machine can build it, offering to install what is missing
# Every build runs that setup check first, so a missing tool is named before CMake or Gradle starts.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"

if [ "${1:-}" = "--variants" ]; then
    echo "linux-nvrhi;Linux - SDL + NVRHI (Vulkan)"
    echo "linux-webgpu;Linux - SDL + WebGPU (wgpu-native)"
    echo "linux-sdlgpu;Linux - SDL + SDL_GPU"
    echo "android;Android APK - SDL + WebGPU (needs JDK 17 and the Android SDK with NDK 27.2)"
    exit 0
fi

setup_only=""
if [ "${1:-}" = "--setup" ]; then
    setup_only=1
    shift
fi
variant=${1:-linux-nvrhi}
case "$variant" in
    linux-nvrhi | linux-webgpu | linux-sdlgpu | android) ;;
    *)
        echo "Unknown SDL variant \"$variant\". Known variants:" >&2
        "$0" --variants >&2
        exit 1
        ;;
esac
"$root/source/frontends/sdl/setup.sh" "$variant"
[ -n "$setup_only" ] && exit 0

case "$variant" in
    linux-nvrhi) renderer=nvrhi; build_dir=build/linux; dist_dir=dist/linux-sdl ;;
    linux-webgpu) renderer=webgpu; build_dir=build/linux-webgpu; dist_dir=dist/linux-sdl-webgpu ;;
    linux-sdlgpu) renderer=sdlgpu; build_dir=build/linux-sdlgpu; dist_dir=dist/linux-sdl-sdlgpu ;;
    android)
        android_dir="$root/source/frontends/sdl/android"
        (cd "$android_dir" && sh ./gradlew --no-daemon assembleDebug)
        mkdir -p dist/android-sdl
        cp "$android_dir/app/build/outputs/apk/debug/app-debug.apk" dist/android-sdl/balls.apk
        echo
        echo "Built: $root/dist/android-sdl/balls.apk"
        exit 0
        ;;
esac

generator=()
if [ ! -f "$build_dir/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi
cmake -S . -B "$build_dir" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DBALLS_FRONTEND=sdl -DBALLS_RENDERER="$renderer" -DBALLS_PROFILE=extended
cmake --build "$build_dir" --parallel --target balls

pkill -f "$root/$dist_dir/balls" 2>/dev/null || true
mkdir -p "$dist_dir"
cp "$build_dir/bin/balls" "$dist_dir/"
cp "$build_dir"/bin/*.so "$dist_dir/" 2>/dev/null || true
rm -rf "$dist_dir/assets"
cp -r "$build_dir/bin/assets" "$dist_dir/assets"

echo
echo "Built: $root/$dist_dir/balls"

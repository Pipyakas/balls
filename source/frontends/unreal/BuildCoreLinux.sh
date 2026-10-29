# Builds the engine-agnostic core for a Linux host, with the compiler and the standard library Unreal itself uses
# (the engine's in-tree clang and bundled libc++), into build/linux-ue. The game module links that archive and
# dlopens the physics plugins from build/linux-ue/bin, so core, plugins and game share one libc++ ABI.
#
# Run by UnrealBuildTool as the project's Linux pre-build step with the engine directory as the first argument;
# it also runs by hand: ./BuildCoreLinux.sh /path/to/UnrealEngine
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$REPO/build/linux-ue"

ENGINE="${1:-${UNREAL_ENGINE_ROOT:-}}"
if [ -z "$ENGINE" ]; then
    echo "BuildCoreLinux: pass the engine directory, or set UNREAL_ENGINE_ROOT." >&2
    exit 1
fi
# UnrealBuildTool expands $(EngineDir) to <root>/Engine; take either that or the root.
ENGINE="${ENGINE%/}"
ENGINE="${ENGINE%/Engine}"

# The toolchain Setup.sh installs in-tree; the version directory carries the compiler version, so glob for it.
TOOLCHAIN="$(echo "$ENGINE"/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/*/x86_64-unknown-linux-gnu)"
if [ ! -x "$TOOLCHAIN/bin/clang++" ]; then
    echo "BuildCoreLinux: no Linux toolchain under $ENGINE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64" >&2
    exit 1
fi

# UnrealBuildTool runs this once per target; two targets at once would share the build directory.
if command -v flock >/dev/null 2>&1; then
    exec 9>"${TMPDIR:-/tmp}/balls_core_linux.lock"
    flock 9
fi

cmake -S "$REPO" -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/LinuxToolchainNative.cmake" \
    -DBALLS_LINUX_TOOLCHAIN="$TOOLCHAIN" \
    -DBALLS_FRONTEND=none -DBALLS_ENABLE_CUDA=OFF -DBALLS_BUILD_TOOLS=OFF
cmake --build "$OUT" --target balls_core balls_physics_plugins --parallel

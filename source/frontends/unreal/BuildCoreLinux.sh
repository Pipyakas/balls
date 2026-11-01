set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$REPO/build/linux-ue"

ENGINE="${1:-${UNREAL_ENGINE_ROOT:-}}"
if [ -z "$ENGINE" ]; then
    echo "BuildCoreLinux: pass the engine directory, or set UNREAL_ENGINE_ROOT." >&2
    exit 1
fi
ENGINE="${ENGINE%/}"
ENGINE="${ENGINE%/Engine}"

TOOLCHAIN="$(echo "$ENGINE"/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/*/x86_64-unknown-linux-gnu)"
if [ ! -x "$TOOLCHAIN/bin/clang++" ]; then
    echo "BuildCoreLinux: no Linux toolchain under $ENGINE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64" >&2
    exit 1
fi

if command -v flock >/dev/null 2>&1; then
    exec 9>"${TMPDIR:-/tmp}/balls_core_linux.lock"
    flock 9
fi

export BALLS_LINUX_TOOLCHAIN="$TOOLCHAIN"
cmake -S "$REPO" -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/LinuxToolchainNative.cmake" \
    -DBALLS_FRONTEND=none -DBALLS_ENABLE_CUDA=OFF -DBALLS_BUILD_TOOLS=OFF
cmake --build "$OUT" --target balls_core balls_physics_plugins --parallel

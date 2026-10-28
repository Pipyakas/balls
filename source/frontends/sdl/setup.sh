#!/usr/bin/env bash
# Checks that this machine can build one SDL front-end variant on Linux and offers to install whatever is missing
# with the system package manager. Exits 0 when every required piece is present.
#   setup.sh <variant>      linux-nvrhi, linux-webgpu, linux-sdlgpu or android
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
variant=${1:-linux-nvrhi}
minimum_cmake=3.25
minimum_jdk=17
ndk_version=27.2.12479018
android_platform=android-35
failures=0
missing_packages=()

report() {
    if [ "$2" = ok ]; then
        printf '  [ ok ] %-22s %s\n' "$1" "$3"
    else
        printf '  [MISS] %-22s %s\n' "$1" "$3"
        failures=$((failures + 1))
    fi
}

version_at_least() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" = "$2" ]
}

# Package names for the distributions the team uses; others get the generic names in the message.
package_for() {
    if command -v apt-get >/dev/null 2>&1; then
        case "$1" in
            compiler) echo build-essential ;;
            cmake) echo cmake ;;
            ninja) echo ninja-build ;;
            git) echo git ;;
            pkg-config) echo pkg-config ;;
            x11) echo "libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev" ;;
            wayland) echo "libwayland-dev libdecor-0-dev wayland-protocols" ;;
            jdk) echo openjdk-17-jdk ;;
        esac
    elif command -v dnf >/dev/null 2>&1; then
        case "$1" in
            compiler) echo "gcc-c++ make" ;;
            cmake) echo cmake ;;
            ninja) echo ninja-build ;;
            git) echo git ;;
            pkg-config) echo pkgconf-pkg-config ;;
            x11) echo "libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXScrnSaver-devel libxkbcommon-devel" ;;
            wayland) echo "wayland-devel libdecor-devel wayland-protocols-devel" ;;
            jdk) echo java-17-openjdk-devel ;;
        esac
    else
        echo "$1"
    fi
}

need() {
    report "$1" miss "$2"
    missing_packages+=($(package_for "$3"))
}

check_desktop() {
    if command -v c++ >/dev/null 2>&1; then
        report "C++ compiler" ok "$(c++ --version | head -n 1)"
    else
        need "C++ compiler" "needs GCC 12+ or Clang 15+" compiler
    fi
    if command -v cmake >/dev/null 2>&1 && version_at_least "$(cmake --version | head -n 1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')" "$minimum_cmake"; then
        report CMake ok "$(cmake --version | head -n 1)"
    else
        need CMake "needs $minimum_cmake or newer" cmake
    fi
    if command -v ninja >/dev/null 2>&1 || command -v make >/dev/null 2>&1; then
        report "Ninja or Make" ok "$(command -v ninja || command -v make)"
    else
        need "Ninja or Make" "CMake needs a build tool" ninja
    fi
    if command -v git >/dev/null 2>&1; then
        report Git ok "(CMake clones NVRHI with it)"
    else
        need Git "CMake clones NVRHI with it" git
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        need pkg-config "SDL finds the window system libraries with it" pkg-config
    elif pkg-config --exists x11 xext || pkg-config --exists wayland-client; then
        report "X11 or Wayland headers" ok "SDL builds its video backend from them"
    else
        need "X11 or Wayland headers" "SDL needs at least one window system to build against" x11
        missing_packages+=($(package_for wayland))
    fi
    if [ ! -d "$root/build/${variant/linux-nvrhi/linux}/_deps" ]; then
        if curl -sfI --max-time 8 https://github.com >/dev/null 2>&1; then
            report Internet ok "the first configure downloads SDL, Dear ImGui and NVRHI"
        else
            report Internet miss "the first configure downloads SDL, Dear ImGui and NVRHI from github.com"
        fi
    fi
}

check_android() {
    local java=""
    if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/java" ]; then java="$JAVA_HOME/bin/java"; fi
    if [ -z "$java" ] && command -v java >/dev/null 2>&1; then java=$(command -v java); fi
    local major=0
    if [ -n "$java" ]; then
        major=$("$java" -version 2>&1 | head -n 1 | sed -E 's/.*version "([0-9]+)(\.([0-9]+))?.*/\1 \3/' | awk '{ print ($1 == 1) ? $2 : $1 }')
    fi
    if [ "${major:-0}" -ge "$minimum_jdk" ] 2>/dev/null; then
        report JDK ok "$major ($java)"
    else
        need JDK "needs JDK $minimum_jdk or newer (Gradle runs on it)" jdk
    fi
    local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [ -z "$sdk" ] && [ -f "$here/android/local.properties" ]; then
        sdk=$(sed -n 's/^sdk\.dir=//p' "$here/android/local.properties")
    fi
    [ -z "$sdk" ] && [ -d "$HOME/Android/Sdk" ] && sdk="$HOME/Android/Sdk"
    if [ -z "$sdk" ] || [ ! -d "$sdk" ]; then
        report "Android SDK" miss "set ANDROID_HOME, or install Android Studio and open it once"
        return
    fi
    report "Android SDK" ok "$sdk"
    local packages=()
    if [ -f "$sdk/ndk/$ndk_version/source.properties" ]; then report "Android NDK" ok "$ndk_version"; else report "Android NDK" miss "$ndk_version"; packages+=("ndk;$ndk_version"); fi
    if [ -d "$sdk/platforms/$android_platform" ]; then report "Android platform" ok "$android_platform"; else report "Android platform" miss "$android_platform"; packages+=("platforms;$android_platform"); fi
    local cmake_dir
    cmake_dir=$(ls -1 "$sdk/cmake" 2>/dev/null | sort -V | tail -n 1)
    if [ -n "$cmake_dir" ] && version_at_least "$cmake_dir" "$minimum_cmake"; then report "Android CMake" ok "$cmake_dir"; else report "Android CMake" miss "needs $minimum_cmake or newer in the SDK"; packages+=("cmake;4.0.2"); fi
    if [ ${#packages[@]} -gt 0 ]; then
        local manager
        manager=$(ls -1 "$sdk"/cmdline-tools/*/bin/sdkmanager 2>/dev/null | tail -n 1)
        if [ -n "$manager" ] && [ -t 0 ]; then
            read -r -p "    Install ${packages[*]} with sdkmanager now? [Y/n] " answer
            if [ -z "$answer" ] || [[ "$answer" =~ ^[Yy] ]]; then
                "$manager" --sdk_root="$sdk" --install "${packages[@]}" && failures=$((failures - ${#packages[@]}))
            fi
        else
            echo "    Install with Android Studio's SDK Manager: ${packages[*]}"
        fi
    fi
}

echo "Checking this machine can build $variant..."
case "$variant" in
    linux-nvrhi | linux-webgpu | linux-sdlgpu) check_desktop ;;
    android) check_android ;;
    *) echo "  setup.sh does not know the variant '$variant'." >&2; exit 2 ;;
esac

if [ ${#missing_packages[@]} -gt 0 ]; then
    install=""
    if command -v apt-get >/dev/null 2>&1; then install="sudo apt-get install -y"
    elif command -v dnf >/dev/null 2>&1; then install="sudo dnf install -y"
    fi
    if [ -n "$install" ] && [ -t 0 ]; then
        read -r -p "    Install ${missing_packages[*]} now with '$install'? [Y/n] " answer
        if [ -z "$answer" ] || [[ "$answer" =~ ^[Yy] ]]; then
            $install "${missing_packages[@]}" && exec "$0" "$variant"
        fi
    else
        echo "    Install these packages: ${missing_packages[*]}"
    fi
fi

if [ "$failures" -gt 0 ]; then
    echo "This machine is missing $failures thing(s) needed to build $variant; see MISS above." >&2
    exit 1
fi
echo "Ready to build $variant."

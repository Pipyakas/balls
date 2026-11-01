#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

usage() {
    echo "Usage: ./build.sh [--setup] [--menu | <frontend> [variant]]"
    echo "  no arguments   build SDL + NVRHI for Linux, into dist/linux-sdl"
    echo "  --menu, -m     choose a front end and platform from a list"
    echo "  sdl android    build one variant directly; ./build.sh --menu lists them"
    echo "  --setup        only check this machine has what that build needs, offering to install anything missing"
}

setup_flag=()
if [ "${1:-}" = "--setup" ]; then
    setup_flag=(--setup)
    shift
fi

case "${1:-}" in
    "") exec source/frontends/sdl/build.sh "${setup_flag[@]}" ;;
    --help | -h) usage; exit 0 ;;
    --menu | -m) ;;
    *)
        script="source/frontends/$1/build.sh"
        if [ ! -x "$script" ]; then
            echo "Unknown front end \"$1\"." >&2
            usage >&2
            exit 1
        fi
        exec "$script" "${setup_flag[@]}" "${@:2}"
        ;;
esac

scripts=()
variants=()
echo "Build which version?"
for script in source/frontends/*/build.sh; do
    [ -x "$script" ] || continue
    frontend=$(basename "$(dirname "$script")")
    while IFS=';' read -r id description; do
        scripts+=("$script")
        variants+=("$id")
        echo "  ${#variants[@]}. $frontend $id: $description"
    done < <("$script" --variants)
done
if [ ${
    echo "No front end has a build.sh." >&2
    exit 1
fi
read -r -p "Number [1]: " choice
choice=${choice:-1}
if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt ${
    echo "\"$choice\" is not on the list." >&2
    exit 1
fi
echo
exec "${scripts[choice - 1]}" "${setup_flag[@]}" "${variants[choice - 1]}"

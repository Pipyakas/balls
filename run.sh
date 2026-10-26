#!/usr/bin/env sh
# Run Balls from this checkout. Arguments go to the game, so
#   ./run.sh --play normal DA_49Bricks --backend avx2
# starts a level directly. Pass --build to rebuild first.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary="$root/build/bin/ColorBlastBalls"

if [ "${1:-}" = "--build" ]; then
    shift
    "$root/build.sh"
fi

if [ ! -x "$binary" ]; then
    echo "Not built yet: $binary" >&2
    echo "Run $root/build.sh (or ./run.sh --build)." >&2
    exit 1
fi

exec "$binary" "$@"

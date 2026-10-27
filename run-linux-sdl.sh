#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

./build-linux-sdl.sh
cd dist/linux-sdl
exec ./ColorBlastBalls "$@"

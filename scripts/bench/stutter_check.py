"""Watch the game's frame times while it fills a board, to check that adding balls slows frames gradually.

Starts the game hidden on a Performance level, then either runs the capacity probe or fills to a count, and samples
/state: live balls, the average and worst work frame rate of the last second, and balls still waiting to spawn. A
second whose worst frame is far below its average is a stutter.

    python -I scripts/bench/stutter_check.py --level Edge_Tough_48x48 --mode probe --seconds 40
"""

import argparse
import json
import subprocess
import time
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GAME = REPO_ROOT / "build" / "win32" / "bin" / "Release" / "balls.exe"
STUTTER_RATIO = 0.5


def call(port, path):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=15) as response:
        return json.loads(response.read())


def wait_for_server(port, timeout_seconds=20.0):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        try:
            return call(port, "/state")
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("game did not start its control server")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default=str(DEFAULT_GAME))
    parser.add_argument("--level", default="DA_1000Bricks")
    parser.add_argument("--backend")
    parser.add_argument("--mode", choices=["probe", "fill"], default="probe")
    parser.add_argument("--count", type=int, default=100_000_000)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--interval", type=float, default=0.5)
    parser.add_argument("--port", type=int, default=7790)
    arguments = parser.parse_args()

    command = [arguments.game, "--play", "performance", arguments.level, "--control-port", str(arguments.port),
               "--hidden"]
    if arguments.backend:
        command += ["--backend", arguments.backend]
    game = subprocess.Popen(command, cwd=Path(arguments.game).parent)
    try:
        wait_for_server(arguments.port)
        if arguments.mode == "probe":
            call(arguments.port, "/probe?fps=60")
        else:
            call(arguments.port, "/start")
            call(arguments.port, f"/fill?count={arguments.count}")
        stutters = 0
        start = time.time()
        while time.time() - start < arguments.seconds:
            time.sleep(arguments.interval)
            state = call(arguments.port, "/state")
            details = state["details"]
            stutter = state["minWorkFps"] < state["workFps"] * STUTTER_RATIO
            stutters += 1 if stutter else 0
            print(f"{time.time() - start:6.1f} s  balls {details['balls']:>14,}  waiting {details['pendingSpawn']:>14,}  "
                  f"work {state['workFps']:7.1f} fps  worst {state['minWorkFps']:7.1f} fps  "
                  f"probe {details['probe']['phase']:<10} {details['backend']}{'  STUTTER' if stutter else ''}",
                  flush=True)
        print(f"{stutters} stuttering samples")
    finally:
        game.terminate()
        game.wait(timeout=10)


if __name__ == "__main__":
    main()

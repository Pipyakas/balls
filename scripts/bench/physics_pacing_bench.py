"""Compare physics frame pacings in the running game: how much of each second the physics thread works, and how fast
the UI draws, with the same balls on the board.

For each level and each pacing (Settings > Advanced > Physics frames: 60, 120, 240 or uncapped), the game fills the
board to a fixed count, settles, and samples /state: physics frames per second, physics frame time, their product
(the share of a second the physics thread is busy), and the UI frame rate. Attach to a game already started with
--control-port, or let the script start one.

    python -I scripts/bench/physics_pacing_bench.py --attach --port 7781 --case Edge_Empty_512x512:2000000000
"""

import argparse
import json
import statistics
import subprocess
import time
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GAME = REPO_ROOT / "build" / "win32" / "bin" / "Release" / "balls.exe"
PACINGS = [("60", 60), ("120", 120), ("240", 240), ("uncapped", -1)]


def call(port, path, timeout=30):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=timeout) as response:
        return json.loads(response.read())


def wait_for_server(port, timeout_seconds=60.0):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        try:
            return call(port, "/state")
        except OSError:
            time.sleep(0.5)
    raise RuntimeError("game did not start its control server")


def wait_for_backend(port, timeout_seconds=60.0):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        details = call(port, "/state").get("details", {})
        if details and not details.get("pendingBackend"):
            return
        time.sleep(0.25)


def fill(port, level, backend, balls, timeout_seconds):
    call(port, f"/play?mode=performance&level={level}")
    time.sleep(0.5)
    call(port, f"/backend?kind={backend}")
    wait_for_backend(port)
    call(port, "/start")
    call(port, f"/fill?count={balls}")
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        details = call(port, "/state")["details"]
        if details["pendingSpawn"] == 0 or details["balls"] >= balls * 0.99:
            return details["balls"]
        time.sleep(1.0)
    return call(port, "/state")["details"]["balls"]


def measure(port, seconds):
    samples = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        time.sleep(0.5)
        samples.append(call(port, "/state"))
    physics_fps = statistics.fmean(s["details"]["physicsFps"] for s in samples)
    physics_ms = statistics.fmean(s["details"]["physicsFrameMs"] for s in samples)
    return {
        "balls": samples[-1]["details"]["balls"],
        "physicsFps": physics_fps,
        "physicsMs": physics_ms,
        "physicsBusy": physics_fps * physics_ms / 1000.0,
        "uiFps": statistics.fmean(s["fps"] for s in samples),
        "uiMinFps": min(s["minFps"] for s in samples),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--attach", action="store_true")
    parser.add_argument("--game", default=str(DEFAULT_GAME))
    parser.add_argument("--port", type=int, default=7781)
    parser.add_argument("--backend", default="hybrid")
    parser.add_argument("--case", action="append", required=True, help="level:balls, e.g. Edge_Empty_512x512:2000000000")
    parser.add_argument("--settle", type=float, default=5.0)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--fill-timeout", type=float, default=180.0)
    arguments = parser.parse_args()

    game = None
    if not arguments.attach:
        game = subprocess.Popen([arguments.game, "--hidden", "--size", "1920", "1080", "--control-port",
                                 str(arguments.port)], cwd=Path(arguments.game).parent)
    try:
        wait_for_server(arguments.port)
        for case in arguments.case:
            level, balls = case.rsplit(":", 1)
            for name, rate in PACINGS:
                call(arguments.port, f"/settings?physicsFrames={rate}")
                held = fill(arguments.port, level, arguments.backend, int(balls), arguments.fill_timeout)
                time.sleep(arguments.settle)
                result = measure(arguments.port, arguments.seconds)
                print(f"{level:22} {arguments.backend:6} {name:>8}  balls {result['balls']:>14,}  "
                      f"physics {result['physicsFps']:7.1f} fps x {result['physicsMs']:6.2f} ms = "
                      f"{result['physicsBusy'] * 100:5.1f}% busy  ui {result['uiFps']:7.1f} fps (min {result['uiMinFps']:6.1f})",
                      flush=True)
    finally:
        try:
            call(arguments.port, "/settings?physicsFrames=0")
            if game:
                call(arguments.port, "/quit")
        except OSError:
            pass
        if game:
            game.wait(timeout=30)


if __name__ == "__main__":
    main()

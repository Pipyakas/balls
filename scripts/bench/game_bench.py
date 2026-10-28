import argparse
import json
import statistics
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

MULTIPLIER_INDEX = {1: 0, 10: 1, 100: 2, 1000: 3, 10000: 4}
MAX_SPAWN_RATE_INDEX = 5
UNLIMITED_BALLS_INDEX = 4


def call(port, path):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=15) as response:
        return json.loads(response.read())


def wait_for_server(port, timeout_seconds=15.0):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        try:
            return call(port, "/state")
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("game did not start its control server")


# Backend switches finish off the frame, so a run that measures one waits until the game has adopted it.
def wait_for_backend(port, timeout_seconds=120.0):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if not call(port, "/state")["details"].get("pendingBackend"):
            return
        time.sleep(0.1)
    raise RuntimeError("the backend switch did not finish")


def wait_until_spawned(port, timeout_seconds=30.0):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        details = call(port, "/state")["details"]
        if details["launchersLeft"] == 0 and details["pendingSpawn"] == 0:
            return
        time.sleep(0.25)


def sample(port, seconds, interval=0.25):
    samples = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        samples.append(call(port, "/state"))
        time.sleep(interval)
    return samples


def summarize(samples):
    def mean(key):
        return statistics.fmean(entry[key] for entry in samples)

    return {
        "balls": samples[-1]["details"]["balls"],
        "fps": mean("fps"),
        "minFps": min(entry["minFps"] for entry in samples),
        "cpuMs": mean("cpuMs"),
        "gpuMs": mean("gpuMs"),
        "presentMs": mean("presentMs"),
        "physicsMs": statistics.fmean(entry["details"]["physicsMs"] for entry in samples),
        "fault": samples[-1]["details"]["backendFault"],
    }


def run(arguments):
    process = None
    if not arguments.attach:
        command = [str(Path(arguments.exe).resolve()), "--hidden", "--control-port", str(arguments.port), "--play", "performance", arguments.level]
        process = subprocess.Popen(command)
    results = []
    try:
        wait_for_server(arguments.port, 60.0 if arguments.attach else 15.0)
        for backend in arguments.backends:
            for multiplier in arguments.multipliers:
                call(arguments.port, f"/play?mode=performance&level={arguments.level}")
                time.sleep(0.3)
                backend_reply = call(arguments.port, f"/backend?kind={backend}")
                if not backend_reply.get("ok", False):
                    print(f"{backend:7} unavailable", flush=True)
                    break
                wait_for_backend(arguments.port)
                call(arguments.port, f"/rate?index={MAX_SPAWN_RATE_INDEX}")
                call(arguments.port, f"/limit?index={UNLIMITED_BALLS_INDEX}")
                call(arguments.port, f"/multiplier?index={MULTIPLIER_INDEX[multiplier]}")
                call(arguments.port, "/start")
                wait_until_spawned(arguments.port)
                time.sleep(arguments.settle)
                summary = summarize(sample(arguments.port, arguments.seconds))
                summary.update({"backend": backend, "multiplier": multiplier})
                results.append(summary)
                print(
                    f"{backend:7} x{multiplier:<5} balls={summary['balls']:>9,}  fps={summary['fps']:6.1f} "
                    f"min={summary['minFps']:6.1f}  cpu={summary['cpuMs']:6.2f}  gpu={summary['gpuMs']:6.2f}  "
                    f"physics={summary['physicsMs']:6.2f}  present={summary['presentMs']:6.2f} {summary['fault']}",
                    flush=True,
                )
    finally:
        try:
            call(arguments.port, "/quit")
        except OSError:
            pass
        if process:
            process.wait(timeout=15)
    if arguments.json:
        Path(arguments.json).write_text(json.dumps(results, indent=2))
    return results


def main():
    parser = argparse.ArgumentParser(description="Headless in-game performance benchmark over the control API")
    parser.add_argument("--exe", default="build/bin/Release/balls.exe")
    parser.add_argument("--port", type=int, default=7781)
    parser.add_argument("--level", default="DA_1000Bricks")
    parser.add_argument("--backends", nargs="+", default=["avx2", "cuda", "scalar"])
    parser.add_argument("--multipliers", nargs="+", type=int, default=[1, 10, 100, 1000])
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--json")
    parser.add_argument("--attach", action="store_true", help="drive an already running game instead of launching one")
    run(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())

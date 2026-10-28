import argparse
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path


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


def probe_backend(port, level, backend, timeout_seconds, steady_seconds, target_fps):
    call(port, f"/play?mode=performance&level={level}")
    time.sleep(0.3)
    if not call(port, f"/backend?kind={backend}").get("ok", False):
        return None
    wait_for_backend(port)
    call(port, f"/probe?fps={target_fps}" if target_fps else "/probe")
    deadline = time.time() + timeout_seconds
    state = call(port, "/state")
    while time.time() < deadline and state["details"]["probe"]["phase"] != "done":
        time.sleep(1.0)
        state = call(port, "/state")
    time.sleep(steady_seconds)
    steady = call(port, "/state")
    return {
        "backend": backend,
        "limit": steady["details"]["probe"]["best"],
        "target": steady["details"]["probe"]["target"],
        "phase": steady["details"]["probe"]["phase"],
        "fps": steady["fps"],
        "minFps": steady["minFps"],
        "cpuMs": steady["cpuMs"],
        "gpuMs": steady["gpuMs"],
        "physicsMs": steady["details"]["physicsMs"],
        "kernelMs": steady["details"]["kernelMs"],
        "balls": steady["details"]["balls"],
        "fault": steady["details"]["backendFault"],
        "backendNow": steady["details"]["backend"],
    }


def main():
    parser = argparse.ArgumentParser(description="Run the in-game capacity probe per backend, headless")
    parser.add_argument("--exe", default="build/win32/bin/Release/balls.exe")
    parser.add_argument("--port", type=int, default=7782)
    parser.add_argument("--level", default="DA_1000Bricks")
    parser.add_argument("--backends", nargs="+", default=["cuda", "avx2"])
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--steady", type=float, default=3.0)
    parser.add_argument("--bytes-per-ball", type=int, default=12, help="bytes streamed per ball per frame by the physics kernel")
    parser.add_argument("--fps", type=int, default=0, help="probe target FPS; default is the monitor refresh rate")
    arguments = parser.parse_args()

    command = [str(Path(arguments.exe).resolve()), "--hidden", "--control-port", str(arguments.port)]
    process = subprocess.Popen(command)
    try:
        wait_for_server(arguments.port)
        for backend in arguments.backends:
            result = probe_backend(arguments.port, arguments.level, backend, arguments.timeout, arguments.steady, arguments.fps)
            if result is None:
                print(f"{backend:7} unavailable", flush=True)
                continue
            print(
                f"{backend:7} limit={result['limit']:>11,} @{result['target']:.0f} ({result['phase']})  steady fps={result['fps']:5.1f} "
                f"min={result['minFps']:5.1f}  cpu={result['cpuMs']:6.2f}  gpu={result['gpuMs']:6.2f}  "
                f"physics={result['physicsMs']:6.2f}  kernel={result['kernelMs']:6.2f}"
                + (f"  {result['balls'] * arguments.bytes_per_ball / (result['kernelMs'] * 1e6):6.1f} GB/s" if result['kernelMs'] > 0 else "")
                + f"  now={result['backendNow']} {result['fault']}",
                flush=True,
            )
    finally:
        try:
            call(arguments.port, "/quit")
        except OSError:
            pass
        process.wait(timeout=15)


if __name__ == "__main__":
    sys.exit(main())

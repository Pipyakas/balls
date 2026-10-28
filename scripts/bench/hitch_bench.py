"""Measures how long the game freezes when each option is toggled.

Starts the game (or attaches to one with --attach), fills a Performance level with balls, then flips every option the
control API exposes: physics backend, compute and render GPU, graphics API, renderer, VSync, ball collisions, layout,
spawn and limit options, pause, the settings overlay, restarting and switching levels, and resizing the window. For
each it reports the longest gap between two frame starts while the toggle happened and settled (from /hitch, which
includes the renderer and adapter swaps that the per-frame timings do not), and flags anything over --budget.

    python scripts/bench/hitch_bench.py --exe dist/win32-sdl/balls.exe --balls 10000000
"""

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

BACKENDS = ["scalar", "avx2", "cuda", "cuda-avx2", "vulkan"]


def call(port, route, **parameters):
    query = urllib.parse.urlencode(parameters)
    url = f"http://127.0.0.1:{port}{route}" + (f"?{query}" if query else "")
    try:
        with urllib.request.urlopen(url, timeout=120) as response:
            return json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        return {"ok": False, "status": error.code, "error": error.read().decode(errors="replace")}


def wait_for_server(port, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            return call(port, "/state")
        except OSError:
            time.sleep(0.3)
    raise TimeoutError("the game's control server did not answer")


class Bench:
    def __init__(self, port, settle, budget):
        self.port = port
        self.settle = settle
        self.budget = budget
        self.results = []

    # Backend switches and refills finish over later frames; the freeze that matters can happen at any of them.
    def wait_for_background_work(self, seconds=60.0):
        deadline = time.time() + seconds
        while time.time() < deadline:
            details = call(self.port, "/state").get("details", {})
            if not details.get("pendingBackend") and not details.get("refilling"):
                return
            time.sleep(0.1)

    def measure(self, name, route, **parameters):
        call(self.port, "/hitch")
        started = time.perf_counter()
        reply = call(self.port, route, **parameters)
        replied_ms = (time.perf_counter() - started) * 1000.0
        self.wait_for_background_work()
        time.sleep(self.settle)
        longest = call(self.port, "/hitch").get("longestFrameMs", 0.0)
        result = {"action": name, "longestFrameMs": longest, "replyMs": round(replied_ms, 1),
                  "ok": reply.get("ok", True)}
        self.results.append(result)
        flag = "  OVER" if longest > self.budget else ""
        note = "" if result["ok"] else f"  ({reply.get('error', 'refused')[:60].strip()})"
        print(f"{name:42} {longest:9.1f} ms{flag}{note}", flush=True)
        return reply


def run(arguments):
    process = None
    if not arguments.attach:
        command = [str(Path(arguments.exe).resolve()), "--control-port", str(arguments.port)]
        process = subprocess.Popen(command)
    bench = Bench(arguments.port, arguments.settle, arguments.budget)
    saved_settings = None
    try:
        state = wait_for_server(arguments.port, 60.0)
        # Settings persist, so the toggles below would otherwise leave the player's game changed.
        saved_settings = call(arguments.port, "/settings")
        render_adapters = state.get("renderAdapters", [])
        compute_adapters = state.get("computeAdapters", [])
        call(arguments.port, "/play", mode="performance", level=arguments.level)
        time.sleep(1.0)
        call(arguments.port, "/limit", index=4)
        call(arguments.port, "/fill", count=arguments.balls)
        time.sleep(2.0)
        print(f"{'action':42} {'longest frame':>12}   (budget {arguments.budget:.1f} ms, {arguments.balls:,} balls)")
        bench.measure("idle", "/state")

        for backend in BACKENDS + [BACKENDS[0]]:
            bench.measure(f"backend -> {backend}", "/backend", kind=backend)
            call(arguments.port, "/fill", count=arguments.balls)
            time.sleep(1.0)
        bench.measure(f"backend -> {arguments.backend}", "/backend", kind=arguments.backend)
        call(arguments.port, "/fill", count=arguments.balls)
        time.sleep(1.0)

        for index, name in enumerate(compute_adapters):
            bench.measure(f"compute GPU -> {name[:24]}", "/compute-gpu", index=index)
        for index, name in list(enumerate(render_adapters)) + [(0, render_adapters[0] if render_adapters else "")]:
            bench.measure(f"render GPU -> {name[:24]}", "/render-gpu", index=index)
        for api in ["vulkan", "d3d12", "auto"]:
            bench.measure(f"graphics API -> {api}", "/settings", api=api)
        for renderer in ["webgpu", "sdlgpu", "nvrhi"]:
            bench.measure(f"renderer -> {renderer}", "/settings", renderer=renderer)
        bench.measure("VSync off", "/vsync", on=0)
        bench.measure("VSync on", "/vsync", on=1)
        bench.measure("ball collisions on", "/settings", collisions=1)
        bench.measure("ball collisions off", "/settings", collisions=0)
        bench.measure("layout -> portrait", "/settings", layout="portrait")
        bench.measure("layout -> landscape", "/settings", layout="landscape")
        bench.measure("layout -> auto", "/settings", layout="auto")
        bench.measure("window -> 1280x720", "/window", width=1280, height=720)
        bench.measure("window -> 1440x900", "/window", width=1440, height=900)
        bench.measure("spawn rate -> max", "/rate", index=5)
        bench.measure("ball count -> x1000", "/multiplier", index=3)
        bench.measure("ball limit -> 1M", "/limit", index=2)
        bench.measure("ball limit -> max", "/limit", index=4)
        bench.measure("pause", "/pause")
        bench.measure("resume", "/resume")
        bench.measure("open settings overlay", "/settings", open=1)
        bench.measure("restart level", "/restart")
        bench.measure("switch level", "/play", mode="normal", level="DA_FirstLevel")
        bench.measure("back to menu", "/menu")
    finally:
        try:
            if saved_settings:
                call(arguments.port, "/settings", collisions=int(saved_settings["ballCollisions"]),
                     layout=saved_settings["layout"], vsync=int(saved_settings["vsync"]),
                     renderer=saved_settings["renderer"], api=saved_settings["graphicsApi"],
                     backend=saved_settings["defaultBackend"].lower())
            call(arguments.port, "/quit")
        except OSError:
            pass
        if process:
            process.wait(timeout=30)

    worst = sorted(bench.results, key=lambda result: -result["longestFrameMs"])
    over = [result for result in worst if result["longestFrameMs"] > arguments.budget]
    print(f"\n{len(over)} of {len(worst)} actions froze the game for longer than {arguments.budget:.1f} ms")
    if arguments.json:
        Path(arguments.json).write_text(json.dumps(bench.results, indent=2))
    return 1 if over else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default="dist/win32-sdl/balls.exe")
    parser.add_argument("--attach", action="store_true", help="use a game already running with --control-port")
    parser.add_argument("--port", type=int, default=7781)
    parser.add_argument("--level", default="DA_1000Bricks")
    parser.add_argument("--balls", type=int, default=10_000_000)
    parser.add_argument("--backend", default="cuda-avx2", help="the backend to measure the other toggles on")
    parser.add_argument("--settle", type=float, default=1.5, help="seconds to wait after each toggle")
    parser.add_argument("--budget", type=float, default=1000.0 / 30.0,
                        help="longest acceptable frame in ms; the default allows one missed 60 Hz frame")
    parser.add_argument("--json")
    sys.exit(run(parser.parse_args()))


if __name__ == "__main__":
    main()

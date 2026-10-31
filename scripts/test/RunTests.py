#!/usr/bin/env python3
"""Verification pipeline: the core's logic tests, then a UI pass over each front end through its control API.

    python -I scripts/test/RunTests.py                     every stage on this checkout's builds
    python -I scripts/test/RunTests.py --stages sdl ue     only the UI passes
    python -I scripts/test/RunTests.py --root E:/balls/wt-submission --out C:/scratch/verify

Stages:
  logic  builds and runs balls_tests (unit tests of the core) and balls_bench --accuracy (every physics backend
         against the reference backend).
  sdl    drives dist/win32-sdl/balls.exe, the submission build.
  ue     drives dist/win32-ue/Balls.exe.

A UI pass starts the game on the main menu, visits every screen the flow has, plays every shipped Normal level to
the win, starts and fills every Performance level, checks that a broken map lands on the load error screen and a
repaired one shows its warning, and quits. Each check takes a screenshot; report.html lists them with the result.
The game must exit on its own when asked to quit, and a crash fails the stage.

Stdlib only, safe for `python -I`.
"""

import argparse
import html
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTROL_PORT = {"sdl": 7851, "ue": 7852}
SCREEN_WIDTH, SCREEN_HEIGHT = 1600, 900
BOOT_SECONDS = 120
LOAD_SECONDS = 90
WIN_SECONDS = 180
FILL_SECONDS = 25
QUIT_SECONDS = 30
POLL_SECONDS = 0.25
# Below this, a level that holds balls is not playable; the game's target is 60, so a dip under 20 is a real fault
# rather than load from a screenshot or a fill.
MINIMUM_PLAYABLE_FPS = 20.0
TEST_LEVEL_PREFIX = "ZzPipeline"

BROKEN_BRICKS = "[Board]\nWidth 2\nHeight 1\nPlayableHeight 4\n[Layout]\n0 zero\n[BrickTypes]\n0 Red 1\n"
REPAIRED_BRICKS = "[Board]\nWidth 2\nHeight 1\n[Layout]\n0 1\n[BrickTypes]\n0 Red 1\n1 Red 1\n"
TWO_RED_BALLS = "[Board]\nWidth 1\nHeight 1\n[Layout]\nRed(2)\n"


@dataclass
class Check:
    stage: str
    name: str
    passed: bool
    detail: str = ""
    screenshot: str = ""
    seconds: float = 0.0


@dataclass
class Report:
    out: Path
    checks: list = field(default_factory=list)

    def add(self, check):
        self.checks.append(check)
        mark = "pass" if check.passed else "FAIL"
        print(f"[{mark}] {check.stage}: {check.name}" + (f" - {check.detail}" if check.detail else ""), flush=True)

    def failed(self):
        return [check for check in self.checks if not check.passed]

    def write(self):
        (self.out / "report.json").write_text(json.dumps([check.__dict__ for check in self.checks], indent=2),
                                              encoding="utf-8")
        rows = []
        for check in self.checks:
            image = (f'<a href="{html.escape(check.screenshot)}"><img src="{html.escape(check.screenshot)}"></a>'
                     if check.screenshot else "")
            rows.append(f'<tr class="{"pass" if check.passed else "fail"}"><td>{html.escape(check.stage)}</td>'
                        f'<td>{html.escape(check.name)}</td><td>{"pass" if check.passed else "FAIL"}</td>'
                        f'<td>{check.seconds:.1f} s</td><td>{html.escape(check.detail)}</td><td>{image}</td></tr>')
        failed = len(self.failed())
        summary = f"{len(self.checks) - failed} passed, {failed} failed"
        (self.out / "report.html").write_text(
            "<!doctype html><meta charset=utf-8><title>balls verification</title><style>"
            "body{font:14px sans-serif;background:#161616;color:#ddd}table{border-collapse:collapse}"
            "td{border-bottom:1px solid #333;padding:4px 8px;vertical-align:top}.fail td{color:#f66}"
            "img{width:320px}</style>"
            f"<h1>balls verification</h1><p>{html.escape(summary)}, {time.strftime('%Y-%m-%d %H:%M')}</p>"
            "<table><tr><th>stage</th><th>check</th><th>result</th><th>time</th><th>detail</th><th>screen</th></tr>"
            + "".join(rows) + "</table>", encoding="utf-8")


class StageFailure(Exception):
    pass


def run(command, cwd, log):
    with open(log, "w", encoding="utf-8", errors="replace") as output:
        return subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT).returncode


def tail(path, lines=15):
    try:
        return "\n".join(Path(path).read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])
    except OSError:
        return ""


def logic_stage(root, out, report, prebuilt):
    build = root / "build" / "verify"
    started = time.time()
    if prebuilt:
        present = all((build / "bin" / "Release" / name).exists() for name in ("balls_tests.exe", "balls_bench.exe"))
        report.add(Check("logic", "prebuilt balls_tests and balls_bench present", present,
                         "" if present else f"nothing in {build / 'bin' / 'Release'}"))
        if present:
            run_logic_binaries(root, build / "bin" / "Release", out, report)
        return
    if run(["cmake", "-S", str(root), "-B", str(build), "-G", "Visual Studio 17 2022", "-A", "x64",
            "-DBALLS_FRONTEND=none", "-DBALLS_BUILD_TOOLS=ON"], root, out / "logic-configure.log") != 0:
        report.add(Check("logic", "configure", False, tail(out / "logic-configure.log"), seconds=time.time() - started))
        return
    code = run(["cmake", "--build", str(build), "--config", "Release", "--parallel", "--target", "balls_tests",
                "balls_bench"], root, out / "logic-build.log")
    report.add(Check("logic", "build balls_tests and balls_bench", code == 0,
                     "" if code == 0 else tail(out / "logic-build.log"), seconds=time.time() - started))
    if code != 0:
        return
    run_logic_binaries(root, build / "bin" / "Release", out, report)


def run_logic_binaries(root, binaries, out, report):
    started = time.time()
    code = run([str(binaries / "balls_tests.exe")], binaries, out / "logic-tests.log")
    lines = tail(out / "logic-tests.log", 400).splitlines()
    failures = [line for line in lines if line.startswith("[FAIL]")]
    totals = next((line for line in reversed(lines) if " tests, " in line), "no summary")
    report.add(Check("logic", "balls_tests", code == 0 and not failures,
                     totals + ("; " + "; ".join(failures[:5]) if failures else ""), seconds=time.time() - started))
    started = time.time()
    # The bench names its levels relative to the checkout root.
    code = run([str(binaries / "balls_bench.exe"), "--accuracy"], root, out / "logic-accuracy.log")
    report.add(Check("logic", "physics accuracy against the reference backend", code == 0,
                     tail(out / "logic-accuracy.log", 3).replace("\n", " | "), seconds=time.time() - started))


class Game:
    """One front end started with its control server, driven over HTTP."""

    def __init__(self, frontend, executable, out, headless=False):
        self.frontend = frontend
        self.headless = headless
        self.executable = executable
        self.out = out
        self.port = CONTROL_PORT[frontend]
        self.process = None
        self.shots = 0

    def start(self):
        if self.frontend == "sdl":
            arguments = ["--size", str(SCREEN_WIDTH), str(SCREEN_HEIGHT), "--control-port", str(self.port)]
            if self.headless:
                arguments.insert(0, "--hidden")
        else:
            arguments = [f"-BallsControlPort={self.port}", "-windowed", f"-ResX={SCREEN_WIDTH}",
                         f"-ResY={SCREEN_HEIGHT}", "-nosound"]
            if self.headless:
                arguments += ["-RenderOffscreen", "-ForceRes"]
        log = open(self.out / f"{self.frontend}-game.log", "w", encoding="utf-8", errors="replace")
        self.process = subprocess.Popen([str(self.executable), *arguments], cwd=self.executable.parent, stdout=log,
                                        stderr=subprocess.STDOUT)
        self.wait_for(lambda state: state.get("ok"), BOOT_SECONDS, "the control server to answer")

    def alive(self):
        return self.process is not None and self.process.poll() is None

    def call(self, route, **parameters):
        query = ("?" + urllib.parse.urlencode(parameters)) if parameters else ""
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{self.port}{route}{query}", timeout=20) as response:
                body = response.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as error:
            raise StageFailure(f"{route} answered {error.code}: {error.read().decode('utf-8', 'replace')}") from error
        try:
            return json.loads(body)
        except json.JSONDecodeError:
            return {"ok": True, "body": body}

    def state(self):
        return self.call("/state")

    def wait_for(self, condition, seconds, what):
        deadline = time.time() + seconds
        last = None
        while time.time() < deadline:
            if not self.alive():
                raise StageFailure(f"the game exited (code {self.process.poll()}) while waiting for {what}")
            try:
                last = self.state()
                if condition(last):
                    return last
            except (StageFailure, OSError, urllib.error.URLError):
                pass
            time.sleep(POLL_SECONDS)
        raise StageFailure(f"timed out after {seconds} s waiting for {what}; last state {json.dumps(last)[:300]}")

    def screenshot(self, name):
        self.shots += 1
        # SDL writes a BMP whatever the name says; browsers show either.
        extension = "bmp" if self.frontend == "sdl" else "png"
        file_name = f"{self.frontend}-{self.shots:03d}-{name}.{extension}"
        path = (self.out / file_name).resolve()
        try:
            self.call("/screenshot", path=str(path).replace("\\", "/"))
        except StageFailure:
            return ""
        deadline = time.time() + 10
        while time.time() < deadline and not path.exists():
            time.sleep(POLL_SECONDS)
        return file_name if path.exists() else ""

    def quit(self):
        if not self.alive():
            return False
        try:
            self.call("/quit")
        except (StageFailure, OSError, urllib.error.URLError):
            pass
        try:
            self.process.wait(QUIT_SECONDS)
            return True
        except subprocess.TimeoutExpired:
            return False

    def kill(self):
        if self.alive():
            self.process.kill()
            self.process.wait(10)


def details(state):
    return state.get("details") or {}


def level_names(levels, mode):
    folder = levels / mode
    return sorted(path.name[:-len("_Bricks.txt")] for path in folder.glob("*_Bricks.txt")
                  if (folder / path.name.replace("_Bricks.txt", "_SpawnBall.txt")).exists()
                  and not path.name.startswith(TEST_LEVEL_PREFIX))


class UiPass:
    def __init__(self, game, levels, test_level_folders, report, quick):
        self.game = game
        self.levels = levels
        self.test_level_folders = test_level_folders
        self.report = report
        self.quick = quick
        self.stage = game.frontend

    def check(self, name, action):
        started = time.time()
        try:
            detail, shot = action()
            self.report.add(Check(self.stage, name, True, detail or "", shot or "", time.time() - started))
            return True
        except (StageFailure, OSError, urllib.error.URLError, KeyError, TypeError, ValueError) as failure:
            shot = self.game.screenshot("failed") if self.game.alive() else ""
            self.report.add(Check(self.stage, name, False, str(failure), shot, time.time() - started))
            return False

    def expect_screen(self, screen):
        return self.game.wait_for(lambda state: state.get("screen") == screen, 15, f"the {screen} screen")

    def visit(self, route, screen, shot_name, **parameters):
        def action():
            self.game.call(route, **parameters)
            self.expect_screen(screen)
            time.sleep(0.5)
            return "", self.game.screenshot(shot_name)
        return action

    def boot(self):
        self.game.start()
        state = self.game.state()
        if state.get("screen") == "benchmark":
            self.game.call("/benchmark/skip")
        state = self.game.wait_for(lambda s: s.get("screen") in ("menu", "gameplay"), 30, "the main menu")
        time.sleep(1.0)
        return f"{state.get('renderer')}, backend {state.get('backend')}", self.game.screenshot("menu")

    def play(self, mode, level):
        self.game.call("/play", mode=mode, level=level)
        state = self.game.wait_for(lambda s: s.get("screen") == "gameplay" and details(s).get("level") == level
                                   and ("state" in details(s) or "loadError" in details(s)), LOAD_SECONDS,
                                   f"{level} to load")
        if "loadError" in details(state):
            raise StageFailure(f"load error: {details(state)['loadError']}")
        return state

    def play_normal_to_win(self, level):
        def action():
            state = self.play("normal", level)
            if details(state).get("bricks", 0) <= 0 or details(state).get("launchersLeft", 0) <= 0:
                raise StageFailure(f"nothing to play: {details(state)}")
            shot = self.game.screenshot(f"normal-{level}")
            started = time.time()
            while time.time() - started < WIN_SECONDS:
                state = self.game.state()
                game = details(state)
                if game.get("state") == "won":
                    break
                if game.get("backendFault"):
                    raise StageFailure(f"backend fault: {game['backendFault']}")
                for column, stack in enumerate(game.get("launchers") or []):
                    if stack:
                        try:
                            self.game.call("/launch", column=column, row=0)
                        except StageFailure:
                            pass
                        break
                time.sleep(0.4)
            state = self.game.state()
            game = details(state)
            if game.get("state") != "won":
                raise StageFailure(f"not won after {WIN_SECONDS} s: {game.get('bricks')} bricks, "
                                   f"{game.get('launchersLeft')} launchers, {game.get('balls')} balls left")
            if game.get("bricks") != 0:
                raise StageFailure(f"won with {game.get('bricks')} bricks left")
            time.sleep(0.6)
            win_shot = self.game.screenshot(f"won-{level}")
            return f"won in {game.get('elapsed', 0):.1f} s of game time", win_shot or shot
        return action

    def pause_resume_restart(self, level):
        def action():
            self.play("normal", level)
            self.game.call("/launch", column=0, row=0)
            self.game.wait_for(lambda s: details(s).get("spawned", 0) > 0, 15, "balls to spawn")
            self.game.call("/pause")
            paused = self.game.wait_for(lambda s: details(s).get("paused") is True, 10, "the pause")
            shot = self.game.screenshot("paused")
            ticks_frozen = details(paused).get("elapsed")
            time.sleep(1.0)
            if abs(details(self.game.state()).get("elapsed", 0) - ticks_frozen) > 0.05:
                raise StageFailure("game time moved while paused")
            self.game.call("/resume")
            self.game.wait_for(lambda s: details(s).get("paused") is False, 10, "the resume")
            self.game.call("/restart")
            self.game.wait_for(lambda s: details(s).get("spawned") == 0 and details(s).get("paused") is False, 15,
                               "the restart to clear the board")
            return "", shot
        return action

    def performance(self, level):
        def action():
            self.play("performance", level)
            self.game.call("/start")
            self.game.call("/fill")
            started = time.time()
            worst_fps = None
            worst_physics = None
            peak = 0
            while time.time() - started < FILL_SECONDS:
                state = self.game.state()
                game = details(state)
                if game.get("backendFault"):
                    raise StageFailure(f"backend fault: {game['backendFault']}")
                peak = max(peak, int(game.get("balls", 0)))
                if time.time() - started > 5:
                    worst_fps = state.get("fps") if worst_fps is None else min(worst_fps, state.get("fps", 0))
                    physics = game.get("tickRate", game.get("physicsFps", 0))
                    worst_physics = physics if worst_physics is None else min(worst_physics, physics)
                time.sleep(0.5)
            shot = self.game.screenshot(f"performance-{level}")
            if peak <= 0:
                raise StageFailure("no balls after /start and /fill")
            if worst_fps is not None and worst_fps < MINIMUM_PLAYABLE_FPS:
                raise StageFailure(f"UI dropped to {worst_fps:.1f} FPS with {peak:,} balls")
            return (f"{peak:,} balls, UI at least {worst_fps or 0:.0f} FPS, tick rate at least {worst_physics or 0:.0f} Hz",
                    shot)
        return action

    # The test maps go into every folder the game might read, since a build made in a checkout reads the checkout's
    # data/assets while a copied one reads the assets beside it.
    def write_test_level(self, name, bricks):
        for levels in self.test_level_folders:
            folder = levels / "performance"
            (folder / f"{name}_Bricks.txt").write_text(bricks, encoding="utf-8")
            (folder / f"{name}_SpawnBall.txt").write_text(TWO_RED_BALLS, encoding="utf-8")

    def remove_test_levels(self):
        for levels in self.test_level_folders:
            for path in (levels / "performance").glob(f"{TEST_LEVEL_PREFIX}*"):
                path.unlink()

    def broken_map(self):
        name = TEST_LEVEL_PREFIX + "Broken"

        def action():
            self.write_test_level(name, BROKEN_BRICKS)
            self.game.call("/play", mode="performance", level=name)
            state = self.game.wait_for(lambda s: "loadError" in details(s), LOAD_SECONDS, "the load error")
            error = details(state)["loadError"]
            if f"{name}_Bricks.txt" not in error:
                raise StageFailure(f"the error does not name the file: {error}")
            time.sleep(0.5)
            return error, self.game.screenshot("load-error")
        return action

    def repaired_map(self):
        name = TEST_LEVEL_PREFIX + "Repaired"

        def action():
            self.write_test_level(name, REPAIRED_BRICKS)
            self.play("performance", name)
            state = self.game.wait_for(lambda s: "repaired" in s.get("toast", ""), 10, "the repair warning")
            return state["toast"], self.game.screenshot("repair-warning")
        return action

    def quit(self):
        def action():
            if not self.game.quit():
                raise StageFailure(f"the game did not exit within {QUIT_SECONDS} s of /quit")
            code = self.game.process.returncode
            if code not in (0, None):
                raise StageFailure(f"exit code {code}")
            return "exited cleanly", ""
        return action

    def run(self):
        try:
            if not self.check("boots to the main menu", self.boot):
                return
            self.check("Normal level select", self.visit("/levels", "levels", "levels-normal", mode="normal"))
            self.check("Performance level select",
                       self.visit("/levels", "levels", "levels-performance", mode="performance"))
            self.check("Settings", self.visit("/settings", "settings", "settings", open=1))
            self.check("map editor, new map", self.visit("/editor", "editor", "editor-new", mode="normal"))
            normal = level_names(self.levels, "normal")
            performance = level_names(self.levels, "performance")
            if normal:
                self.check(f"map editor, {normal[0]}",
                           self.visit("/editor", "editor", "editor-level", mode="normal", level=normal[0]))
            self.check("back to the main menu", self.visit("/menu", "menu", "menu-again"))
            if normal:
                self.check("pause, resume and restart", self.pause_resume_restart(normal[0]))
            for level in normal[:1] if self.quick else normal:
                self.check(f"Normal {level}: play to the win", self.play_normal_to_win(level))
            shipped = [level for level in performance if not level.startswith("Edge_")]
            for level in shipped[:1] if self.quick else shipped:
                self.check(f"Performance {level}: start and fill", self.performance(level))
            self.check("a broken map shows the load error", self.broken_map())
            self.check("a repaired map shows the repair warning", self.repaired_map())
            self.check("quits on request", self.quit())
        finally:
            self.remove_test_levels()
            if self.game.alive():
                self.report.add(Check(self.stage, "still running at the end; killed", False))
                self.game.kill()


def frontend_paths(root, frontend):
    checkout_levels = root / "data" / "assets" / "levels"
    if frontend == "sdl":
        dist = root / "dist" / "win32-sdl"
        candidates = [checkout_levels, dist / "assets" / "levels"]
        return dist / "balls.exe", [folder for folder in candidates if (folder / "performance").is_dir()]
    # A packaged Unreal game reads the checkout's data/assets when its build path exists, else the assets staged
    # beside it in Balls/assets.
    candidates = [checkout_levels, root / "dist" / "win32-ue" / "Balls" / "assets" / "levels"]
    return root / "dist" / "win32-ue" / "Balls.exe", [folder for folder in candidates if (folder / "performance").is_dir()]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(REPO_ROOT), help="the checkout whose builds are tested")
    parser.add_argument("--stages", nargs="+", default=["logic", "sdl", "ue"], choices=["logic", "sdl", "ue"])
    parser.add_argument("--out", default=None, help="report folder (default <root>/build/verify-report)")
    parser.add_argument("--quick", action="store_true", help="one Normal and one Performance level per front end")
    parser.add_argument("--prebuilt", action="store_true",
                        help="run the logic stage from build/verify/bin/Release as copied, without building")
    parser.add_argument("--headless", action="store_true",
                        help="no visible window (SDL hidden, Unreal offscreen): for a session without a desktop, such as SSH")
    arguments = parser.parse_args()
    # Not resolved: on a subst drive that maps the build's own path, resolving would name the backing folder instead.
    root = Path(arguments.root).absolute()
    out = Path(arguments.out).resolve() if arguments.out else root / "build" / "verify-report"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    report = Report(out)
    try:
        for stage in arguments.stages:
            if stage == "logic":
                logic_stage(root, out, report, arguments.prebuilt)
                continue
            executable, level_folders = frontend_paths(root, stage)
            if not executable.exists() or not level_folders:
                report.add(Check(stage, "build present", False, f"{executable} or its levels are missing; build it first"))
                continue
            UiPass(Game(stage, executable, out, arguments.headless), level_folders[0], level_folders, report,
                   arguments.quick).run()
    finally:
        report.write()
    failed = report.failed()
    print(f"\n{len(report.checks) - len(failed)} passed, {len(failed)} failed - {out / 'report.html'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

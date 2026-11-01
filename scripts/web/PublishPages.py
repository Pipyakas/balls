
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIRECTORY.parents[1]
TARGETS_FILE = REPO_ROOT / "scripts" / "submission" / "targets.json"
BUILD_FILES = {"balls.html": "index.html", "balls.js": "balls.js", "balls.wasm": "balls.wasm", "balls.data": "balls.data"}
DEFAULT_BRANCH = "gh-pages"
DEFAULT_MESSAGE = "build(web): publish the browser build to GitHub Pages"

def git(*arguments, env=None, check=True):
    result = subprocess.run(["git", *arguments], cwd=REPO_ROOT, capture_output=True, text=True, env=env)
    if check and result.returncode != 0:
        raise SystemExit(result.stderr.strip() or result.stdout.strip())
    return result.stdout.strip()

def destination(name):
    return json.loads(TARGETS_FILE.read_text(encoding="utf-8"))["destinations"][name]

def blob_ids(build_directory):
    blobs = {}
    for source_name, published_name in BUILD_FILES.items():
        path = build_directory / source_name
        if not path.is_file():
            raise SystemExit(f"{path} is missing: build the web target first (see scripts/web/README.md)")
        blobs[published_name] = git("hash-object", "-w", str(path))
    return blobs

def make_commit(blobs, identity, message):
    with tempfile.TemporaryDirectory(prefix="balls-pages-index-") as scratch:
        environment = dict(os.environ, GIT_INDEX_FILE=str(Path(scratch) / "index"))
        git("read-tree", "--empty", env=environment)
        for name, blob in blobs.items():
            git("update-index", "--add", "--cacheinfo", f"100644,{blob},{name}", env=environment)
        tree = git("write-tree", env=environment)
    stamp = subprocess.run(["git", "log", "-1", "--format=%aI", "HEAD"], cwd=REPO_ROOT,
                           capture_output=True, text=True).stdout.strip()
    environment = dict(os.environ,
                       GIT_AUTHOR_NAME=identity["authorName"], GIT_AUTHOR_EMAIL=identity["authorEmail"],
                       GIT_AUTHOR_DATE=stamp,
                       GIT_COMMITTER_NAME=identity["authorName"], GIT_COMMITTER_EMAIL=identity["authorEmail"],
                       GIT_COMMITTER_DATE=stamp)
    return git("commit-tree", tree, "-m", message, env=environment)

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", default=str(REPO_ROOT / "build-web"))
    parser.add_argument("--destination", default="github")
    parser.add_argument("--branch", default=DEFAULT_BRANCH)
    parser.add_argument("--message", default=DEFAULT_MESSAGE)
    parser.add_argument("--dry-run", action="store_true")
    arguments = parser.parse_args()

    build_directory = Path(arguments.build_dir)
    identity = destination(arguments.destination)
    remote_url = identity.get("remoteUrl", "")
    if not remote_url:
        raise SystemExit(f"{arguments.destination}: set remoteUrl in targets.json first")

    blobs = blob_ids(build_directory)
    sizes = {name: (build_directory / source).stat().st_size for source, name in BUILD_FILES.items()}
    for name in sorted(sizes):
        print(f"  {name}: {sizes[name] / (1024 * 1024):.1f} MB")
    commit = make_commit(blobs, identity, arguments.message)

    command = ["git", "push", "--force", remote_url, f"{commit}:refs/heads/{arguments.branch}"]
    if arguments.dry_run:
        command.insert(2, "--dry-run")
    result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True)
    print((result.stdout + result.stderr).strip())
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    print(f"{arguments.destination}: pages branch '{arguments.branch}' -> {remote_url} ({commit[:9]})")

if __name__ == "__main__":
    main()

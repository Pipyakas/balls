import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VULKAN_BACKEND_DIRECTORY = REPO_ROOT / "source" / "backends" / "vulkan"
SOURCE = VULKAN_BACKEND_DIRECTORY / "Compaction.hlsl"
DEFAULT_OUTPUT = VULKAN_BACKEND_DIRECTORY / "generated" / "CompactionBlobs.h"
DEFAULT_DXC = REPO_ROOT / ".deps" / "dxc" / "bin" / "x64" / "dxc.exe"
VULKAN_SHIFTS = ["-fvk-t-shift", "0", "0", "-fvk-s-shift", "128", "0", "-fvk-b-shift", "256", "0",
                 "-fvk-u-shift", "384", "0"]

def compile_shader(dxc, entry, output):
    command = [dxc, "-nologo", "-O3", "-T", "cs_6_0", "-E", entry, "-spirv", "-fspv-target-env=vulkan1.1",
               "-fvk-use-dx-layout", *VULKAN_SHIFTS, "-Fo", output, SOURCE]
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"shader compile failed: {' '.join(str(part) for part in command)}")
    return output.read_bytes()

def format_array(name, data):
    lines = [f"inline constexpr unsigned char {name}[] = {{"]
    for start in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(byte) for byte in data[start:start + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description="Compile the Vulkan ball compaction shaders to SPIR-V")
    parser.add_argument("--dxc", default=str(DEFAULT_DXC))
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    arguments = parser.parse_args()
    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    blocks = ["#pragma once", ""]
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        for entry in ("collectHoles", "fillHoles"):
            blob = compile_shader(Path(arguments.dxc), entry, scratch / f"{entry}.spv")
            blocks.extend((format_array(f"{entry}Spirv", blob), ""))
    output.write_text("\n".join(blocks), encoding="utf-8")

if __name__ == "__main__":
    main()

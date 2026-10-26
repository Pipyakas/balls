import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SHADER_DIRECTORY = REPO_ROOT / "frontends" / "sdl" / "shaders"
DEFAULT_OUTPUT = SHADER_DIRECTORY / "generated" / "ShaderBlobs.h"
DEFAULT_DXC = REPO_ROOT / ".deps" / "dxc" / "bin" / "x64" / "dxc.exe"

SHADERS = [
    ("ballsVertex", "Balls.hlsl", "vertexMain", "vs_6_0", []),
    ("ballsFragment", "Balls.hlsl", "fragmentMain", "ps_6_0", []),
    ("rectsVertex", "Rects.hlsl", "vertexMain", "vs_6_0", []),
    ("rectsFragment", "Rects.hlsl", "fragmentMain", "ps_6_0", []),
    ("visibilityClear", "BallVisibility.hlsl", "computeMain", "cs_6_0", ["CLEAR_PASS"]),
    ("visibilityBid", "BallVisibility.hlsl", "computeMain", "cs_6_0", ["BID_PASS"]),
    ("visibilityResolve", "BallVisibility.hlsl", "computeMain", "cs_6_0", ["RESOLVE_PASS"]),
    ("visibilityCompact", "BallVisibility.hlsl", "computeMain", "cs_6_0", ["COMPACT_PASS"]),
]

TARGETS = [
    ("Spirv", ["-spirv", "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout"]),
    ("Dxil", []),
]


def compile_blob(dxc, source, entry, profile, defines, extra, output):
    command = [str(dxc), "-nologo", "-O3", "-T", profile, "-E", entry, "-Fo", str(output), str(source)]
    for define in defines:
        command += ["-D", define]
    command += extra
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"dxc failed for {source.name} {entry} {defines}")
    return output.read_bytes()


def format_array(name, data):
    lines = [f"inline constexpr unsigned char {name}[] = {{"]
    for start in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(byte) for byte in data[start:start + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Compile the SDL front end's HLSL to SPIR-V and DXIL byte arrays")
    parser.add_argument("--dxc", default=str(DEFAULT_DXC))
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    arguments = parser.parse_args()

    blocks = ["#pragma once", ""]
    with tempfile.TemporaryDirectory() as scratch:
        for name, file_name, entry, profile, defines in SHADERS:
            for suffix, extra in TARGETS:
                blob = compile_blob(Path(arguments.dxc), SHADER_DIRECTORY / file_name, entry, profile, defines, extra,
                                    Path(scratch) / f"{name}{suffix}.bin")
                blocks.append(format_array(f"{name}{suffix}", blob))
                blocks.append("")
    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(blocks), newline="\n")
    print(f"wrote {output}")


if __name__ == "__main__":
    main()

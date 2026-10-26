import argparse
import glob
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent / "BallStep.hlsl"
DEFAULT_OUTPUT = Path(__file__).resolve().parent / "generated" / "BallStepBlobs.h"
DEFAULT_DXC = REPO_ROOT / ".deps" / "dxc" / "bin" / "x64" / "dxc.exe"
PASSES = [("ballStep", None), ("countCells", "COUNT_PASS"), ("scanCells", "SCAN_PASS"),
          ("stepAndSort", "SORT_PASS")]
VULKAN_SHIFTS = ["-fvk-t-shift", "0", "0", "-fvk-s-shift", "128", "0", "-fvk-b-shift", "256", "0",
                 "-fvk-u-shift", "384", "0"]


def find_fxc():
    candidates = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\fxc.exe"))
    if not candidates:
        raise SystemExit("fxc.exe not found in the Windows 10/11 SDK")
    return Path(candidates[-1])


def run(command, output):
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
    parser = argparse.ArgumentParser(description="Compile BallStep.hlsl to DXBC, DXIL and SPIR-V byte arrays")
    parser.add_argument("--dxc", default=str(DEFAULT_DXC))
    parser.add_argument("--fxc", default=None)
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--define", action="append", default=[], help="extra NAME=VALUE for every pass")
    parser.add_argument("--emit-dir", default=None, help="also write raw .dxbc/.dxil/.spv files here")
    arguments = parser.parse_args()
    fxc = Path(arguments.fxc) if arguments.fxc else find_fxc()
    dxc = Path(arguments.dxc)

    blocks = ["#pragma once", ""]
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        for name, define in PASSES:
            defines = ([define] if define else []) + arguments.define
            fxc_define = [part for item in defines for part in ("/D", item)]
            dxc_define = [part for item in defines for part in ("-D", item)]
            dxbc = run([fxc, "/nologo", "/O3", "/T", "cs_5_0", "/E", "computeMain", *fxc_define, "/Fo",
                        scratch / f"{name}.dxbc", SOURCE], scratch / f"{name}.dxbc")
            dxil = run([dxc, "-nologo", "-O3", "-T", "cs_6_0", "-E", "computeMain", *dxc_define, "-Fo",
                        scratch / f"{name}.dxil", SOURCE], scratch / f"{name}.dxil")
            spirv = run([dxc, "-nologo", "-O3", "-T", "cs_6_0", "-E", "computeMain", *dxc_define, "-spirv",
                         "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout", *VULKAN_SHIFTS, "-Fo",
                         scratch / f"{name}.spv", SOURCE], scratch / f"{name}.spv")
            blocks += [format_array(f"{name}Dxbc", dxbc), "", format_array(f"{name}Dxil", dxil), "",
                       format_array(f"{name}Spirv", spirv), ""]
            if arguments.emit_dir:
                emit = Path(arguments.emit_dir)
                emit.mkdir(parents=True, exist_ok=True)
                for suffix, blob in (("dxbc", dxbc), ("dxil", dxil), ("spv", spirv)):
                    (emit / f"{name}.{suffix}").write_bytes(blob)
    if arguments.emit_dir:
        return
    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(blocks), newline="\n")
    print(f"wrote {output}")


if __name__ == "__main__":
    main()

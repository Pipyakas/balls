#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Compile ROCm OpenCL kernels to code objects and C++ header")
    parser.add_argument("--clang", required=True, help="Path to ROCm clang binary")
    parser.add_argument("--dev-libs", required=True, help="Path to AMD device libs bitcode directory")
    parser.add_argument("--source", required=True, help="Path to BallKernels.cl")
    parser.add_argument("--output", required=True, help="Path to output header file")
    parser.add_argument("--targets", default="gfx1030,gfx1031", help="Comma-separated target architectures")
    args = parser.parse_args()

    source_path = Path(args.source).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    targets = [t.strip() for t in args.targets.split(",") if t.strip()]
    blobs = {}

    with tempfile.TemporaryDirectory() as tmpdir:
        for target in targets:
            co_path = Path(tmpdir) / f"BallKernels_{target}.co"
            cmd = [
                args.clang,
                "-x", "cl",
                "-cl-std=CL2.0",
                "-target", "amdgcn-amd-amdhsa",
                f"-mcpu={target}",
                "-O3",
                f"--rocm-device-lib-path={args.dev_libs}",
                "-mcode-object-version=5",
                str(source_path),
                "-o", str(co_path)
            ]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode != 0:
                print(f"Error compiling for {target}:", res.stderr)
                raise SystemExit(res.returncode)
            with open(co_path, "rb") as f:
                blobs[target] = f.read()

    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <span>",
        "#include <string_view>",
        ""
    ]

    for target, data in blobs.items():
        var_name = f"ballKernels_{target}"
        lines.append(f"inline constexpr unsigned char {var_name}[] = {{")
        # Format in rows of 16 bytes
        row = []
        for i, b in enumerate(data):
            row.append(f"0x{b:02x}")
            if len(row) == 16 or i == len(data) - 1:
                lines.append("    " + ", ".join(row) + ",")
                row = []
        lines.append("};")
        lines.append(f"inline constexpr size_t {var_name}_size = sizeof({var_name});")
        lines.append("")

    lines.append("inline std::span<const unsigned char> findRocmKernelImage(std::string_view target) {")
    for target in targets:
        suffix = target[3:] if target.startswith("gfx") else target
        lines.append(f'    if (target.find("{suffix}") != std::string_view::npos) {{')
        lines.append(f'        return ballKernels_{target};')
        lines.append("    }")
    default_target = targets[0]
    lines.append(f"    return ballKernels_{default_target};")
    lines.append("}")
    lines.append("")

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

if __name__ == "__main__":
    main()

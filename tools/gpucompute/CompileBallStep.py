"""Compile BallStep.hlsl into one shader-blob set per hardware profile.

Reads profile rows from core/gpu/BallStepProfiles.def (single source of
truth, see GpuAdapters.cpp) plus any --extra-profile sweep entries, then
compiles the 4 passes (BallStep, CountCells, ScanCells, StepAndSort) to
SPIR-V and DXIL for EVERY profile and to DXBC (cs_5_0 via FXC, D3D11 path)
for the FIRST compiled profile only.

Run with `python -I tools/gpucompute/CompileBallStep.py` from the repo root.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent / "BallStep.hlsl"
DEFAULT_OUTPUT = Path(__file__).resolve().parent / "generated" / "BallStepBlobs.h"
DEFAULT_DXC = REPO_ROOT / ".deps" / "dxc" / "bin" / "x64" / "dxc.exe"
PROFILES_DEF = REPO_ROOT / "core" / "gpu" / "BallStepProfiles.def"

# Passes: (pass_key, base_define). pass_key becomes the <Q> part of the
# array name as BallStep/CountCells/ScanCells/StepAndSort.
PASSES = [
    ("BallStep", None),
    ("CountCells", "COUNT_PASS"),
    ("ScanCells", "SCAN_PASS"),
    ("StepAndSort", "SORT_PASS"),
]

# Bench --blob-dir layout names per pass (see GpuComputeBench.cpp
# loadBlobOverrides): ballStep/countCells/scanCells/stepAndSort + suffix.
BLOB_FILES = {
    "BallStep": "ballStep",
    "CountCells": "countCells",
    "ScanCells": "scanCells",
    "StepAndSort": "stepAndSort",
}

VULKAN_SHIFTS = ["-fvk-t-shift", "0", "0", "-fvk-s-shift", "128", "0", "-fvk-b-shift", "256", "0",
                 "-fvk-u-shift", "384", "0"]

PROFILE_RE = re.compile(r'^\s*BALLS_BALLSTEP_PROFILE\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,(.*)\)\s*$')
NAME_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')


def find_fxc():
    candidates = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\fxc.exe"))
    if not candidates:
        return None
    return Path(candidates[-1])


def discover_dxc(dxc_arg):
    if dxc_arg:
        p = Path(dxc_arg)
        if not p.exists():
            raise SystemExit(f"dxc not found at --dxc path: {dxc_arg}")
        return p
    env_dxc = os.environ.get("DXC")
    if env_dxc:
        p = Path(env_dxc)
        if not p.exists():
            raise SystemExit(f"dxc not found at $DXC path: {env_dxc}")
        return p
    which = shutil.which("dxc")
    if which:
        return Path(which)
    if DEFAULT_DXC.exists():
        return DEFAULT_DXC
    return None


def discover_fxc(fxc_arg):
    if fxc_arg:
        p = Path(fxc_arg)
        if not p.exists():
            raise SystemExit(f"fxc not found at --fxc path: {fxc_arg}")
        return p
    return find_fxc()


def run(command, output_path):
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"shader compile failed: {' '.join(str(part) for part in command)}")
    return output_path.read_bytes()


def format_array(name, data):
    lines = [f"inline constexpr unsigned char {name}[] = {{"]
    for start in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(byte) for byte in data[start:start + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def parse_int_list(tail, what):
    parts = tail.split(",")
    if len(parts) != 6:
        raise SystemExit(f"Malformed profile row (need 6 ints): {what}")
    try:
        return tuple(int(p.strip()) for p in parts)
    except ValueError:
        raise SystemExit(f"Malformed profile row (need 6 ints): {what}")


def parse_profiles_def(extra_profiles):
    profiles = []
    if PROFILES_DEF.exists():
        with PROFILES_DEF.open('r', encoding='utf-8') as f:
            for line in f:
                stripped = line.strip()
                if not stripped or stripped.startswith('//'):
                    continue
                m = PROFILE_RE.match(line)
                if not m:
                    if 'BALLS_BALLSTEP_PROFILE' in line:
                        raise SystemExit(f"Malformed profile row: {line.rstrip()}")
                    continue
                name = m.group(1)
                tail = m.group(2)
                values = parse_int_list(tail, line.rstrip())
                profiles.append((name, values))
    for ep in extra_profiles:
        if '=' not in ep:
            raise SystemExit(f"Invalid --extra-profile format (want name=a,b,c,d,e,f): {ep}")
        name_part, vals_part = ep.split('=', 1)
        if not NAME_RE.match(name_part):
            raise SystemExit(f"Invalid --extra-profile name: {name_part}")
        values = parse_int_list(vals_part, ep)
        # An extra profile with the same name overrides the .def row so
        # sweeps can shadow a checked-in profile without editing the .def.
        profiles = [(n, v) for (n, v) in profiles if n != name_part]
        profiles.append((name_part, values))
    return profiles


def first_def_profile_name():
    if not PROFILES_DEF.exists():
        return None
    with PROFILES_DEF.open('r', encoding='utf-8') as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith('//'):
                continue
            m = PROFILE_RE.match(line)
            if m:
                return m.group(1)
    return None


def main():
    parser = argparse.ArgumentParser(description="Compile BallStep.hlsl to DXBC, DXIL and SPIR-V byte arrays")
    parser.add_argument("--dxc", default=None)
    parser.add_argument("--fxc", default=None)
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--define", action="append", default=[], help="extra NAME=VALUE for every pass")
    parser.add_argument("--emit-dir", default=None, help="also write raw .dxbc/.dxil/.spv files here")
    parser.add_argument("--profiles-only", default=None, help="compile only those rows")
    parser.add_argument("--extra-profile", action="append", default=[], help="ad-hoc profile name=a,b,c,d,e,f")
    arguments = parser.parse_args()

    all_profiles = parse_profiles_def(arguments.extra_profile)
    if not all_profiles:
        raise SystemExit("No profiles found")

    if arguments.profiles_only:
        wanted = set(p.strip() for p in arguments.profiles_only.split(',') if p.strip())
        compiled_profiles = [(n, v) for (n, v) in all_profiles if n in wanted]
        if not compiled_profiles:
            raise SystemExit("No profiles selected by --profiles-only")
        first_def = first_def_profile_name()
        if first_def and first_def not in wanted:
            print(f"Warning: .def-first profile '{first_def}' is excluded; legacy aliases will point to "
                  f"first compiled profile '{compiled_profiles[0][0]}'", file=sys.stderr)
    else:
        compiled_profiles = list(all_profiles)

    first_compiled_name = compiled_profiles[0][0]

    dxc_path = discover_dxc(arguments.dxc)
    if not dxc_path:
        raise SystemExit("dxc not found: pass --dxc or set $DXC or put in PATH or unpack to .deps/dxc/bin/x64/dxc.exe "
                         "(https://github.com/microsoft/DirectXShaderCompiler/releases)")
    fxc_path = discover_fxc(arguments.fxc)

    blocks = []
    blocks.append("#pragma once")
    blocks.append("")
    blocks.append("// Generated by tools/gpucompute/CompileBallStep.py from BallStep.hlsl; do not edit.")
    blocks.append("// One shader-blob set per hardware profile row in core/gpu/BallStepProfiles.def")
    blocks.append("// (plus any --extra-profile sweep entries).")
    blocks.append("//")
    blocks.append("// Naming scheme: ballStep_<profile>_<Pass><Api> where <profile> is the verbatim")
    blocks.append("// .def row name (e.g. wave32), <Pass> is one of BallStep, CountCells, ScanCells,")
    blocks.append("// StepAndSort, and <Api> is one of Spirv, Dxil, Dxbc, e.g.")
    blocks.append("// ballStep_wave32_BallStepSpirv, ballStep_wave32_CountCellsDxil.")
    blocks.append("// Formable via token-paste as ballStep_##name##_BallStepSpirv etc.")
    blocks.append("// (see tools/gpucompute/BallStepVariants.cpp).")
    blocks.append("// SPIR-V and DXIL are compiled for EVERY profile; DXBC (cs_5_0 via FXC,")
    blocks.append("// for the D3D11 path) is compiled ONLY for the FIRST compiled profile.")
    blocks.append("// The 12 legacy aliases below (ballStep/countCells/scanCells/stepAndSort x")
    blocks.append("// Spirv/Dxil/Dxbc) alias the FIRST COMPILED profile's arrays.")
    blocks.append("//")
    blocks.append("// Profile table (name: threadsPerGroup, ballsPerThread, countThreads, countItems,")
    blocks.append("// sortThreads, sortItems):")
    for pname, (tpg, bpt, ct, ci, st, si) in compiled_profiles:
        blocks.append(f"//   {pname}: {tpg}, {bpt}, {ct}, {ci}, {st}, {si}")
    blocks.append("")

    # blobs[(profile, pass)] = {"spirv": bytes, "dxil": bytes, "dxbc": bytes|None}
    blobs = {}
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        for prof_idx, (prof_name, (tpg, bpt, ct, ci, st, si)) in enumerate(compiled_profiles):
            profile_values = [
                f"THREADS_PER_GROUP={tpg}",
                f"BALLS_PER_THREAD={bpt}",
                f"COUNT_THREADS={ct}",
                f"COUNT_ITEMS={ci}",
                f"SORT_THREADS={st}",
                f"SORT_ITEMS={si}",
            ]
            for pass_name, pass_define in PASSES:
                # Per (profile, pass): base define + user --define entries + profile defines.
                dxc_defs = []
                fxc_defs = []
                if pass_define:
                    dxc_defs += ["-D", pass_define]
                    fxc_defs += ["/D", pass_define]
                for item in arguments.define:
                    dxc_defs += ["-D", item]
                    fxc_defs += ["/D", item]
                for pv in profile_values:
                    dxc_defs += ["-D", pv]
                    fxc_defs += ["/D", pv]

                common_dxc = ["-nologo", "-O3", "-T", "cs_6_0", "-E", "computeMain"] + dxc_defs
                common_fxc = ["/nologo", "/O3", "/T", "cs_5_0", "/E", "computeMain"] + fxc_defs

                # SPIR-V for every profile.
                spirv_out = scratch / f"{prof_name}_{pass_name}.spv"
                dxc_spirv_cmd = ([str(dxc_path)] + common_dxc
                                 + ["-spirv", "-fspv-target-env=vulkan1.1",
                                    "-fvk-use-dx-layout", *VULKAN_SHIFTS,
                                    "-Fo", str(spirv_out), str(SOURCE)])
                spirv_blob = run(dxc_spirv_cmd, spirv_out)

                # DXIL for every profile.
                dxil_out = scratch / f"{prof_name}_{pass_name}.dxil"
                dxc_dxil_cmd = [str(dxc_path)] + common_dxc + ["-Fo", str(dxil_out), str(SOURCE)]
                dxil_blob = run(dxc_dxil_cmd, dxil_out)

                entry = {"spirv": spirv_blob, "dxil": dxil_blob, "dxbc": None}

                # DXBC only for the FIRST compiled profile.
                if prof_idx == 0:
                    if fxc_path is None:
                        print("Warning: FXC not found, skipping DXBC generation "
                              "(D3D11 path is Windows-only)", file=sys.stderr)
                    else:
                        dxbc_out = scratch / f"{prof_name}_{pass_name}.dxbc"
                        fxc_dxbc_cmd = ([str(fxc_path)] + common_fxc
                                        + ["/Fo", str(dxbc_out), str(SOURCE)])
                        entry["dxbc"] = run(fxc_dxbc_cmd, dxbc_out)

                blobs[(prof_name, pass_name)] = entry

        for prof_name, _values in compiled_profiles:
            for pass_name, _pass_define in PASSES:
                entry = blobs[(prof_name, pass_name)]
                blocks.append(format_array(f"ballStep_{prof_name}_{pass_name}Spirv", entry["spirv"]))
                blocks.append("")
                blocks.append(format_array(f"ballStep_{prof_name}_{pass_name}Dxil", entry["dxil"]))
                blocks.append("")
                if entry["dxbc"] is not None:
                    blocks.append(format_array(f"ballStep_{prof_name}_{pass_name}Dxbc", entry["dxbc"]))
                    blocks.append("")

    # Legacy aliases point at the first COMPILED profile (derived, never hardcoded).
    blocks.append("// Legacy aliases (point to first compiled profile)")
    legacy_map = [
        ("ballStepSpirv", f"ballStep_{first_compiled_name}_BallStepSpirv"),
        ("ballStepDxil", f"ballStep_{first_compiled_name}_BallStepDxil"),
        ("ballStepDxbc", f"ballStep_{first_compiled_name}_BallStepDxbc"),
        ("countCellsSpirv", f"ballStep_{first_compiled_name}_CountCellsSpirv"),
        ("countCellsDxil", f"ballStep_{first_compiled_name}_CountCellsDxil"),
        ("countCellsDxbc", f"ballStep_{first_compiled_name}_CountCellsDxbc"),
        ("scanCellsSpirv", f"ballStep_{first_compiled_name}_ScanCellsSpirv"),
        ("scanCellsDxil", f"ballStep_{first_compiled_name}_ScanCellsDxil"),
        ("scanCellsDxbc", f"ballStep_{first_compiled_name}_ScanCellsDxbc"),
        ("stepAndSortSpirv", f"ballStep_{first_compiled_name}_StepAndSortSpirv"),
        ("stepAndSortDxil", f"ballStep_{first_compiled_name}_StepAndSortDxil"),
        ("stepAndSortDxbc", f"ballStep_{first_compiled_name}_StepAndSortDxbc"),
    ]
    for old, new in legacy_map:
        blocks.append(f"inline constexpr auto& {old} = {new};")
    blocks.append("")

    # Raw files for bench --blob-dir / sweep consumers.
    if arguments.emit_dir:
        emit_root = Path(arguments.emit_dir)
        emit_root.mkdir(parents=True, exist_ok=True)
        if len(compiled_profiles) == 1:
            prof_name = compiled_profiles[0][0]
            for pass_name, _pass_define in PASSES:
                entry = blobs[(prof_name, pass_name)]
                stem = BLOB_FILES[pass_name]
                (emit_root / f"{stem}.spv").write_bytes(entry["spirv"])
                (emit_root / f"{stem}.dxil").write_bytes(entry["dxil"])
                if entry["dxbc"] is not None:
                    (emit_root / f"{stem}.dxbc").write_bytes(entry["dxbc"])
        else:
            for prof_name, _values in compiled_profiles:
                prof_dir = emit_root / prof_name
                prof_dir.mkdir(parents=True, exist_ok=True)
                for pass_name, _pass_define in PASSES:
                    entry = blobs[(prof_name, pass_name)]
                    stem = BLOB_FILES[pass_name]
                    (prof_dir / f"{stem}.spv").write_bytes(entry["spirv"])
                    (prof_dir / f"{stem}.dxil").write_bytes(entry["dxil"])
                    if entry["dxbc"] is not None:
                        (prof_dir / f"{stem}.dxbc").write_bytes(entry["dxbc"])

    # The --output header is ALWAYS written, even with --emit-dir.
    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(blocks), newline="\n")
    print(f"wrote {output}")


if __name__ == "__main__":
    main()

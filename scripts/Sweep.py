#!/usr/bin/env python3
"""Sweep.py - Parameter sweep harness for balls_gpucompute compute shaders.

Automates exploring tuning parameters (threadsPerGroup, ballsPerThread, countThreads,
countItems, sortThreads, sortItems) across hardware capabilities, generating per-candidate
shaders with CompileBallStep.py, benchmarking each with balls_gpucompute, and ranking results.

Stdlib-only, safe for execution with `python -I`.
"""

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import List, Optional, Tuple

# Candidate profiles. The first block spans the space coarsely; the second refines around the first sweep's
# winner on a GCN GPU (few balls per thread, small groups). sortThreads * sortItems must stay <= 4096 because the
# sort pass packs a ball's rank into RANK_BITS = 12 bits (BallStep.hlsl); larger products corrupt the sort.
# Tuples: (threadsPerGroup, ballsPerThread, countThreads, countItems, sortThreads, sortItems)
CANDIDATES: List[Tuple[int, int, int, int, int, int]] = [
    (128, 4, 256, 16, 512, 8),
    (512, 4, 256, 16, 512, 8),
    (256, 2, 256, 16, 512, 8),
    (256, 8, 256, 16, 512, 8),
    (256, 1, 256, 16, 512, 8),
    (128, 2, 256, 16, 256, 8),
    (256, 4, 256, 16, 256, 8),
    (256, 4, 256, 16, 512, 4),
    (128, 8, 256, 16, 256, 16),
    (512, 8, 256, 16, 1024, 4),
    (128, 1, 128, 16, 256, 4),
    (64, 1, 128, 16, 256, 4),
    (64, 1, 64, 16, 128, 4),
    (128, 1, 128, 32, 256, 4),
    (128, 1, 128, 8, 256, 4),
    (128, 1, 64, 16, 256, 4),
    (128, 1, 256, 16, 256, 4),
    (128, 1, 128, 16, 128, 8),
    (128, 1, 128, 16, 512, 4),
    (128, 1, 128, 16, 256, 8),
    (128, 1, 128, 16, 256, 16),
    (256, 1, 128, 16, 256, 4),
    (128, 2, 128, 16, 256, 4),
    (64, 2, 128, 16, 256, 4),
]
MAX_SORT_RANK = 4096
DEFAULT_ROW = "default"


@dataclass
class AdapterInfo:
    index: int
    name: str
    kind: str  # "discrete", "integrated", "software", "virtual", "unknown"
    device_local_mb: int
    max_workgroup: int
    shared_bytes: int
    subgroup_max: int
    raw_line: str


@dataclass
class CandidateResult:
    name: str
    params: Optional[Tuple[int, int, int, int, int, int]]
    kernel_ms: Optional[float]
    verdict: str  # "conserved", "MISMATCH", "FAILED"
    detail: str = ""
    low: float = 0.0
    high: float = 0.0


def parse_adapter_line(line: str, fallback_index: int) -> Optional[AdapterInfo]:
    stripped = line.strip()
    if not stripped:
        return None

    # Adapter index at line start, e.g. "0: ..." or "[0] ..."
    m_idx = re.match(r"^\s*(?:\[|#)?(\d+)(?:\]|:|\b)", stripped)
    index = int(m_idx.group(1)) if m_idx else fallback_index

    lower = stripped.lower()
    if "discrete" in lower:
        kind = "discrete"
    elif "integrated" in lower:
        kind = "integrated"
    elif "software" in lower:
        kind = "software"
    elif "virtual" in lower:
        kind = "virtual"
    else:
        kind = "unknown"

    # Device-local memory in MB (e.g. "... (discrete, wave 32, 8192 MB)" or "vram=8192 MB")
    m_mb = re.search(r"(\d+)\s*MB", stripped, re.IGNORECASE)
    if not m_mb:
        m_mb = re.search(r"(?:device-local|vram|memory)[=:\s]*(\d+)", stripped, re.IGNORECASE)
    device_local_mb = int(m_mb.group(1)) if m_mb else 0

    # Max workgroup invocations, e.g. "max workgroup invocations 1024" or "maxWorkgroup=1024"
    m_wg = re.search(
        r"(?:max\s*workgroup(?:\s*invocations)?|maxworkgroup|workgroup)[=:\s]+(\d+)",
        stripped,
        re.IGNORECASE,
    )
    max_workgroup = int(m_wg.group(1)) if m_wg else 1024

    # Group shared memory, e.g. "shared memory 65536" or "shared=32768"
    m_sh = re.search(
        r"(?:(?:max\s*)?shared(?:\s*memory)?(?:bytes)?)[=:\s]+(\d+)",
        stripped,
        re.IGNORECASE,
    )
    shared_bytes = int(m_sh.group(1)) if m_sh else 32768

    # Wave/subgroup size max, e.g. "subgroup=32-32", "subgroup=32", "wave 32"
    m_sg = re.search(r"subgroup[=:\s]+(?:\d+\s*-\s*)?(\d+)", stripped, re.IGNORECASE)
    if not m_sg:
        m_sg = re.search(r"wave\s+(\d+)", stripped, re.IGNORECASE)
    subgroup_max = int(m_sg.group(1)) if m_sg else 32

    return AdapterInfo(
        index=index,
        name=stripped,
        kind=kind,
        device_local_mb=device_local_mb,
        max_workgroup=max_workgroup,
        shared_bytes=shared_bytes,
        subgroup_max=subgroup_max,
        raw_line=stripped,
    )


def select_adapter(adapters: List[AdapterInfo], preference: str) -> Optional[AdapterInfo]:
    """Select adapter matching preference string, mirroring source/core/gpu/GpuAdapters.cpp:
    - Decimal index: match adapter index or position.
    - 'discrete': discrete adapter with the largest device-local memory.
    - 'integrated': first integrated adapter.
    - 'auto' / '': discrete with largest device-local memory, else integrated, else first.
    - Substring: case-insensitive substring match against adapter line.
    """
    if not adapters:
        return None
    pref = preference.strip().lower()

    if pref.isdigit():
        target_idx = int(pref)
        for a in adapters:
            if a.index == target_idx:
                return a
        if 0 <= target_idx < len(adapters):
            return adapters[target_idx]
        return None

    if pref in ("", "auto"):
        discretes = [a for a in adapters if a.kind == "discrete"]
        if discretes:
            return max(discretes, key=lambda a: a.device_local_mb)
        integrateds = [a for a in adapters if a.kind == "integrated"]
        if integrateds:
            return integrateds[0]
        return adapters[0]

    if pref == "discrete":
        discretes = [a for a in adapters if a.kind == "discrete"]
        return max(discretes, key=lambda a: a.device_local_mb) if discretes else None

    if pref == "integrated":
        integrateds = [a for a in adapters if a.kind == "integrated"]
        return integrateds[0] if integrateds else None

    for a in adapters:
        if pref in a.raw_line.lower():
            return a
    return None


def inspect_hardware_limits(bench_path: str, gpu_pref: str) -> Tuple[int, int, int]:
    """Runs `<bench> --list-gpus` and extracts (maxWorkgroup, sharedBytes, subgroupMax)
    for the selected adapter. Falls back to safe defaults (1024, 32768, 32) on failure.
    """
    default_limits = (1024, 32768, 32)
    try:
        proc = subprocess.run(
            [bench_path, "--list-gpus"],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0 or not proc.stdout.strip():
            return default_limits

        adapters: List[AdapterInfo] = []
        for i, line in enumerate(proc.stdout.splitlines()):
            info = parse_adapter_line(line, fallback_index=i)
            if info:
                adapters.append(info)

        selected = select_adapter(adapters, gpu_pref)
        if selected:
            return (
                selected.max_workgroup or default_limits[0],
                selected.shared_bytes or default_limits[1],
                selected.subgroup_max or default_limits[2],
            )
    except Exception:
        pass

    return default_limits


def filter_candidates(
    candidates: List[Tuple[int, int, int, int, int, int]],
    max_workgroup: int,
    shared_bytes: int,
    max_count: int,
) -> List[Tuple[int, int, int, int, int, int]]:
    """Filters candidate parameter tuples against the limits BallStep.hlsl actually needs:
    - every pass's thread count <= maxWorkgroup
    - count/scan passes use sort-cell tables only (MAX_SORT_CELLS words)
    - the fused sort pass uses MAX_SORT_CELLS words plus sortThreads * sortItems words
    The unsorted step pass uses no group-shared memory.
    Caps result at max_count.
    """
    max_sort_cells = 4096
    word_bytes = 4
    valid: List[Tuple[int, int, int, int, int, int]] = []
    for cand in candidates:
        tpg, bpt, ct, ci, st, si = cand
        if max(tpg, ct, st) > max_workgroup:
            continue
        if st * si > MAX_SORT_RANK:
            continue
        if max_sort_cells * word_bytes > shared_bytes:
            continue
        if (max_sort_cells + st * si) * word_bytes > shared_bytes:
            continue
        valid.append(cand)
        if len(valid) >= max_count:
            break
    return valid


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Parameter sweep harness for balls_gpucompute compute shaders",
    )
    parser.add_argument(
        "--bench",
        required=True,
        help="Path to balls_gpucompute binary",
    )
    parser.add_argument(
        "--api",
        default="vulkan",
        help="Target graphics/compute API (default: vulkan)",
    )
    parser.add_argument(
        "--gpu",
        default="auto",
        help="Target GPU adapter (default: auto)",
    )
    parser.add_argument(
        "--balls",
        type=int,
        default=1000000,
        help="Number of balls to simulate (default: 1000000)",
    )
    parser.add_argument(
        "--ticks",
        type=int,
        default=100,
        help="Number of simulation ticks to run (default: 100)",
    )
    parser.add_argument(
        "--dir",
        default="assets/levels/performance",
        help="Directory containing benchmark levels (default: assets/levels/performance)",
    )
    parser.add_argument(
        "--level",
        default="DA_1000Bricks",
        help="Level name (default: DA_1000Bricks)",
    )
    parser.add_argument(
        "--substeps",
        type=int,
        default=2,
        help="Substeps per tick (default: 2)",
    )
    parser.add_argument(
        "--sort-interval",
        default="auto",
        help="Sorting interval in ticks or 'auto' (default: auto)",
    )
    parser.add_argument(
        "--dxc",
        default=None,
        help="Passthrough path to DXC compiler",
    )
    parser.add_argument(
        "--fxc",
        default=None,
        help="Passthrough path to FXC compiler",
    )
    parser.add_argument(
        "--keep-dir",
        action="store_true",
        help="Retain temporary compilation scratch directory",
    )
    parser.add_argument(
        "--blob-cache",
        default=None,
        help="Directory of compiled candidate blobs, reused when present and filled otherwise. Compile on a machine "
             "with DXC, copy the directory to one without (e.g. Linux), and the sweep runs there without compiling.",
    )
    parser.add_argument(
        "--compile-only",
        action="store_true",
        help="With --blob-cache: compile every candidate that passes this adapter's limits, then exit",
    )
    parser.add_argument(
        "--rounds",
        type=int,
        default=3,
        help="Interleaved benchmark rounds per candidate; the median is ranked (default: 3)",
    )
    parser.add_argument(
        "--max-candidates",
        type=int,
        default=24,
        help="Maximum candidate configurations to evaluate (default: 24)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_arguments()
    bench_path = Path(args.bench).resolve()
    compile_script = Path(__file__).resolve().parent / "CompileBallStep.py"

    max_wg, shared_mem, subgroup_max = inspect_hardware_limits(str(bench_path), args.gpu)
    candidates = filter_candidates(CANDIDATES, max_wg, shared_mem, args.max_candidates)

    if not candidates:
        sys.stderr.write("No candidate configurations satisfied hardware constraints.\n")
        sys.exit(1)

    scratch_dir = Path(tempfile.mkdtemp(prefix="ballsweep"))
    blob_cache = Path(args.blob_cache).resolve() if args.blob_cache else None
    results: List[CandidateResult] = []
    compiled: list = []

    try:
        for cand in candidates:
            tpg, bpt, ct, ci, st, si = cand
            numbers = f"{tpg},{bpt},{ct},{ci},{st},{si}"
            # Named by numbers so a blob cache built on one machine matches the candidates another selects.
            sweep_name = "s_" + numbers.replace(",", "_")
            cand_emit_dir = (blob_cache or scratch_dir) / sweep_name
            if blob_cache and (cand_emit_dir / "stepAndSort.spv").exists() and (cand_emit_dir / "stepAndSort.dxil").exists():
                compiled.append((sweep_name, cand, cand_emit_dir))
                continue
            cand_header = scratch_dir / f"{sweep_name}.h"
            cand_emit_dir.mkdir(parents=True, exist_ok=True)

            # 1. Compile per-profile shaders with CompileBallStep.py
            compile_cmd = [
                sys.executable,
                "-I",
                str(compile_script),
                "--extra-profile",
                f"{sweep_name}={numbers}",
                "--profiles-only",
                sweep_name,
                "--emit-dir",
                str(cand_emit_dir),
                "--output",
                str(cand_header),
            ]
            if args.dxc:
                compile_cmd.extend(["--dxc", str(args.dxc)])
            if args.fxc:
                compile_cmd.extend(["--fxc", str(args.fxc)])

            compile_res = subprocess.run(compile_cmd, capture_output=True, text=True)
            if compile_res.returncode != 0:
                err_text = (compile_res.stderr.strip() or compile_res.stdout.strip() or "compile failed").splitlines()[-1]
                results.append(
                    CandidateResult(
                        name=sweep_name,
                        params=cand,
                        kernel_ms=None,
                        verdict="FAILED",
                        detail=f"compile error: {err_text}",
                    )
                )
                continue

            compiled.append((sweep_name, cand, cand_emit_dir))

        if args.compile_only:
            print(f"compiled {len(compiled)} of {len(candidates)} candidates into {blob_cache}")
            sys.exit(0 if len(compiled) == len(candidates) else 1)

        # 2. Benchmark in interleaved rounds so clock drift and thermal state hit every candidate equally,
        # then rank each candidate by the median of its rounds.
        # The built-in profile for this adapter runs inside the same rounds as the reference row.
        runs = [(DEFAULT_ROW, None, None)] + compiled
        samples: dict = {name: [] for name, _, _ in runs}
        verdicts: dict = {name: set() for name, _, _ in runs}
        failures: dict = {}
        for _round in range(args.rounds):
            for sweep_name, cand, cand_emit_dir in runs:
                if sweep_name in failures:
                    continue
                override = ["--blob-dir", str(cand_emit_dir), "--profile-numbers", ",".join(map(str, cand))] if cand else []
                bench_cmd = [
                    str(bench_path),
                    "--api", str(args.api),
                    "--gpu", str(args.gpu),
                    *override,
                    "--balls", str(args.balls),
                    "--ticks", str(args.ticks),
                    "--dir", str(args.dir),
                    "--level", str(args.level),
                    "--substeps", str(args.substeps),
                    "--sort-interval", str(args.sort_interval),
                ]
                bench_res = subprocess.run(bench_cmd, capture_output=True, text=True)
                bench_output = bench_res.stdout + "\n" + bench_res.stderr
                m_ms = re.search(r"kernel\s+([\d.]+)\s*ms", bench_output, re.IGNORECASE)
                m_verdict = re.search(r"\b(conserved|MISMATCH)\b", bench_output)
                if bench_res.returncode != 0 or not m_ms:
                    failures[sweep_name] = (bench_res.stderr.strip() or bench_res.stdout.strip()
                                            or f"exit code {bench_res.returncode}").splitlines()[-1]
                    continue
                samples[sweep_name].append(float(m_ms.group(1)))
                verdicts[sweep_name].add(m_verdict.group(1) if m_verdict else "UNKNOWN")

        for sweep_name, cand, _ in runs:
            if sweep_name in failures:
                results.append(CandidateResult(name=sweep_name, params=cand, kernel_ms=None, verdict="FAILED",
                                               detail=f"bench error: {failures[sweep_name]}"))
                continue
            ordered = sorted(samples[sweep_name])
            median = ordered[len(ordered) // 2]
            verdict = "MISMATCH" if "MISMATCH" in verdicts[sweep_name] else (
                "conserved" if verdicts[sweep_name] == {"conserved"} else "UNKNOWN")
            results.append(CandidateResult(name=sweep_name, params=cand, kernel_ms=median, verdict=verdict,
                                           detail="", low=ordered[0], high=ordered[-1]))
    finally:
        if args.keep_dir:
            print(f"Retained sweep directory: {scratch_dir}")
        else:
            shutil.rmtree(scratch_dir, ignore_errors=True)

    # 3. Print ranked table; the built-in profile is the reference row.
    def describe(result: CandidateResult) -> str:
        return "built-in profile" if result.params is None else ",".join(str(value) for value in result.params)

    reference = next((r for r in results if r.name == DEFAULT_ROW and r.kernel_ms is not None), None)
    timed_results = sorted((r for r in results if r.kernel_ms is not None), key=lambda r: r.kernel_ms)
    failed_results = [r for r in results if r.kernel_ms is None]

    print("\nRanked Sweep Results (median of interleaved rounds):")
    print(f"{'Rank':>4}  {'Profile':<8}  {'Parameters (tpg,bpt,ct,ci,st,si)':<34}  {'Median ms':>9}  {'Min':>7}  {'Max':>7}  "
          f"{'vs default':>10}  Verdict")
    print("-" * 105)
    for rank, r in enumerate(timed_results, start=1):
        delta = f"{(r.kernel_ms / reference.kernel_ms - 1.0) * 100.0:+.1f}%" if reference else "-"
        note = " (excluded from best)" if r.verdict != "conserved" else ""
        print(f"{rank:4d}  {r.name:<8}  {describe(r):<34}  {r.kernel_ms:9.3f}  {r.low:7.3f}  {r.high:7.3f}  "
              f"{delta:>10}  {r.verdict}{note}")
    for r in failed_results:
        fail_desc = f"FAILED ({r.detail})" if r.detail else "FAILED"
        print(f"{'-':>4}  {r.name:<8}  {describe(r):<34}  {'-':>9}  {'-':>7}  {'-':>7}  {'-':>10}  {fail_desc}")
    print("-" * 105)

    # 4. BEST must be conserved and must beat the default by more than twice the default's own min-max spread,
    # otherwise the difference is indistinguishable from clock and thermal noise.
    conserved = [r for r in timed_results if r.verdict == "conserved" and r.name != DEFAULT_ROW]
    if conserved:
        best = conserved[0]
        best_params = ",".join(str(value) for value in best.params)
        print(f"BEST {best.name} {best_params} {best.kernel_ms:.3f}")
        if reference:
            gap = reference.kernel_ms - best.kernel_ms
            noise = 2.0 * (reference.high - reference.low)
            real = gap > noise
            print(f"DEFAULT {reference.kernel_ms:.3f} ms (spread {reference.high - reference.low:.3f}); "
                  f"gap {gap:.3f} ms, {gap / reference.kernel_ms * 100.0:.1f}% -> {'REAL' if real else 'NOISE'}"
                  f" (needs gap > {noise:.3f})")
    else:
        print("BEST NONE - no candidate passed validation with conserved verdict")

    if results and all(r.verdict == "FAILED" for r in results):
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()

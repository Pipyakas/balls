import argparse
import os
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
KERNEL = REPO_ROOT / "source" / "backends" / "cuda" / "BallKernels.cu"
ANCHOR = "    const int brick = stepBallSubsteps("
PERMUTE_SELECTORS = ("0x3120", "0x2301", "0x1032", "0x0213")
INDEPENDENT_CHAINS = 4

def injected_source(base, count):
    if count == 0:
        return base
    lines = ["    unsigned int pad0 = target, pad1 = target ^ 1u, pad2 = target ^ 2u, pad3 = target ^ 3u;"]
    for step in range(count // INDEPENDENT_CHAINS):
        lines.append('    asm volatile("mad.lo.u32 %0, %0, %0, %1;" : "+r"(pad0) : "r"(target));')
        lines.append('    asm volatile("mad.lo.u32 %0, %0, %0, %1;" : "+r"(pad1) : "r"(target));')
        selector = PERMUTE_SELECTORS[step % len(PERMUTE_SELECTORS)]
        lines.append(f'    asm volatile("prmt.b32 %0, %0, %1, {selector};" : "+r"(pad2) : "r"(target));')
        lines.append(f'    asm volatile("prmt.b32 %0, %0, %1, {selector};" : "+r"(pad3) : "r"(target));')
    lines.append("    if ((pad0 ^ pad1 ^ pad2 ^ pad3) == 0x7fffffffu) { packedOut[0] = pad0; }")
    return base.replace(ANCHOR, "\n".join(lines) + "\n" + ANCHOR, 1)

def kernel_milliseconds(bench, bench_arguments, runs):
    best = float("inf")
    for _ in range(runs):
        output = subprocess.run([str(bench), "--roofline", *bench_arguments], capture_output=True, text=True,
                                check=True).stdout
        best = min(best, float(output.strip().splitlines()[-1].split("kernel")[1].split("ms")[0]))
    return best

def main():
    parser = argparse.ArgumentParser(
        description="Inject K independent ALU instructions per thread into stepBallsKernel and report the marginal "
                    "kernel time per instruction against one full SM issue slot. ~100%% means no spare issue "
                    "cycles (issue-bound); well below means the kernel waits on memory or latency.")
    parser.add_argument("--build", default=str(REPO_ROOT / "build" / "win32"))
    parser.add_argument("--counts", default="0,32,64,128")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--balls", type=int, default=60_000_000)
    parser.add_argument("--ticks", type=int, default=60)
    parser.add_argument("--sm-clock-mhz", type=float, required=True,
                        help="SM clock while the bench runs (nvidia-smi -q -d CLOCK)")
    parser.add_argument("--multiprocessors", type=int, default=14)
    parser.add_argument("--on-level", action="store_true")
    arguments = parser.parse_args()

    bench = Path(arguments.build) / "bin" / "Release" / ("balls_bench.exe" if os.name == "nt" else "balls_bench")
    bench_arguments = ["--balls", str(arguments.balls), "--ticks", str(arguments.ticks)]
    if arguments.on_level:
        bench_arguments.append("--on-level")
    issue_slots_per_ms = arguments.multiprocessors * 4 * arguments.sm_clock_mhz * 1e3
    warps = arguments.balls / 32
    full_cost_ms = warps / issue_slots_per_ms

    base = KERNEL.read_text()
    baseline = None
    try:
        for count in (int(value) for value in arguments.counts.split(",")):
            KERNEL.write_text(injected_source(base, count), newline="\n")
            subprocess.run(["cmake", "--build", arguments.build, "--config", "Release", "--target",
                            "balls_physics_cuda", "--parallel"], check=True, capture_output=True)
            milliseconds = kernel_milliseconds(bench, bench_arguments, arguments.runs)
            if count == 0:
                baseline = milliseconds
                print(f"inject={count:4d}  kernel {milliseconds:7.3f} ms", flush=True)
                continue
            share = (milliseconds - baseline) / (count * full_cost_ms) * 100.0
            print(f"inject={count:4d}  kernel {milliseconds:7.3f} ms  marginal cost {share:5.1f}% of an issue slot",
                  flush=True)
    finally:
        KERNEL.write_text(base, newline="\n")
        subprocess.run(["cmake", "--build", arguments.build, "--config", "Release", "--target", "balls_physics_cuda",
                        "--parallel"], capture_output=True)

if __name__ == "__main__":
    main()

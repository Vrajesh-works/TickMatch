"""Plot latency histograms written by tickmatch_replay --hist-csv.

Usage:
    python -m tickmatch_client.plot_latency out/latency_pipeline.csv --out out/latency.png
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Bucket:
    lower: int
    upper: int
    count: int


def read_histogram(path: Path) -> list[Bucket]:
    with path.open(newline="") as fh:
        rows = csv.DictReader(fh)
        return [Bucket(int(r["lower_ns"]), int(r["upper_ns"]), int(r["count"])) for r in rows]


def percentile(buckets: list[Bucket], p: float) -> int:
    """Same rule as the C++ histogram: upper bound of the bucket holding rank ceil(p% * n)."""
    total = sum(b.count for b in buckets)
    if total == 0:
        return 0
    rank = max(1, math.ceil(p / 100.0 * total))
    seen = 0
    for b in buckets:
        seen += b.count
        if seen >= rank:
            return b.upper
    return buckets[-1].upper


def plot(paths: list[Path], out: Path, title: str) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(9, 4.5))
    for path in paths:
        buckets = read_histogram(path)
        total = sum(b.count for b in buckets) or 1
        xs = [max(1, b.lower) for b in buckets]
        widths = [max(1, b.upper - b.lower + 1) for b in buckets]
        ys = [100.0 * b.count / total for b in buckets]
        p50, p99 = percentile(buckets, 50), percentile(buckets, 99)
        bars = ax.bar(xs, ys, width=widths, align="edge", alpha=0.6, label=f"{path.stem} (p50 {p50} ns, p99 {p99} ns)")
        color = bars.patches[0].get_facecolor() if bars.patches else None
        ax.axvline(p50, linestyle="--", linewidth=1, color=color)
        ax.axvline(p99, linestyle=":", linewidth=1, color=color)
    ax.set_xscale("log")
    ax.set_xlabel("latency (ns, log scale)")
    ax.set_ylabel("share of operations (%)")
    ax.set_title(title)
    ax.legend()
    fig.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=120)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="+")
    parser.add_argument("--out", type=Path, default=Path("out/latency.png"))
    parser.add_argument("--title", default="TickMatch latency")
    args = parser.parse_args(argv)
    plot(args.csv, args.out, args.title)
    for path in args.csv:
        b = read_histogram(path)
        print(f"{path}: p50={percentile(b, 50)}ns p99={percentile(b, 99)}ns p99.9={percentile(b, 99.9)}ns")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

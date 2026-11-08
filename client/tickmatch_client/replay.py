"""Drive the tickmatch_replay binary and print its summary.

Usage:
    python -m tickmatch_client.replay --binary build/release/apps/tickmatch_replay \
        --input data/sample/synthetic_messages.csv --out-dir out
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_BINARIES = (
    Path("build/release/apps/tickmatch_replay"),
    Path("build/apps/tickmatch_replay"),
)


def find_binary(explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit
    for candidate in DEFAULT_BINARIES:
        if candidate.exists():
            return candidate
    found = shutil.which("tickmatch_replay")
    if found:
        return Path(found)
    raise FileNotFoundError("tickmatch_replay not found; build it or pass --binary")


def run_replay(
    binary: Path, input_path: Path, out_dir: Path, mode: str = "pipeline", extra: list[str] | None = None
) -> dict:
    """Run one replay and return the parsed summary JSON."""
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / f"summary_{mode}.json"
    cmd = [
        str(binary),
        "--input",
        str(input_path),
        "--mode",
        mode,
        "--hist-csv",
        str(out_dir / f"latency_{mode}.csv"),
        "--summary-json",
        str(summary_path),
        *(extra or []),
    ]
    subprocess.run(cmd, check=True)
    return json.loads(summary_path.read_text())


def format_summary(summary: dict) -> str:
    lat = summary["latency_ns"]["book"]
    return (
        f"{summary['messages']} messages, {summary['fills']} fills, "
        f"{summary['throughput_msgs_per_sec']:.0f} msg/s, "
        f"book p50={lat['p50']}ns p99={lat['p99']}ns p99.9={lat['p999']}ns"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, default=Path("out"))
    parser.add_argument("--mode", choices=("pipeline", "inline"), default="pipeline")
    args = parser.parse_args(argv)

    summary = run_replay(find_binary(args.binary), args.input, args.out_dir, args.mode)
    print(format_summary(summary))
    return 0 if summary.get("invariants_ok") else 1


if __name__ == "__main__":
    sys.exit(main())

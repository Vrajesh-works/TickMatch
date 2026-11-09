#!/usr/bin/env bash
# Generate a synthetic LOBSTER stream, replay it in both modes and plot latency.
set -euo pipefail

OUT=${OUT:-out}
MESSAGES=${MESSAGES:-200000}
SEED=${SEED:-7}
REPLAY=${REPLAY:-build/release/apps/tickmatch_replay}
CLIENT_DIR=${CLIENT_DIR:-client}

mkdir -p "$OUT"
export PYTHONPATH="$CLIENT_DIR${PYTHONPATH:+:$PYTHONPATH}"

python3 -m tickmatch_client.gen_lobster --messages "$MESSAGES" --seed "$SEED" --out "$OUT/messages.csv"

for mode in inline pipeline; do
    "$REPLAY" --input "$OUT/messages.csv" --mode "$mode" \
        --hist-csv "$OUT/latency_$mode.csv" --summary-json "$OUT/summary_$mode.json"
done

if python3 -c "import matplotlib" 2>/dev/null; then
    python3 -m tickmatch_client.plot_latency "$OUT/latency_inline.csv" "$OUT/latency_pipeline.csv" \
        --out "$OUT/latency.png" --title "TickMatch in-book latency, $MESSAGES synthetic LOBSTER messages"
else
    echo "matplotlib not installed; skipping plot (pip install matplotlib)"
fi
echo "results in $OUT/"

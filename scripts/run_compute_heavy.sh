#!/usr/bin/env bash
set -euo pipefail

mkdir -p results_final

PROCS="${PROCS:-1 2 4}" \
SIZE="${SIZE:-5000}" \
STEPS="${STEPS:-50000}" \
ANTS="${ANTS:-5000}" \
./scripts/run_benchmarks.sh | tee results_final/strong_scaling_compute_heavy_5000_50000_ants5000.csv

#!/usr/bin/env bash
set -euo pipefail

EXE="${EXE:-./build/langton_ant}"
SIZE="${SIZE:-5000}"
STEPS="${STEPS:-100000}"
ANTS="${ANTS:-100}"
SEED="${SEED:-1}"
PROCS="${PROCS:-1 2 4 8 16}"
OVERSUBSCRIBE="${OVERSUBSCRIBE:-0}"

MPI_EXTRA=()
if [[ "${OVERSUBSCRIBE}" == "1" ]]; then
    MPI_EXTRA+=(--oversubscribe)
fi

echo "processes,elapsed_seconds,compute_seconds,communication_seconds,io_seconds,speedup,efficiency_percent,serial_fraction"

baseline=""
for p in ${PROCS}; do
    line="$(mpirun "${MPI_EXTRA[@]}" -np "${p}" "${EXE}" --mode mpi --size "${SIZE}" --steps "${STEPS}" --ants "${ANTS}" --seed "${SEED}")"
    elapsed="$(printf '%s\n' "${line}" | sed -n 's/.*elapsed_seconds=\([^ ]*\).*/\1/p')"
    compute="$(printf '%s\n' "${line}" | sed -n 's/.*compute_seconds=\([^ ]*\).*/\1/p')"
    communication="$(printf '%s\n' "${line}" | sed -n 's/.*communication_seconds=\([^ ]*\).*/\1/p')"
    io="$(printf '%s\n' "${line}" | sed -n 's/.*io_seconds=\([^ ]*\).*/\1/p')"

    if [[ -z "${elapsed}" ]]; then
        echo "Could not parse elapsed time from: ${line}" >&2
        exit 1
    fi

    if [[ -z "${baseline}" ]]; then
        baseline="${elapsed}"
    fi

    speedup="$(awk -v b="${baseline}" -v e="${elapsed}" 'BEGIN { printf "%.6f", b / e }')"
    efficiency="$(awk -v s="${speedup}" -v p="${p}" 'BEGIN { printf "%.2f", (s / p) * 100.0 }')"
    serial_fraction="$(awk -v s="${speedup}" -v p="${p}" 'BEGIN { if (p == 1) printf "0.000000"; else printf "%.6f", ((1.0 / s) - (1.0 / p)) / (1.0 - (1.0 / p)) }')"
    echo "${p},${elapsed},${compute},${communication},${io},${speedup},${efficiency},${serial_fraction}"
done

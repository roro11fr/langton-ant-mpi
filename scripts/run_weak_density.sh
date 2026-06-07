#!/usr/bin/env bash
set -euo pipefail

OUT="${OUT:-results_final/weak_scaling_density_100000.csv}"
STEPS="${STEPS:-100000}"
SEED="${SEED:-1}"

mkdir -p "$(dirname "${OUT}")"
echo "experiment,processes,size,steps,ants,elapsed_seconds,compute_seconds,communication_seconds,io_seconds" > "${OUT}"

for spec in "1 500 100" "2 707 200" "4 1000 400"; do
    set -- ${spec}
    p="$1"
    size="$2"
    ants="$3"

    line="$(mpirun -np "${p}" ./build/langton_ant --mode mpi --size "${size}" --steps "${STEPS}" --ants "${ants}" --seed "${SEED}")"
    echo "${line}"

    elapsed="$(printf '%s\n' "${line}" | sed -n 's/.*elapsed_seconds=\([^ ]*\).*/\1/p')"
    compute="$(printf '%s\n' "${line}" | sed -n 's/.*compute_seconds=\([^ ]*\).*/\1/p')"
    communication="$(printf '%s\n' "${line}" | sed -n 's/.*communication_seconds=\([^ ]*\).*/\1/p')"
    io="$(printf '%s\n' "${line}" | sed -n 's/.*io_seconds=\([^ ]*\).*/\1/p')"

    echo "weak_density,${p},${size},${STEPS},${ants},${elapsed},${compute},${communication},${io}" >> "${OUT}"
done

cat "${OUT}"

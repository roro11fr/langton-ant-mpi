#!/usr/bin/env bash
set -euo pipefail

EXE="${EXE:-./build/langton_ant}"
OUT_DIR="${OUT_DIR:-results}"
PROCS="${PROCS:-1 2 4 8 16}"
STEPS="${STEPS:-100000}"
SEED="${SEED:-1}"
OVERSUBSCRIBE="${OVERSUBSCRIBE:-0}"

MPI_EXTRA=()
if [[ "${OVERSUBSCRIBE}" == "1" ]]; then
    MPI_EXTRA+=(--oversubscribe)
fi

mkdir -p "${OUT_DIR}"

run_one() {
    local p="$1"
    local size="$2"
    local ants="$3"
    local gather_every="$4"
    mpirun "${MPI_EXTRA[@]}" -np "${p}" "${EXE}" \
        --mode mpi \
        --size "${size}" \
        --steps "${STEPS}" \
        --ants "${ants}" \
        --seed "${SEED}" \
        --gather-every "${gather_every}"
}

extract_metric() {
    local line="$1"
    local name="$2"
    printf '%s\n' "${line}" | sed -n "s/.*${name}=\\([^ ]*\\).*/\\1/p"
}

write_header() {
    local file="$1"
    echo "experiment,processes,size,steps,ants,gather_every,elapsed_seconds,compute_seconds,communication_seconds,io_seconds" > "${file}"
}

append_result() {
    local file="$1"
    local experiment="$2"
    local p="$3"
    local size="$4"
    local ants="$5"
    local gather_every="$6"
    local line="$7"
    echo "${experiment},${p},${size},${STEPS},${ants},${gather_every},$(extract_metric "${line}" elapsed_seconds),$(extract_metric "${line}" compute_seconds),$(extract_metric "${line}" communication_seconds),$(extract_metric "${line}" io_seconds)" >> "${file}"
}

strong_file="${OUT_DIR}/strong_scaling.csv"
weak_file="${OUT_DIR}/weak_scaling.csv"
migration_file="${OUT_DIR}/migration_overhead.csv"
gather_file="${OUT_DIR}/gather_frequency.csv"

write_header "${strong_file}"
write_header "${weak_file}"
write_header "${migration_file}"
write_header "${gather_file}"

for p in ${PROCS}; do
    line="$(run_one "${p}" 5000 100 0)"
    append_result "${strong_file}" strong "${p}" 5000 100 0 "${line}"
done

for p in ${PROCS}; do
    size="$(awk -v p="${p}" 'BEGIN { printf "%d", 500 * sqrt(p) }')"
    line="$(run_one "${p}" "${size}" 100 0)"
    append_result "${weak_file}" weak "${p}" "${size}" 100 0 "${line}"
done

for ants in 1 10 100 1000; do
    line="$(run_one 4 1000 "${ants}" 0)"
    append_result "${migration_file}" migration 4 1000 "${ants}" 0 "${line}"
done

for gather_every in 1 10 100 1000; do
    line="$(run_one 4 1000 100 "${gather_every}")"
    append_result "${gather_file}" gather 4 1000 100 "${gather_every}" "${line}"
done

echo "Wrote:"
echo "  ${strong_file}"
echo "  ${weak_file}"
echo "  ${migration_file}"
echo "  ${gather_file}"

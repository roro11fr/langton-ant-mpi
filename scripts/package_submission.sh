#!/usr/bin/env bash
set -euo pipefail

PACKAGE_NAME="${PACKAGE_NAME:-langton-ant-mpi-submission.zip}"

rm -f "${PACKAGE_NAME}"

zip -r "${PACKAGE_NAME}" . \
  -x ".git/*" \
  -x "build/*" \
  -x "frames/*" \
  -x "results/*" \
  -x "results_large/*" \
  -x "results_test/*" \
  -x "*.ppm" \
  -x "*.log"

echo "Created ${PACKAGE_NAME}"

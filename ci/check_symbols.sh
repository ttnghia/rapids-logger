#!/bin/bash
# Copyright (c) 2025, NVIDIA CORPORATION.

set -eEuo pipefail

echo "checking for symbol visibility issues"

LIBRARY="${1}"

echo ""
echo "Checking exported symbols in '${LIBRARY}'"
symbol_file="./symbols.txt"
readelf --dyn-syms --wide "${LIBRARY}" \
    | c++filt \
    > "${symbol_file}"

for lib in fmt spdlog; do
    echo "Checking for '${lib}' symbols..."
    if grep -E "${lib}\:\:" "${symbol_file}"; then
        echo "ERROR: Found some exported symbols in ${LIBRARY} matching the pattern ${lib}::."
        exit 1
    fi
done

echo "No symbol visibility issues found in ${LIBRARY}"

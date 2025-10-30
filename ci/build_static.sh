#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

. /opt/conda/etc/profile.d/conda.sh

export CMAKE_GENERATOR=Ninja

rapids-logger "Static cpp build"

ENV_YAML_DIR="$(mktemp -d)"
ENV_FILE="${ENV_YAML_DIR}/env.yaml"
rapids-dependency-file-generator --file-key test_static_library --output conda --matrix "" | tee "${ENV_FILE}"

rapids-mamba-retry env create --yes -f "${ENV_FILE}" -n test

# Temporarily allow unbound variables for conda activation.
set +u
conda activate test
set -u

cmake -S . -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

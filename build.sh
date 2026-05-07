#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${ROOT}"
BUILD_DIR="${ROOT}/build"
BUILD_TYPE="Release"

if [[ $# -gt 0 && "$1" != "clean" ]]; then
    echo "Invalid parameter: $1"
    echo "Usage: ./build.sh [clean]"
    exit 1
fi

if [[ $# -eq 1 && "$1" == "clean" ]]; then
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    exit 0
fi

mkdir -p "${BUILD_DIR}"

echo "Running CMake configuration..."
cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" || exit 1

echo "Building tnasm target..."
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target tnasm || exit 1

echo "Building RPS target..."
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target RPS || exit 1

echo
echo "Build complete."
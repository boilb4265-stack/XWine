#!/usr/bin/env bash
set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

echo "[XWine] Configuring CMake + Ninja (macOS ARM64/x64)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="${BREW_PREFIX}"

echo "[XWine] Building all targets..."
cmake --build "${BUILD_DIR}" -j

echo "[XWine] Build completed successfully."

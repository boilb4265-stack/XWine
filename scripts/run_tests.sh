#!/usr/bin/env bash
set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

if [[ ! -d "${BUILD_DIR}" ]]; then
  "${ROOT_DIR}/scripts/build_macos.sh"
fi

echo "[XWine] Executing automated CTest suite..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

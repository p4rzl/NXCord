#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
build_dir="${BUILD_DIR:-build}"
# Keep downloaded, hash-verified crypto sources and incremental objects. Do not
# delete an existing build or a user's release directory during packaging.
cmake -S . -B "$build_dir" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/DevkitA64Libnx.cmake"
cmake --build "$build_dir" --parallel "${JOBS:-4}"
python3 tools/package_release.py --build "$build_dir"

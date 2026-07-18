#!/usr/bin/env bash
# Build and run Gravity Sandbox.
#   ./run.sh            build (if needed) and run
#   ./run.sh --build    build only, don't launch
#   ./run.sh --clean    wipe the build directory and rebuild from scratch
set -euo pipefail
cd "$(dirname "$0")"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf build
fi

# fetch raylib if the submodule hasn't been initialized (fresh clone)
if [[ ! -f vendor/raylib/CMakeLists.txt ]]; then
    echo "==> Initializing raylib submodule..."
    git submodule update --init --depth 1 vendor/raylib
fi

if [[ ! -d build ]]; then
    echo "==> Configuring..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi

echo "==> Building..."
cmake --build build -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

if [[ "${1:-}" != "--build" ]]; then
    echo "==> Running..."
    exec ./build/gravity_sandbox
fi

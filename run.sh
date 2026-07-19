#!/usr/bin/env bash
# Build and run Gravity Sandbox.
#   ./run.sh            build (if needed) and run
#   ./run.sh --build    build only, don't launch
#   ./run.sh --clean    wipe the build directories and rebuild from scratch
#   ./run.sh --web      build the web version with Emscripten and serve it locally
set -euo pipefail
cd "$(dirname "$0")"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf build build-web
fi

# fetch raylib if the submodule hasn't been initialized (fresh clone)
if [[ ! -f vendor/raylib/CMakeLists.txt ]]; then
    echo "==> Initializing raylib submodule..."
    git submodule update --init --depth 1 vendor/raylib
fi

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

if [[ "${1:-}" == "--web" ]]; then
    if ! command -v emcmake >/dev/null; then
        echo "Emscripten not found (emcmake/emcc). Install it first, e.g.: brew install emscripten"
        exit 1
    fi
    if [[ ! -d build-web ]]; then
        echo "==> Configuring web build..."
        emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
    fi
    echo "==> Building web version..."
    cmake --build build-web -j "$JOBS"
    echo "==> Serving at http://localhost:8080/gravity_sandbox.html  (Ctrl-C to stop)"
    exec python3 -m http.server 8080 -d build-web
fi

if [[ ! -d build ]]; then
    echo "==> Configuring..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi

echo "==> Building..."
cmake --build build -j "$JOBS"

if [[ "${1:-}" != "--build" ]]; then
    echo "==> Running..."
    exec ./build/gravity_sandbox
fi

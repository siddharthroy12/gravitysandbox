#!/usr/bin/env bash
# Build the web (WebAssembly) version of Gravity Sandbox into build-web/.
#   ./build-web.sh          configure (if needed) and build
#   ./build-web.sh --clean  wipe build-web and rebuild from scratch
set -euo pipefail
cd "$(dirname "$0")"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf build-web
fi

if ! command -v emcmake >/dev/null; then
    echo "Emscripten not found (emcmake/emcc). Install it first, e.g.: brew install emscripten"
    exit 1
fi

# fetch raylib if the submodule hasn't been initialized (fresh clone)
if [[ ! -f vendor/raylib/CMakeLists.txt ]]; then
    echo "==> Initializing raylib submodule..."
    git submodule update --init --depth 1 vendor/raylib
fi

if [[ ! -d build-web ]]; then
    echo "==> Configuring web build..."
    emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
fi

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
echo "==> Building web version..."
cmake --build build-web -j "$JOBS"

cat <<'EOF'
==> Done. Deployable files in build-web/:
      gravity_sandbox.{html,js,wasm,data}   (+ assets/icon.png favicon)
    Serve locally with:  python3 -m http.server 8080 -d build-web
    Then open:           http://localhost:8080/gravity_sandbox.html
EOF

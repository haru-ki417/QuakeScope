#!/bin/bash
# 判定エンジン（core/src）を WebAssembly にする。wasi-sdk が必要（WASI_SDK に場所を入れる）
set -euo pipefail
cd "$(dirname "$0")/.."
WASI_SDK=${WASI_SDK:-/opt/wasi-sdk}
"$WASI_SDK/bin/clang++" --target=wasm32-wasip1 -mexec-model=reactor -O2 -std=c++17 -fno-exceptions -Icore/src \
  core/src/wasm_api.cpp core/src/engine.cpp core/src/intensity.cpp core/src/notify.cpp core/src/screen.cpp core/src/synth.cpp \
  -Wl,--export-dynamic -Wl,--strip-all -o web/qs.wasm
echo "web/qs.wasm $(stat -c %s web/qs.wasm) bytes"

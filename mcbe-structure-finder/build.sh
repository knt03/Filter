#!/usr/bin/env bash
# ビルド:
#   ./build.sh          コマンドライン版 build/mcbe-structures
#   ./build.sh test     既知の座標との照合テスト
#   ./build.sh wasm     ブラウザ用 web/engine.wasm (zig が必要: pip install ziglang)
#   ./build.sh page     web/ を 1 ファイルにまとめた dist/index.html
set -euo pipefail
cd "$(dirname "$0")"
V=vendor/cubiomes
LIB="$V/biomes.c $V/biomenoise.c $V/finders.c $V/generator.c $V/layers.c $V/noise.c
     $V/util.c $V/terrainnoise.c $V/carver.c $V/features/*.c"
CFLAGS="-O2 -I$V -Isrc -w"
mkdir -p build

case "${1:-cli}" in
cli)
    gcc $CFLAGS src/cli.c src/bedrock.c $LIB -lm -o build/mcbe-structures
    echo "built build/mcbe-structures"
    ;;
test)
    gcc $CFLAGS tests/ground_truth.c src/bedrock.c $LIB -lm -o build/ground_truth
    ./build/ground_truth
    ;;
wasm)
    ZIG="${ZIG:-python3 -m ziglang}"
    $ZIG cc -target wasm32-wasi -O3 -s -I$V -Isrc -w -mexec-model=reactor \
        -Wl,--export=wasm_setup -Wl,--export=wasm_available -Wl,--export=wasm_scan \
        -Wl,--export=wasm_buffer -Wl,--export=wasm_biome_grid -Wl,--export=wasm_biome_rgba \
        src/wasm.c src/bedrock.c $LIB -o web/engine.wasm
    ls -l web/engine.wasm
    ;;
page)
    python3 tools/bundle.py web dist/index.html
    ;;
*)
    echo "usage: $0 [cli|test|wasm|page]" >&2
    exit 1
    ;;
esac

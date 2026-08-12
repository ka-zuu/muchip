#!/bin/sh
# scripts/build-host.sh
#
# 開発機 (x86_64 Linux 等) 向けにネイティブビルドする。 (SPEC 10.1)
# 前提パッケージ: pkg-config, libsdl2-dev, cmake, build-essential
#
#   sudo apt update && sudo apt install -y \
#       pkg-config libsdl2-dev cmake build-essential git
#
# SDL2_ttf は要らない。フォントは vendor/font8x8(ASCII)/vendor/misaki(非ASCII)
# をコンパイル時に埋め込んでおり、実行時の外部アセットロードは無い
# (src/ui.h)。以前はここに
# libsdl2-ttf-dev と書いてあったが、CI (.github/workflows/ci.yml) が
# 入れずにビルドできることを毎回実証している。
#
set -e

cd "$(dirname "$0")/.."

cmake -B build -DTARGET_HOST=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"

echo ""
echo "ビルド完了: build/muchip"

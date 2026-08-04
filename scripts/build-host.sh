#!/bin/sh
# scripts/build-host.sh
#
# 開発機 (x86_64 Linux 等) 向けにネイティブビルドする。 (SPEC 10.1)
# 前提パッケージ: pkg-config, libsdl2-dev, libsdl2-ttf-dev, cmake, build-essential
#
#   sudo apt update && sudo apt install -y \
#       pkg-config libsdl2-dev libsdl2-ttf-dev cmake build-essential git
#
set -e

cd "$(dirname "$0")/.."

cmake -B build -DTARGET_HOST=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"

echo ""
echo "ビルド完了: build/mugbs"

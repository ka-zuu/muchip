# toolchain-aarch64.cmake
#
# muOS 実機 (aarch64) 向けクロスコンパイル用ツールチェーンファイル。 (SPEC 8.4)
#
# 使い方:
#   cmake -B build-aarch64 -DTARGET_HOST=OFF \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
#
# 前提:
#   - Docker イメージ内 (docker/Dockerfile) に crossbuild-essential-arm64 が
#     入っており、aarch64-linux-gnu-gcc/g++ が使える
#   - sysroot/ に実機から抜いた SDL2 の .so とヘッダを配置してあること (README 参照)
#
# ステータス: 【P7 時点で実機検証予定・現時点では未検証】
# SPEC 8.3 は「Debian の libsdl2-dev:arm64 は実機で正しく動かない可能性が高い」と
# 警告しており、本プロジェクトの P0-P4 スコープではまだこのファイルを実際には
# ビルドに使っていない。P7 着手時に実機の SDL2 バージョン・必要バックエンドを
# 確認してから sysroot/ の内容とこのファイルの妥当性を検証すること。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_SYSROOT ${CMAKE_CURRENT_LIST_DIR}/../sysroot)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# toolchain-aarch64.cmake
#
# muOS 実機 (aarch64) 向けクロスコンパイル用ツールチェーンファイル。 (SPEC 8.4)
#
# 使い方:
#   cmake -B build-aarch64 -DTARGET_HOST=OFF \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
#   (通常は docker/Dockerfile でビルドしたイメージの中から使う。後述)
#
# ステータス: 【実機 (muOS 2601.0 JACARANDA, RG35XX系) で検証済み】
# tools/sdl_probe.c をこの方式でビルドし、実機で映像(mali driver)・
# 音声(alsa経由pipewire)とも正常動作を確認済み。詳細な調査経緯は
# PLAN.md の「P7: SDL2の扱いに関する調査」節を参照。
#
# --- CMAKE_SYSROOT を使わない理由（重要） -------------------------------
# 素直に CMAKE_SYSROOT でsysroot/を指定しても機能しない。Debianの
# crossbuild-essential-arm64 パッケージの aarch64-linux-gnu-gcc は、
# libc/libm 等の暗黙リンクに使うsysrootを `/usr/aarch64-linux-gnu` に
# ビルド時点で固定しており、コマンドラインの --sysroot 指定を無視する
# (`gcc --sysroot=X -print-file-name=libc.so` を実行しても X 配下を
#  見に行かない。実機で readelf 経由の未定義参照エラーで確認済み)。
#
# そのため本プロジェクトでは、sysroot/ から実機のヘッダ・.so を
# docker/Dockerfile が `/usr/aarch64-linux-gnu` へ直接上書きコピーする
# 方式を取る。このtoolchainファイル自体はコンパイラの指定のみを行う。
#
# --- 前提 -----------------------------------------------------------------
#   - docker/Dockerfile でビルドしたイメージ内で使うこと
#     (crossbuild-essential-arm64 と、実機ファイルで上書き済みの
#      /usr/aarch64-linux-gnu を含む)
#   - イメージビルド前に sysroot/ を実機から取得しておくこと
#     (scripts/fetch-sysroot.sh 参照)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Cortex-A53 (実機の /proc/cpuinfo で CPU part 0xd03 と確認済み)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -mcpu=cortex-a53" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a53" CACHE STRING "" FORCE)

# CMAKE_SYSROOT / CMAKE_FIND_ROOT_PATH は意図的に設定しない(上記の理由により
# 効かないため)。SDL2の解決はルート CMakeLists.txt 側で
# /usr/aarch64-linux-gnu を直接参照する形で行う。

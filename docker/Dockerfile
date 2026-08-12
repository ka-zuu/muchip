# docker/Dockerfile
#
# muOS (aarch64) 向けクロスビルド環境。 (SPEC 8.2)
#
# ステータス: 【実機 (muOS 2601.0 JACARANDA, RG35XX系) で検証済み】
# tools/sdl_probe.c をこの方式でビルドし、実機で映像・音声とも正常動作を
# 確認済み。詳細は docs/design-notes.md の「クロスビルドとmuOSパッケージング」節を参照。
#
# --- glibcバージョンについて（SPECの想定と逆だった） -----------------------
# SPEC.md 8.2 は「新しすぎるglibcでリンクすると実機の古いglibcで動かない」
# という前提で「古めのベースイメージを使う」よう指示しているが、実機を
# 調べたところ逆だった: 実機は glibc 2.38（Buildroot, 2024年ビルド）と
# 比較的新しく、Debian bullseyeのcrossbuild-essential-arm64が持つ
# glibc 2.31 の方が古い。この状態でリンクすると、実機のSDL2が要求する
# 新しいglibcシンボル(GLIBC_2.34以降のpthread/dlopen統合等)が
# 「undefined reference」になり、そもそもリンクが通らない。
#
# そのため方針を変更し、実機から抜いた libc.so.6 / libSDL2 一式を
# crossbuild-essential-arm64 が既定で参照する sysroot
# (`/usr/aarch64-linux-gnu`) へビルド時に直接上書き配置する。
# (`--sysroot`フラグでは上書きできないことを実機検証で確認済み。
#  cmake/toolchain-aarch64.cmake のコメント参照)
#
# ベースイメージ自体は debian:bullseye のままで問題ない
# (crossbuild-essential-arm64 のバイナリツール鎖自体に古さを要求する
#  理由は無く、あくまで「どのglibcにリンクするか」が上書きで解決されるため)。
#
# 前提: イメージをビルドする前に、リポジトリ直下の sysroot/ を
# scripts/fetch-sysroot.sh で実機から取得しておくこと。

FROM debian:bullseye

RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    crossbuild-essential-arm64 \
    zip \
    && rm -rf /var/lib/apt/lists/*

# 実機から抜いた libc/SDL2 一式で、クロスツールチェイン既定のsysrootを
# 上書きする。sysroot/ が空の場合はこのステップは何もしない
# (scripts/fetch-sysroot.sh を先に実行すること)。
COPY sysroot/lib/ /usr/aarch64-linux-gnu/lib/
COPY sysroot/usr/lib/ /usr/aarch64-linux-gnu/lib/
COPY sysroot/usr/include/SDL2/ /usr/aarch64-linux-gnu/include/SDL2/
RUN cd /usr/aarch64-linux-gnu/lib && \
    for f in libSDL2-2.0.so.0.*; do \
        [ -e "$f" ] || continue; \
        ln -sf "$f" libSDL2-2.0.so.0; \
        ln -sf "$f" libSDL2.so; \
    done

WORKDIR /work

# docker/Dockerfile
#
# muOS (aarch64) 向けクロスビルド環境。 (SPEC 8.2)
#
# glibc のバージョン差で実機起動時に事故る（新しすぎる glibc でリンクすると
# 実機の古い glibc で "version `GLIBC_x.xx' not found" になる）のを避けるため、
# あえて古めのベースイメージを使う。
#
# ステータス: 【P7 時点で検証予定・現時点では未検証】
# P0-P4 のスコープではこのイメージは未使用（ホストネイティブビルドのみで検証）。
# P7 着手時に実際にビルドが通ること・生成バイナリが実機で起動することを確認する。

FROM debian:bullseye

RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    crossbuild-essential-arm64 \
    zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

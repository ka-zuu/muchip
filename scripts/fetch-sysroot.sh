#!/bin/sh
# scripts/fetch-sysroot.sh
#
# muOS実機からクロスビルドに必要な libc/SDL2 一式を取得し、
# リポジトリ直下の sysroot/ を構成する。 (SPEC 8.3, PLAN.md 「P7: SDL2の扱いに関する調査」)
#
# 実機のSDL2は標準ディストリのものと大きく異なる(独自の malifb ビデオ
# ドライバを内蔵し、依存ライブラリも libc/libm のみと極めてミニマル)ため、
# 実機から直接抜く必要がある。同様にlibcも実機の方がクロスツールチェイン
# 既定のものより新しい(glibc 2.38 vs Debian bullseyeの2.31)ため、
# 実機のlibcも合わせて取得する。
#
# 使い方:
#   ./scripts/fetch-sysroot.sh root@<実機のIP>
#   ./scripts/fetch-sysroot.sh root@<実機のIP> 22   # ポート指定
#
# パスワード認証の場合は scp/ssh がその都度プロンプトを出す
# (このスクリプトにパスワードは埋め込まない)。
#
# 取得後、SDL2のバージョンを実機の.soから自動検出し、対応する
# upstream SDL2 (libsdl-org/SDL) のヘッダをそのタグから取得する
# (実機にはヘッダが同梱されていないため)。malifb等の内部ドライバ差分は
# 公開APIのヘッダには影響しないという前提に立つ
# (実機での動作確認で裏付け済み。PLAN.md参照)。

set -e

if [ -z "$1" ]; then
    echo "使い方: $0 <user@host> [port]" >&2
    exit 1
fi

TARGET="$1"
PORT="${2:-22}"
SCP="scp -P $PORT"
SSH="ssh -p $PORT"

cd "$(dirname "$0")/.."
SYSROOT="$(pwd)/sysroot"

echo "== sysroot/ を初期化 =="
mkdir -p "$SYSROOT/lib" "$SYSROOT/usr/lib" "$SYSROOT/usr/include"

echo "== 実機からlibc一式を取得 =="
for f in ld-linux-aarch64.so.1 libc.so.6 libdl.so.2 libm.so.6 libpthread.so.0; do
    $SCP "$TARGET:/lib/$f" "$SYSROOT/lib/$f"
done

echo "== 実機からSDL2本体を取得 =="
# バージョン部分(2800.5等)はビルドにより変わるためワイルドカードで取得する。
SDL2_REMOTE_NAME=$($SSH "$TARGET" "basename \$(readlink -f /usr/lib/libSDL2-2.0.so.0)")
$SCP "$TARGET:/usr/lib/$SDL2_REMOTE_NAME" "$SYSROOT/usr/lib/$SDL2_REMOTE_NAME"
ln -sf "$SDL2_REMOTE_NAME" "$SYSROOT/usr/lib/libSDL2-2.0.so.0"
ln -sf "$SDL2_REMOTE_NAME" "$SYSROOT/usr/lib/libSDL2.so"
echo "取得したSDL2: $SDL2_REMOTE_NAME"

echo "== SDL2バージョンを検出してヘッダを取得 =="
SDL2_VERSION=$(strings "$SYSROOT/usr/lib/$SDL2_REMOTE_NAME" | grep -oE 'SDL-[0-9]+\.[0-9]+\.[0-9]+' | head -1 | sed 's/SDL-//')
if [ -z "$SDL2_VERSION" ]; then
    echo "警告: SDL2バージョンを自動検出できませんでした。ヘッダは手動で用意してください。" >&2
else
    echo "検出したSDL2バージョン: $SDL2_VERSION"
    TMPDIR_SDL=$(mktemp -d)
    git clone --depth 1 --branch "release-$SDL2_VERSION" https://github.com/libsdl-org/SDL "$TMPDIR_SDL"
    mkdir -p "$SYSROOT/usr/include/SDL2"
    cp "$TMPDIR_SDL"/include/*.h "$SYSROOT/usr/include/SDL2/"
    rm -rf "$TMPDIR_SDL"
fi

echo ""
echo "sysroot/ の構成が完了しました。"
echo "次に: docker build -f docker/Dockerfile -t mugbs-crossbuild ."

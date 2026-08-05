#!/bin/sh
# scripts/package.sh
#
# packaging/muGBS/ の資材とクロスビルド済みバイナリから、muOS 用の
# muGBS-<version>.muxapp を生成する。 (SPEC 9.4)
#
#   ./scripts/build-aarch64.sh && ./scripts/package.sh
#
# オプション:
#   --bin PATH      同梱する mugbs 実行ファイル (既定: build-aarch64/mugbs)
#   --version VER   版番号 (既定: CMakeLists.txt の project(...) から読む)
#   --out FILE      出力先 (既定: ./muGBS-<version>.muxapp)
#   --no-strip      strip をしない (ダミーバイナリで構造だけ検証するとき用)
#   --keep-stage    staging ディレクトリを消さない (デバッグ用)
#   --print-version 解決したバージョンを表示して終了する
#                   (tests/test_package.sh が CMake の PROJECT_VERSION との
#                    一致を見るために使う)
#
# .muxapp は拡張子を変えた zip。muOS の Archive Manager から
# Applications > Archive Manager で展開してインストールする。
#
# **zip 内のトップレベルディレクトリは `muGBS/`** であり、SPEC 9.1 が書く
# `mnt/mmc/MUOS/application/muGBS/` ではない。MustardOS/internal の
# script/mux/extract.sh が
#     EXTRACT_ARCHIVE "Application" "$ARCHIVE" "$MUOS_STORE_DIR/application"
# (= unzip -o -d /run/muos/storage/application) で展開するため、SPEC通りだと
# .../application/mnt/mmc/MUOS/application/muGBS/ に展開されて動かない。
# 実際に動作している muOS アプリ (XMPlayer v0.2.1) の .muxapp も全エントリが
# `XMPlayer/` 始まりだった。
#
set -e

cd "$(dirname "$0")/.."

IMAGE="${MUGBS_CROSS_IMAGE:-mugbs-crossbuild}"
PKG_NAME="muGBS"     # zip内トップレベル = 実機の application/<これ>/
SRC_DIR="packaging/$PKG_NAME"

BIN="build-aarch64/mugbs"
VERSION=""
OUT=""
DO_STRIP=1
KEEP_STAGE=0
PRINT_VERSION=0

die() {
	echo "エラー: $1" >&2
	shift
	for line in "$@"; do echo "  $line" >&2; done
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
	--bin) BIN="$2"; shift 2 ;;
	--version) VERSION="$2"; shift 2 ;;
	--out) OUT="$2"; shift 2 ;;
	--no-strip) DO_STRIP=0; shift ;;
	--keep-stage) KEEP_STAGE=1; shift ;;
	--print-version) PRINT_VERSION=1; shift ;;
	*) die "不明なオプション: $1" "使い方: $0 [--bin PATH] [--version VER] [--out FILE] [--no-strip] [--keep-stage] [--print-version]" ;;
	esac
done

# --- バージョン -------------------------------------------------------------
# CMakeLists.txt の project(...) 行を唯一の情報源とする。CMake を介さない
# (configure 済みの build ディレクトリを要求しない)ため、ここでは sed で直接
# 読む。project() を1行で書く前提が壊れると無言で空になるので、
# tests/test_package.sh が --print-version と CMake の PROJECT_VERSION の
# 一致を検証している。
if [ -z "$VERSION" ]; then
	VERSION=$(sed -n 's/^[[:space:]]*project([[:space:]]*mugbs[[:space:]]\{1,\}VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
	[ -n "$VERSION" ] ||
		die "CMakeLists.txt からバージョンを読めませんでした。" \
			"project(mugbs VERSION x.y.z LANGUAGES C CXX) を1行で書く形を保ってください。"
fi

if [ "$PRINT_VERSION" = 1 ]; then
	echo "$VERSION"
	exit 0
fi

# --- 前提チェック -----------------------------------------------------------

command -v zip >/dev/null 2>&1 || die "zip が見つかりません。" "  sudo apt install -y zip"
command -v unzip >/dev/null 2>&1 || die "unzip が見つかりません。" "  sudo apt install -y unzip"

[ -f "$BIN" ] ||
	die "実行ファイルがありません: $BIN" \
		"先にクロスビルドしてください:" \
		"  ./scripts/build-aarch64.sh"

[ -n "$OUT" ] || OUT="./$PKG_NAME-$VERSION.muxapp"
# zip は staging ディレクトリの中から実行するので、出力先は絶対パスにする。
case "$OUT" in
/*) OUT_ABS="$OUT" ;;
*) OUT_ABS="$PWD/${OUT#./}" ;;
esac

# --- staging ----------------------------------------------------------------

STAGE=$(mktemp -d)
if [ "$KEEP_STAGE" = 1 ]; then
	echo "staging: $STAGE (--keep-stage のため削除しません)"
else
	trap 'rm -rf "$STAGE"' EXIT INT TERM
fi

PKG="$STAGE/$PKG_NAME"
mkdir -p "$PKG/bin" "$PKG/glyph" "$PKG/grid"

# packaging/muGBS/ の中身は zip 内の muGBS/ と1:1に対応している。
cp "$SRC_DIR/mux_launch.sh" "$PKG/"
cp "$SRC_DIR/mux_lang.ini" "$PKG/"
cp "$SRC_DIR/config.ini" "$PKG/"
cp "$SRC_DIR/glyph/mugbs.png" "$PKG/glyph/"
cp "$SRC_DIR/grid/mugbs.png" "$PKG/grid/"
# build-aarch64/ が(古い環境で)root所有でも、cp した先はホストユーザー所有に
# なるので以降の chmod / strip が通る。
cp "$BIN" "$PKG/bin/mugbs"

# lib/ と assets/ は同梱しない (SPEC 9.1 からの逸脱)。
#   lib/    … SDL2 だけが実機の動的ライブラリで、libgme/miniz は静的リンク、
#             libstdc++/libgcc も -static-* 済み
#   assets/ … フォントは vendor/font8x8 をコンパイル時埋め込みしており
#             (src/ui.c)、実行時の外部アセットロードはゼロ

# --- strip ------------------------------------------------------------------
# RelWithDebInfo でビルドしているため debug_info が大半を占める。実機のSDカードに
# 3.7MB を置く意味は無いので落とす。ホストにクロス binutils は通常無いので、
# 無ければクロスビルド用 Docker イメージを借りる。
strip_binary() {
	target="$1"
	if command -v aarch64-linux-gnu-strip >/dev/null 2>&1; then
		aarch64-linux-gnu-strip "$target"
		return 0
	fi
	if command -v docker >/dev/null 2>&1 && docker image inspect "$IMAGE" >/dev/null 2>&1; then
		docker run --rm -u "$(id -u):$(id -g)" \
			-v "$(dirname "$target"):/strip" "$IMAGE" \
			aarch64-linux-gnu-strip "/strip/$(basename "$target")"
		return 0
	fi
	return 1
}

if [ "$DO_STRIP" = 1 ]; then
	before=$(wc -c <"$PKG/bin/mugbs")
	# 黙って strip 無しの巨大バイナリを出荷しないよう、失敗したら止める。
	strip_binary "$PKG/bin/mugbs" ||
		die "aarch64-linux-gnu-strip を実行できませんでした。" \
			"ホストにクロス binutils が無く、Dockerイメージ $IMAGE も見つかりません。" \
			"  ./scripts/build-aarch64.sh   (イメージを作る)" \
			"strip せずに作る場合は --no-strip を付けてください。"
	after=$(wc -c <"$PKG/bin/mugbs")
	echo "strip: $before -> $after バイト"
fi

# --- パーミッションと zip ---------------------------------------------------
# muxfrontend (script/mux/frontend.sh:108) は
#   [ -x "$RUN_APP/mux_launch.sh" ]
# を見てからアプリを起動する。実行ビットが無いと無言でスキップされ、muOS側の
# ログに "Invalid app launcher" が出るだけになる。zip は Unix のモードを
# external file attributes に保存し、muOS の unzip (Info-ZIP) が復元するので、
# ここで立てておけば実機まで保たれる。
chmod 755 "$PKG/mux_launch.sh" "$PKG/bin/mugbs"
chmod 644 "$PKG/mux_lang.ini" "$PKG/config.ini" "$PKG/glyph/mugbs.png" "$PKG/grid/mugbs.png"

rm -f "$OUT_ABS"
# -X で uid/gid と拡張タイムスタンプを落とす(モードは保たれる)。
# SPEC 9.4 の `cd package_root && zip -r ../x.muxapp .` は使わない:
# "./" エントリが混ざり、かつトップレベルディレクトリ名が zip に入らないため
# 上記の muGBS/ 構造を満たせない。
# -x '.*' はトップレベルのドットファイルにしかマッチしないので '*/.*' も要る。
(cd "$STAGE" && zip -r -X -q "$OUT_ABS" "$PKG_NAME" -x '.*' '*/.*' '__MACOSX/*')

# --- 生成物の自己検証 -------------------------------------------------------
# tests/test_package.sh と同じ検査をここでも行う(手で叩いた場合も守られる)。
entries=$(unzip -Z1 "$OUT_ABS")

# muOS の SAFE_ARCHIVE (internal/script/var/zip.sh) と同じ拒否条件。
if printf '%s\n' "$entries" | grep -Eq '^/|(^|/)\.\.(/|$)'; then
	die "絶対パスまたは '..' を含んでいます(muOSのArchive Managerに拒否されます)。"
fi
if printf '%s\n' "$entries" | grep -qv "^$PKG_NAME/"; then
	die "トップレベルが $PKG_NAME/ ではないエントリがあります。"
fi

echo ""
unzip -Z "$OUT_ABS"
echo ""
echo "パッケージ完了: $OUT ($(wc -c <"$OUT_ABS") バイト)"
echo "実機へのインストール:"
echo "  scp $OUT root@<実機のIP>:/mnt/mmc/ARCHIVE/"
echo "  実機で Applications > Archive Manager から展開する"

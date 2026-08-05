#!/bin/sh
# scripts/build-aarch64.sh
#
# muOS 実機 (aarch64) 向けにクロスビルドする。 (SPEC 8)
# docker/Dockerfile で作ったイメージの中で cmake を回すだけのラッパ。
#
#   ./scripts/build-aarch64.sh                  通常ビルド
#   ./scripts/build-aarch64.sh --rebuild-image  Dockerイメージを作り直してからビルド
#                                               (sysroot/ を更新したら必須)
#
# 前提: 実機から sysroot/ を取得済みであること。
#
#   ./scripts/fetch-sysroot.sh root@<実機のIP>
#
# 環境変数:
#   MUGBS_CROSS_IMAGE      Dockerイメージ名   (既定: mugbs-crossbuild)
#   MUGBS_CROSS_BUILD_DIR  ビルドディレクトリ (既定: build-aarch64)
#
set -e

cd "$(dirname "$0")/.."

IMAGE="${MUGBS_CROSS_IMAGE:-mugbs-crossbuild}"
BUILD_DIR="${MUGBS_CROSS_BUILD_DIR:-build-aarch64}"
REBUILD_IMAGE=0

for arg in "$@"; do
	case "$arg" in
	--rebuild-image) REBUILD_IMAGE=1 ;;
	*)
		echo "不明なオプション: $arg" >&2
		echo "使い方: $0 [--rebuild-image]" >&2
		exit 2
		;;
	esac
done

die() {
	echo "エラー: $1" >&2
	shift
	for line in "$@"; do echo "  $line" >&2; done
	exit 1
}

# --- 前提チェック -----------------------------------------------------------

command -v docker >/dev/null 2>&1 ||
	die "docker が見つかりません。" \
		"クロスビルドは docker/Dockerfile のイメージ内で行います。" \
		"詳細は README.md の「実機(muOS)向けクロスビルドについて」を参照してください。"

# sysroot/ には実機から抜いた libc/libSDL2 と SDL2 ヘッダが入る。これが無いと
# docker build 自体が COPY で失敗するので、先に分かりやすく落とす。
[ -f sysroot/usr/lib/libSDL2.so ] ||
	die "sysroot/ がありません(または不完全です)。" \
		"実機の libc/libSDL2 とヘッダが必要です。先に次を実行してください:" \
		"  ./scripts/fetch-sysroot.sh root@<実機のIP>"

# 以前 docker を root で走らせて作られたディレクトリが残っていると、下記の
# -u 付き docker run が書き込めずに分かりにくいエラーになる。先回りして落とす。
if [ -e "$BUILD_DIR" ] && [ ! -w "$BUILD_DIR" ]; then
	die "$BUILD_DIR に書き込めません。" \
		"以前 docker を root ユーザーで実行して作られたディレクトリが残っています。" \
		"  sudo rm -rf $BUILD_DIR" \
		"を実行してからやり直してください。"
fi

# --- Dockerイメージ ---------------------------------------------------------

if [ "$REBUILD_IMAGE" = 1 ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
	echo "Dockerイメージ $IMAGE を作成します (sysroot/ を取り込みます)..."
	docker build -f docker/Dockerfile -t "$IMAGE" .
fi

# --- クロスビルド -----------------------------------------------------------
# -u で呼び出しユーザーの uid:gid で走らせる。付けないと build-aarch64/ が
# root 所有で作られ、以後ホスト側から消せなくなる。
# -e HOME=/tmp は -u 指定時に HOME を持たないユーザーになるため
# (cmake/git が警告を出す) の対策。
DOCKER_RUN="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work $IMAGE"

echo "クロスビルド中 ($BUILD_DIR)..."
$DOCKER_RUN sh -c "
	set -e
	cmake -B '$BUILD_DIR' -DTARGET_HOST=OFF \
	      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
	      -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build '$BUILD_DIR' -j\$(nproc)
"

# --- 成果物の健全性チェック -------------------------------------------------
# 期待する NEEDED は libSDL2-2.0.so.0 / libm.so.6 / libc.so.6 の3つだけ。
# libstdc++.so.6 が現れたら -static-libstdc++ の回帰(実機で
# "GLIBCXX_3.4.xx not found" になる。SPEC 4.1/13章)。
echo ""
$DOCKER_RUN sh -c "
	file '$BUILD_DIR/mugbs'
	echo '--- NEEDED ---'
	aarch64-linux-gnu-readelf -d '$BUILD_DIR/mugbs' | sed -n 's/.*NEEDED.*\[\(.*\)\]/  \1/p'
"

echo ""
echo "ビルド完了: $BUILD_DIR/mugbs"
echo "パッケージ化する場合: ./scripts/package.sh"

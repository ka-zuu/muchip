#!/bin/sh
# scripts/release.sh
#
# muGBS のリリース作業を1本にまとめる。 (SPEC 9.5, P13)
#
# GitHub Actions では実機向け aarch64 バイナリを作れない。クロスビルドには
# 実機から抜いた sysroot/ (実機の libc.so.6 と libSDL2 の実体) が必要で、
# これは実機由来のバイナリなのでリポジトリに置かない方針だから。したがって
# .muxapp は必ず開発機で作り、GitHub Release へアップロードする。この
# スクリプトはその手順(検査 -> ビルド -> パッケージ -> タグ -> 下書き
# リリース)を固定し、「テストを通さずにタグを打つ」「CMakeLists.txt の
# 版番号とタグがずれる」「CHANGELOG を書き忘れる」といった事故を機械的に
# 防ぐためのもの。
#
# 取り返しのつかない操作(タグの作成と push、リリースの作成)は最後にまとめて
# あるので、途中で落ちてもリモートには何も残らない。
#
# 事前にやっておくこと:
#   1. CMakeLists.txt の project(mugbs VERSION x.y.z ...) を上げる
#   2. CHANGELOG.md の「## Unreleased」を「## vx.y.z - YYYY-MM-DD」にする
#   3. 上の2つを PR 経由で main へマージし、ローカルの main を最新にする
#   4. 実機から sysroot/ を取得済みであること (scripts/fetch-sysroot.sh)
#
# 使い方:
#   ./scripts/release.sh --dry-run   検査とビルドだけ行い、何も変更しない
#   ./scripts/release.sh             タグを打ち、下書きリリースを作る
#
# オプション:
#   --dry-run       git tag / git push / gh release を実行せず、実行予定の
#                   コマンドを表示するだけにする。検査・ビルド・パッケージは
#                   実際に行う(クロスビルドこそリハーサルしたい所なので)
#   --print-notes   CHANGELOG.md から現バージョンの節を切り出して表示し終了する
#                   (.github/workflows/release-guard.yml がこれを呼ぶ)
#   --remote NAME   push 先リモート (既定: origin)
#   --branch NAME   リリース元ブランチ (既定: main)
#   --notes FILE    リリース本文を CHANGELOG.md ではなくこのファイルから取る
#   --keep-binary   クロスビルドをやり直さず既存の build-aarch64/mugbs を使う
#   --no-github     タグの push までで止め、gh release を作らない
#   --publish       下書きではなく公開状態でリリースを作る
#                   (既定は下書き。Release Guard ワークフローが緑になったのを
#                    確認してから gh release edit --draft=false で公開する)
#
set -e

cd "$(dirname "$0")/.."

REMOTE="origin"
BRANCH="main"
SUBMODULE="vendor/game-music-emu"
BUILD_DIR="build-release"
DRY_RUN=0
PRINT_NOTES=0
NOTES_FILE=""
KEEP_BINARY=0
NO_GITHUB=0
PUBLISH=0

die() {
	echo "エラー: $1" >&2
	shift
	for line in "$@"; do echo "  $line" >&2; done
	exit 1
}

# --dry-run のとき「実行せずに表示するだけ」にするためのラッパ。
run() {
	if [ "$DRY_RUN" = 1 ]; then
		echo "  [dry-run] $*"
	else
		"$@"
	fi
}

while [ $# -gt 0 ]; do
	case "$1" in
	--dry-run) DRY_RUN=1; shift ;;
	--print-notes) PRINT_NOTES=1; shift ;;
	--remote) REMOTE="$2"; shift 2 ;;
	--branch) BRANCH="$2"; shift 2 ;;
	--notes) NOTES_FILE="$2"; shift 2 ;;
	--keep-binary) KEEP_BINARY=1; shift ;;
	--no-github) NO_GITHUB=1; shift ;;
	--publish) PUBLISH=1; shift ;;
	*) die "不明なオプション: $1" \
		"使い方: $0 [--dry-run] [--print-notes] [--remote NAME] [--branch NAME]" \
		"          [--notes FILE] [--keep-binary] [--no-github] [--publish]" ;;
	esac
done

# --- バージョン -------------------------------------------------------------
# 版番号の唯一の情報源は CMakeLists.txt。sed のパターンを2箇所に書かないよう、
# package.sh に問い合わせて解釈を一本化する。
VERSION=$(./scripts/package.sh --print-version)
[ -n "$VERSION" ] || die "バージョンを取得できませんでした。"
TAG="v$VERSION"
MUXAPP="./muGBS-$VERSION.muxapp"

# --- CHANGELOG からリリースノートを切り出す ---------------------------------
# 「## vX.Y.Z - YYYY-MM-DD」の次の行から、次の「## 」の直前まで。
# 見出しの末尾に空白を含めて前方一致を見るので、「## v1.0.0-rc1 ...」のような
# 別バージョンには誤ヒットしない。
changelog_section() {
	awk -v head="## v$1 " '
		index($0, head) == 1 { inside = 1; next }
		inside && index($0, "## ") == 1 { exit }
		inside { print }
	' CHANGELOG.md
}

if [ "$PRINT_NOTES" = 1 ]; then
	body=$(changelog_section "$VERSION")
	[ -n "$(printf '%s' "$body" | tr -d '[:space:]')" ] ||
		die "CHANGELOG.md に '## v$VERSION - YYYY-MM-DD' の節が無いか、本文が空です。" \
			"リリース前に「## Unreleased」を「## v$VERSION - $(date +%Y-%m-%d)」へ" \
			"書き換えてください。"
	printf '%s\n' "$body"
	exit 0
fi

echo "== muGBS $TAG のリリース =="
if [ "$DRY_RUN" = 1 ]; then
	echo "(--dry-run: 検査とビルドは実行しますが、変更操作はしません)"
fi
echo ""

# --- 1. 前提コマンド --------------------------------------------------------

echo "-- 前提コマンドを確認 --"
for c in git docker zip unzip sha256sum cmake; do
	command -v "$c" >/dev/null 2>&1 || die "$c が見つかりません。"
done
# 下の CTest は MUGBS_REQUIRE_SHELLCHECK=1 で回すので shellcheck も必須。
# ここで見ておかないと、クロスビルドの直前まで進んでから
# test_package の中で落ちることになる。
command -v shellcheck >/dev/null 2>&1 ||
	die "shellcheck が見つかりません。" \
		"リリース時のテストはシェルスクリプトの静的解析込みで回します。" \
		"  sudo apt install -y shellcheck"
if [ "$NO_GITHUB" = 0 ]; then
	command -v gh >/dev/null 2>&1 ||
		die "gh (GitHub CLI) が見つかりません。" \
			"リリース作成を省くなら --no-github を付けてください。"
	gh auth status >/dev/null 2>&1 || die "gh が認証されていません。" "  gh auth login"
fi

# --- 2. リポジトリの状態 ----------------------------------------------------

echo "-- リポジトリの状態を確認 --"
current_branch=$(git rev-parse --abbrev-ref HEAD)
[ "$current_branch" = "$BRANCH" ] ||
	die "現在のブランチは $current_branch です ($BRANCH からリリースしてください)。"

# submodule の変更もここで拾える。
[ -z "$(git status --porcelain)" ] ||
	die "作業ツリーがクリーンではありません。" \
		"コミットするか退避してからやり直してください。"

git fetch --quiet "$REMOTE" "$BRANCH"
local_head=$(git rev-parse HEAD)
remote_head=$(git rev-parse "$REMOTE/$BRANCH")
[ "$local_head" = "$remote_head" ] ||
	die "ローカルの $BRANCH が $REMOTE/$BRANCH と一致しません。" \
		"  ローカル: $local_head" \
		"  リモート: $remote_head" \
		"pull / push して揃えてからやり直してください。"

# gitlink が公開リモートから取得できることを見る。ここが崩れていると CI も
# 第三者も submodule を解決できない (P13 C1 で実際に起きた)。
sub_sha=$(git -C "$SUBMODULE" rev-parse HEAD)
git -C "$SUBMODULE" fetch --quiet origin
[ -n "$(git -C "$SUBMODULE" branch -r --contains "$sub_sha" 2>/dev/null)" ] ||
	die "submodule $SUBMODULE の $sub_sha が origin のどのブランチにもありません。" \
		"この状態では GitHub Actions も第三者の clone も submodule を解決できません。" \
		"  git -C $SUBMODULE push origin HEAD:<ブランチ名>" \
		"をしてから .gitmodules を確認してください。"

# --- 3. タグの重複確認 ------------------------------------------------------
# git rev-parse ... && die ... と書くと、rev-parse が失敗した時点で複合
# コマンドの終了ステータスが非0になり set -e がスクリプトごと殺す。if で書く。

echo "-- タグ $TAG が未使用か確認 --"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null 2>&1; then
	die "タグ $TAG は既にローカルに存在します。"
fi
if [ -n "$(git ls-remote --tags "$REMOTE" "refs/tags/$TAG")" ]; then
	die "タグ $TAG は既に $REMOTE に存在します。"
fi

# --- 4. リリースノート ------------------------------------------------------

echo "-- リリースノートを用意 --"
NOTES=$(mktemp)
trap 'rm -f "$NOTES"' EXIT INT TERM
if [ -n "$NOTES_FILE" ]; then
	[ -f "$NOTES_FILE" ] || die "--notes に指定したファイルがありません: $NOTES_FILE"
	cat "$NOTES_FILE" >"$NOTES"
else
	./scripts/release.sh --print-notes >"$NOTES"
fi

# --- 5. ホストビルドとテスト ------------------------------------------------
# 開発機の build/ を壊さないよう専用ディレクトリを毎回作り直す
# (.gitignore の build-*/ に含まれる)。コミットし忘れや古いオブジェクトの
# 残骸を掴まないようにするのが目的なので、使い回しはしない。

echo ""
echo "-- ホストビルドと CTest ($BUILD_DIR) --"
rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" -DTARGET_HOST=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" >/dev/null

CTEST_LOG=$(mktemp)
trap 'rm -f "$NOTES" "$CTEST_LOG"' EXIT INT TERM
if ! MUGBS_REQUIRE_SHELLCHECK=1 ctest --test-dir "$BUILD_DIR" \
	--output-on-failure --no-tests=error -j"$(nproc)" >"$CTEST_LOG" 2>&1; then
	cat "$CTEST_LOG" >&2
	die "CTest が失敗しました。"
fi
if grep -q '\*\*\*Skipped' "$CTEST_LOG"; then
	cat "$CTEST_LOG" >&2
	die "SKIP されたテストがあります (リリース時は許容しません)。"
fi
tail -3 "$CTEST_LOG"

# --- 6. クロスビルドとパッケージング ----------------------------------------

echo ""
echo "-- 実機向けクロスビルドと .muxapp の生成 --"
if [ "$KEEP_BINARY" = 1 ]; then
	echo "  (--keep-binary: build-aarch64/mugbs をそのまま使います)"
	[ -f build-aarch64/mugbs ] || die "build-aarch64/mugbs がありません。"
else
	./scripts/build-aarch64.sh
fi
rm -f "$MUXAPP"
./scripts/package.sh >/dev/null
[ -f "$MUXAPP" ] || die "$MUXAPP が生成されませんでした。"

# package.sh 側が zip の構造 (絶対パス無し / '..' 無し / 全エントリが
# muGBS/ 始まり) と strip を自己検証済みなので、ここでは重複して見ない。
SIZE=$(wc -c <"$MUXAPP")
SHA=$(sha256sum "$MUXAPP" | cut -d' ' -f1)
echo "  $MUXAPP ($SIZE バイト)"
echo "  SHA256: $SHA"

# --- 7. 成果物の情報をリリースノートへ追記 ----------------------------------
# 手で書き忘れる余地を無くし、ダウンロードした人が検証できるようにする。

{
	echo ""
	echo "### 成果物"
	echo ""
	echo "- \`muGBS-$VERSION.muxapp\` ($SIZE バイト)"
	echo "  - SHA256: \`$SHA\`"
	echo "- muOS の **Applications > Archive Manager** から展開してインストールしてください。"
} >>"$NOTES"

# --- 8. タグの作成と push ---------------------------------------------------

echo ""
echo "-- タグ $TAG --"
run git tag -a "$TAG" -m "muGBS $TAG"
run git push "$REMOTE" "$TAG"

# --- 9. GitHub Release ------------------------------------------------------

if [ "$NO_GITHUB" = 1 ]; then
	echo ""
	echo "(--no-github: GitHub Release は作りません)"
else
	echo ""
	echo "-- GitHub Release --"
	if [ "$PUBLISH" = 1 ]; then
		run gh release create "$TAG" "$MUXAPP" \
			--title "muGBS $VERSION" --notes-file "$NOTES"
	else
		run gh release create "$TAG" "$MUXAPP" \
			--title "muGBS $VERSION" --notes-file "$NOTES" --draft
	fi
fi

# --- 10. 次にやること -------------------------------------------------------

echo ""
if [ "$DRY_RUN" = 1 ]; then
	echo "リハーサル完了。上記の [dry-run] 行が実際に実行される操作です。"
	echo "リリースノートの中身:"
	echo "---"
	cat "$NOTES"
	echo "---"
	exit 0
fi

echo "リリース下書きを作成しました。次にやること:"
echo "  1. Release Guard ワークフローが緑になるのを確認する"
echo "       gh run list --workflow=release-guard.yml --limit 1"
echo "  2. 下書きを公開する"
echo "       gh release edit $TAG --draft=false"
echo "  3. .muxapp を実機へ入れて起動を確認する"
echo "       scp $MUXAPP root@<実機のIP>:/mnt/mmc/ARCHIVE/"

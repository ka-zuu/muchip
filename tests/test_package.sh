#!/bin/sh
# tests/test_package.sh - scripts/package.sh が生成する .muxapp の構造を検証する。 (P7)
#
#   使い方: sh test_package.sh <リポジトリルート> <作業ディレクトリ> <PROJECT_VERSION>
#
# 他のテストは全て C だが、検証対象がシェルスクリプトと zip の中身なので
# C で書くと system()/popen() だらけになる。CTest は終了コードで成否を
# 判定する流儀なので、sh でも tests/ の思想からは外れない
# (test_util.h の「FAIL <場所>: <内容>」という出力形式だけ踏襲する)。
#
# クロスビルド成果物 (build-aarch64/muchip) が無いホストやCIでも動くこと。
# package.sh が守るべき不変条件はバイナリの中身と無関係なので、--bin に
# ダミーファイルを渡し、aarch64向けの strip は --no-strip で回避する。
#
# zip/unzip が無い環境では 77 を返す (CMake側で SKIP_RETURN_CODE 77 を設定)。

ROOT="$1"
WORK="$2"
CMAKE_VERSION="$3"

[ -n "$ROOT" ] && [ -n "$WORK" ] && [ -n "$CMAKE_VERSION" ] || {
	echo "使い方: $0 <リポジトリルート> <作業ディレクトリ> <PROJECT_VERSION>" >&2
	exit 2
}

fails=0
fail() {
	echo "FAIL $1: $2" >&2
	fails=$((fails + 1))
}

command -v zip >/dev/null 2>&1 || { echo "SKIP: zip が無い"; exit 77; }
command -v unzip >/dev/null 2>&1 || { echo "SKIP: unzip が無い"; exit 77; }

cd "$ROOT" || exit 2

rm -rf "$WORK"
mkdir -p "$WORK"
# 実バイナリの代わりのダミー。中身は検証に使わない。
dd if=/dev/zero of="$WORK/dummy-muchip" bs=1024 count=1 2>/dev/null

# --- A. シェルスクリプトの構文 ----------------------------------------------
# 検査対象はこのリポジトリのシェルスクリプト全部。新しくシェルスクリプトを
# 追加したら必ずこのリストにも足すこと (SPEC 12)。
SHELL_SCRIPTS="scripts/package.sh
scripts/build-aarch64.sh
scripts/build-host.sh
scripts/fetch-sysroot.sh
scripts/release.sh
packaging/muChip/mux_launch.sh
tests/test_package.sh
.githooks/pre-push"

for s in $SHELL_SCRIPTS; do
	sh -n "$s" 2>/dev/null || fail "$s" "A-1 sh -n が通らない"
done

# 静的解析は shellcheck に任せる。POSIX sh (-s sh) として検査するので、
# 実機の busybox ash で動く mux_launch.sh に bashism が紛れ込むと SC3xxx で
# 落ちる。SC3xxx の severity は warning なので -S error では拾えない。
#
# なお shellcheck は開発ツールであって muChip のビルド成果物の依存では
# ないので、手元に無ければ黙って飛ばす
# (SPEC 12 の「依存追加は事前に相談」の対象外)。
# ただしそれだと CI で apt を書き忘れたときに検査が無言で消えるため、
# MUCHIP_REQUIRE_SHELLCHECK=1 のときだけは「無いこと」自体を失敗にする。
# CI (.github/workflows/ci.yml) と scripts/release.sh がこれを立てる。
if command -v shellcheck >/dev/null 2>&1; then
	# shellcheck disable=SC2086 # SHELL_SCRIPTS は意図的に単語分割させる
	shellcheck -s sh -S warning $SHELL_SCRIPTS ||
		fail "shellcheck" "A-2 warning 以上の指摘がある"
elif [ "${MUCHIP_REQUIRE_SHELLCHECK:-0}" = "1" ]; then
	fail "shellcheck" "A-2 MUCHIP_REQUIRE_SHELLCHECK=1 だが shellcheck が無い"
fi

# --- B. バージョンの一致 ----------------------------------------------------
# package.sh は .muxapp のファイル名を決めるために CMakeLists.txt を sed で
# 読む。CMake が実際に解釈した PROJECT_VERSION と食い違えば、project() の
# 書き方が変わって sed が拾えなくなったということ。

pkg_version=$(./scripts/package.sh --print-version 2>&1) || pkg_version="(失敗: $pkg_version)"
[ "$pkg_version" = "$CMAKE_VERSION" ] ||
	fail "CMakeLists.txt" "B-1 package.sh のバージョン解釈 '$pkg_version' が CMake の PROJECT_VERSION '$CMAKE_VERSION' と一致しない"

# --- パッケージを生成 -------------------------------------------------------
# 出力は作業ディレクトリへ出す。リポジトリルートの既定名で作ると、ユーザーが
# 手で作った muChip-<version>.muxapp を上書き・削除してしまう。

OUT="$WORK/muChip-test.muxapp"
if ! ./scripts/package.sh --bin "$WORK/dummy-muchip" --no-strip --out "$OUT" \
	>"$WORK/package.log" 2>&1; then
	echo "FAIL scripts/package.sh: 実行に失敗した" >&2
	cat "$WORK/package.log" >&2
	exit 1
fi

entries=$(unzip -Z1 "$OUT")
modes=$(unzip -Z "$OUT")

# --- C. zip の構造 ----------------------------------------------------------

# C-1: 全エントリが muChip/ 始まり。
#      (SPEC 9.1 の mnt/mmc/MUOS/application/muChip/ ではない。extract.sh が
#       /run/muos/storage/application へ展開するため)
printf '%s\n' "$entries" | grep -qv '^muChip/' &&
	fail "$OUT" "C-1 トップレベルが muChip/ ではないエントリがある"

# C-2: muOS の SAFE_ARCHIVE (internal/script/var/zip.sh) と同じ拒否条件。
printf '%s\n' "$entries" | grep -Eq '^/|(^|/)\.\.(/|$)' &&
	fail "$OUT" "C-2 絶対パスまたは '..' を含む(Archive Manager に拒否される)"

# C-3: 必須エントリ。LICENSE/THIRD-PARTY.md/licenses/LGPL-2.1.txt は
#      libgme(LGPL-2.1)を静的リンクしているための同梱義務(THIRD-PARTY.md参照)。
for e in muChip/mux_launch.sh muChip/mux_lang.ini muChip/config.ini \
	muChip/bin/muchip muChip/glyph/muchip.png muChip/grid/muchip.png \
	muChip/LICENSE muChip/THIRD-PARTY.md muChip/licenses/LGPL-2.1.txt; do
	printf '%s\n' "$entries" | grep -qx "$e" || fail "$OUT" "C-3 $e が無い"
done

# C-4: 実行ビット。muxfrontend (script/mux/frontend.sh) は
#      [ -x "$RUN_APP/mux_launch.sh" ] を見てからアプリを起動する。
for e in muChip/mux_launch.sh muChip/bin/muchip; do
	m=$(printf '%s\n' "$modes" | awk -v f="$e" '$NF == f { print $1 }')
	case "$m" in
	-rwx*) ;;
	*) fail "$OUT" "C-4 $e に実行ビットが無い (mode=${m:-不明})" ;;
	esac
done

# C-5: assets/ を含まない。SPEC 9.1 は assets/font.ttf を要求するが、
#      フォントは vendor/font8x8 をコンパイル時埋め込みしている(src/ui.c)ため
#      不要。この逸脱を固定するための negative test。
printf '%s\n' "$entries" | grep -q '^muChip/assets/' &&
	fail "$OUT" "C-5 assets/ が入っている(font8x8 埋め込みなので不要)"

# --- D. mux_launch.sh の muOS 作法 ------------------------------------------

LAUNCH=packaging/muChip/mux_launch.sh

head -1 "$LAUNCH" | grep -qx '#!/bin/sh' || fail "$LAUNCH" "D-1 1行目が #!/bin/sh でない"

# D-2: frontend/common/fileio.c の get_script_value() は "# KEY: " の
#      行頭完全前方一致でパースする。空白の数がずれると無言でフォールバックする。
for key in HELP ICON GRID; do
	grep -q "^# $key: ." "$LAUNCH" ||
		fail "$LAUNCH" "D-2 '# $key: <値>' の行が無い(空白1つ+値が必須)"
done

# D-3/D-4: func.sh の読み込みと SETUP_APP。func.sh は CPUガバナ・HOME・
#          XDG_RUNTIME_DIR/PIPEWIRE_RUNTIME_DIR・SDL環境変数を設定するので
#          削除すると実機で音が出ない。
grep -q '^\. /opt/muos/script/var/func\.sh' "$LAUNCH" ||
	fail "$LAUNCH" "D-3 '. /opt/muos/script/var/func.sh' が無い"
grep -q 'SETUP_APP' "$LAUNCH" || fail "$LAUNCH" "D-4 SETUP_APP の呼び出しが無い"

# D-5: ストレージのハードコード禁止 (SPEC 9.2/12/13)。コメント行は除いて見る。
#      マウントポイントは GET_VAR device storage/*/mount から引くこと。
if grep -v '^[[:space:]]*#' "$LAUNCH" | grep -qE '/mnt/mmc|/mnt/sdcard|/run/muos/storage'; then
	fail "$LAUNCH" "D-5 ストレージパスをハードコードしている(GET_VAR device storage/*/mount を使うこと)"
fi

# D-6: '# ICON:' の値と同梱グリフのファイル名の照合。ずれるとアプリ一覧で
#      アイコンが出ない/グリッドが空セルになるが、実機に持って行くまで
#      気づけない種類のバグなので機械的に潰す。
icon=$(sed -n 's/^# ICON: \(.*\)$/\1/p' "$LAUNCH" | head -1)
if [ -z "$icon" ]; then
	fail "$LAUNCH" "D-6 '# ICON:' の値を読めない"
else
	for d in glyph grid; do
		printf '%s\n' "$entries" | grep -qx "muChip/$d/$icon.png" ||
			fail "$OUT" "D-6 '# ICON: $icon' に対応する muChip/$d/$icon.png が無い"
	done
fi

# --- E. mux_lang.ini --------------------------------------------------------

LANG_INI=packaging/muChip/mux_lang.ini
for sec in full grid help; do
	grep -q "^\[$sec\]$" "$LANG_INI" || fail "$LANG_INI" "E-1 [$sec] セクションが無い"
done
# muOS の既定言語。frontend/module/muxapp.c が言語名で引くので English は必須。
[ "$(grep -c '^English=.' "$LANG_INI")" = "3" ] ||
	fail "$LANG_INI" "E-2 English= の行が3つ([full]/[grid]/[help])揃っていない"

if [ "$fails" -gt 0 ]; then
	echo "$fails 件の検査に失敗しました" >&2
	exit 1
fi
echo "test_package: OK (PROJECT_VERSION=$CMAKE_VERSION)"
exit 0

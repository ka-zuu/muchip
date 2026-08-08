#!/bin/sh
#
# muOS のアプリケーションランチャ。.muxapp の中では muChip/mux_launch.sh に置かれ、
# muxfrontend (MustardOS/internal script/mux/frontend.sh) から
#
#     "$RUN_APP"/mux_launch.sh "$RUN_APP"
#
# の形で呼ばれる。busybox ash で動く POSIX sh に留めること。
#
# 直下の3行コメントは muxfrontend がメタデータとしてパースする。書式は
# frontend/common/fileio.c の get_script_value() が "# KEY: " の完全前方一致で
# 見るため、'#' の後の空白1つ・':' の後の空白1つが必須。ずれると無言で
# フォールバックされる(アイコンが出ない等)。
#   HELP: mux_lang.ini が無い場合のヘルプ文
#   ICON: glyph/<ICON>.png と grid/<ICON>.png を引くためのキー
#   GRID: グリッド表示時の短い名前

# HELP: A chiptune music player (GBS/NSF) with full sub-track and M3U support.
# ICON: muchip
# GRID: muChip

# --- アプリケーションディレクトリ -------------------------------------------
# muxfrontend は $1 にアプリディレクトリ(bind mount 済みの
# /run/muos/storage/application/muChip)を渡してくる。SSH からの手動実行や
# muOS 側の実装変更に備えて $0 からも導出できるようにしておく。
# /mnt/mmc や /mnt/sdcard は絶対にハードコードしない (SPEC 12/13。
# SD2 搭載機やストレージ構成の異なる機種で壊れる)。
APP_DIR="${1:-$(cd -- "$(dirname -- "$0")" && pwd)}"
cd "$APP_DIR" || exit 1

# 出力をすべて log.txt に落とす。muOS はアプリの出力を残してくれないので、
# 実機のトラブルシュートはこのファイルが唯一の手がかりになる。
# func.sh を読む前にリダイレクトしておくと func.sh 自体の失敗も拾える。
exec >"$APP_DIR/log.txt" 2>&1
echo "=== muChip mux_launch.sh ==="
date
echo "APP_DIR=$APP_DIR"

# --- muOS 共通関数 (この行は絶対に削除しない) -------------------------------
# CPU ガバナ・HOME/XDG_CONFIG_HOME・XDG_RUNTIME_DIR/PIPEWIRE_RUNTIME_DIR・
# SDL 環境変数・SD1/SD2 判定をすべてここが行う。実機のオーディオは
# ALSA -> PipeWire 経由なので XDG_RUNTIME_DIR が無いと音が出ない。
. /opt/muos/script/var/func.sh

# SETUP_APP の第1引数は「pidof で引けるプロセス名」でなければならない。
# muOS の script/mux/quit.sh が
#   pidof "$(GET_VAR system foreground_process)" -> SIGTERM -> (3秒後) SIGKILL
# の順で終了させるため、実バイナリ名と一致していないとスリープ・電源断で
# プロセスが残る。バイナリは bin/muchip なのでプロセス名は "muchip"。
#
# 第2引数は gamecontrollerdb のレイアウト強制指定 ("retro"|"modern"|"")。
# 空文字にすると muOS 側のユーザー設定 (settings/remap/layout) に従う。
# retro と modern の差は A/B・X/Y の入れ替えだけで、muchip は SDL_GameController の
# 論理ボタン(SDL_CONTROLLER_BUTTON_*)で入力を解釈するため、ユーザーが muOS 全体で
# 選んでいるレイアウトをそのまま尊重するのが正しい。
# (SPEC 9.2 はコードブロックが "retro"、散文が "" と自己矛盾している。"" を採る)
APP_BIN="muchip"
SETUP_APP "$APP_BIN" ""

# SETUP_APP -> SETUP_SDL_ENVIRONMENT が SDL_GAMECONTROLLERCONFIG_FILE を既に
# export しているはずだが、muOS 側の実装が変わった場合に備えて補う。
# (config.ini の [input] gamecontroller_db でも同じことができる二重化)
[ -n "${SDL_GAMECONTROLLERCONFIG_FILE:-}" ] ||
	export SDL_GAMECONTROLLERCONFIG_FILE="/usr/lib/gamecontrollerdb.txt"

# --- 低バッテリー判定のしきい値 (Issue #7) -----------------------------------
# 画面右上のバッテリーゲージを「残量が少ないときだけ」表示する設定
# ([ui] battery_show = low)向けのしきい値。muOS 側に設定があればそれを
# 尊重し、無ければ muchip の既定(10%)に任せる(battery_low_threshold_from_env,
# src/battery.c)。
#
# muOS のどのキーがこれに当たるかはバージョン・機種で変わりうるので、候補を
# 順に試し、"1..99 の整数" が返ったものだけを採用する。GET_VAR は未知の
# キーに対して空文字・エラー・非0終了のいずれを返してもよい($( ) の中なので
# 途中で exit してもこのスクリプトは死なない)。全候補の結果を必ず log.txt に
# 残す -- 実機で「どのキーが生きているか」を知る唯一の手段であり、初回の
# 実機起動でここを見て候補リストを1行に絞り込む想定。
BATT_LOW=""
for PROBE in device:battery/low \
             global:settings/general/low_battery \
             global:settings/power/low_battery; do
	G=${PROBE%%:*}
	K=${PROBE#*:}
	V=$(GET_VAR "$G" "$K" 2>/dev/null)
	echo "battery: probe GET_VAR $G $K -> '$V'"
	case "$V" in
	'' | *[!0-9]*) continue ;;
	esac
	if [ "$V" -ge 1 ] && [ "$V" -le 99 ]; then
		BATT_LOW="$V"
		break
	fi
done
if [ -n "$BATT_LOW" ]; then
	echo "battery: low threshold ${BATT_LOW}% (muOS)"
	export MUCHIP_BATTERY_LOW_PCT="$BATT_LOW"
else
	echo "battery: muOS 側にしきい値の設定が無いため、muchip の既定(10%)に任せる"
fi

# SD が exFAT なら mount オプション由来で常に 0755 に見えるため実質 no-op だが、
# ext4 の SD へ手で展開されたケースのために実行権限を保証しておく。
chmod +x "$APP_DIR/bin/muchip" 2>/dev/null

# lib/ は現状同梱していない (SDL2 だけが実機の動的ライブラリで、libgme/miniz は
# 静的リンク、libstdc++/libgcc も -static-* 済み。SPEC 9.1 からの逸脱)。
# 将来同梱するものが出たときのためにパスだけ通しておく。
[ -d "$APP_DIR/lib" ] &&
	export LD_LIBRARY_PATH="$APP_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# --- 音楽ディレクトリの自動検出 ---------------------------------------------
# マウントポイントは必ず device 設定から引く(ハードコード禁止)。探索順は
# muOS 自身のストレージ選択に合わせて SD2 -> SD1 -> USB。
#
# 注: /run/muos/storage/music (= <SD>/MUOS/music) は muOS フロントエンドの
# BGM(.ogg) 置き場であってユーザーの音楽ライブラリではない。muchip は ogg を
# 再生できないので候補に入れない。
ROM_MOUNT="$(GET_VAR device storage/rom/mount)"
SD_MOUNT="$(GET_VAR device storage/sdcard/mount)"
USB_MOUNT="$(GET_VAR device storage/usb/mount)"

MOUNTS=""
[ "$(GET_VAR device storage/sdcard/active)" = "1" ] && [ -n "$SD_MOUNT" ] &&
	MOUNTS="$MOUNTS $SD_MOUNT"
[ -n "$ROM_MOUNT" ] && MOUNTS="$MOUNTS $ROM_MOUNT"
[ "$(GET_VAR device storage/usb/active)" = "1" ] && [ -n "$USB_MOUNT" ] &&
	MOUNTS="$MOUNTS $USB_MOUNT"

# 各マウント下を上から順に探し、最初に見つかったものを使う。muOS のコンテンツ
# ルートは <mount>/ROMS。SD は通常 exFAT (大小文字を区別しない) なので
# MUSIC/Music は同じものにマッチするが、ext4 の SD を使う人のために両方書く。
# 将来「メディアライブラリのディレクトリ指定」を実装したら、この一覧は
# config.ini 側へ移す。
SEARCH_SUBDIRS="MUSIC Music ROMS/GBS ROMS/MUSIC ROMS/Music ROMS"

START_DIR=""
for M in $MOUNTS; do
	for S in $SEARCH_SUBDIRS; do
		if [ -d "$M/$S" ]; then
			START_DIR="$M/$S"
			break
		fi
	done
	[ -n "$START_DIR" ] && break
done

# 見つからなければ SD1 のルート、それも駄目ならアプリ自身のディレクトリ。
# Browser さえ開けば実機上でどこへでも辿れるので、致命的な失敗にはしない。
[ -n "$START_DIR" ] || START_DIR="$ROM_MOUNT"
[ -d "$START_DIR" ] || START_DIR="$APP_DIR"
echo "start dir: $START_DIR"

# 自動検出した値は「保存された last_path がまだ無いときの初期値」として渡す。
# --start-dir だと config.ini の last_path (F-13) より優先されてしまい、
# 前回の続きから開けなくなるため、優先度の低い環境変数で渡す。
# (優先度: --start-dir > last_path > MUCHIP_START_DIR > ".")
export MUCHIP_START_DIR="$START_DIR"

# --- 起動 -------------------------------------------------------------------
# config.ini は APP_DIR に置く。config.c の config_resolve_path() は引数も
# 環境変数も無ければ "./config.ini" を見る(ここで cd 済み)ので既定でも同じ
# 場所になるが、意図を固定するため明示的に渡す。
./bin/muchip --config "$APP_DIR/config.ini"
RC=$?
echo "muchip exited with $RC"
exit "$RC"

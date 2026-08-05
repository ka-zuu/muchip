# muGBS

muOS 向けの GBS (Game Boy Sound System) プレーヤー。サブトラック構造と拡張M3U
（曲名・曲長・ループ指定）を正しく扱う。詳細仕様は [`SPEC.md`](./SPEC.md)、
実装進捗は [`PLAN.md`](./PLAN.md) を参照。

現在のスコープは **コア再生エンジン + GUI + 設定ファイル + 解像度非依存化
（P0〜P6）**。SDL2 の Browser/Player/TrackList/Settings 画面をホスト上で
キーボード操作から通しで確認でき、`config.ini` の読み書き（終了時オート
セーブ・直近パスの記憶）にも対応した。CLI ハーネス（`--list`/`--cli`。
CI・自動検証用）も引き続き使える。実機向けのクロスビルド・パッケージング
自体（P7、クロスビルド手順自体は下記の通り確立済み）は別プランで扱う。

## ビルド（ホスト / 開発機）

前提パッケージ（Ubuntu/Debian系）:

```sh
sudo apt update && sudo apt install -y \
    pkg-config libsdl2-dev libsdl2-ttf-dev cmake build-essential git
```

初回のみ submodule を取得:

```sh
git submodule update --init --recursive
```

ビルド:

```sh
./scripts/build-host.sh
# または直接:
cmake -B build -DTARGET_HOST=ON
cmake --build build -j
```

テスト:

```sh
ctest --test-dir build --output-on-failure
```

実行:

```sh
./build/mugbs --list Game.gbs      # プレイリストを列挙するだけ（無音）
./build/mugbs --cli Game.gbs       # 1トラック目を再生
```

## GUI (Browser / Player / TrackList / Settings)

引数無し、または `--list`/`--cli` 以外の起動でGUI本体が立ち上がる。

```sh
./build/mugbs                       # カレントディレクトリのBrowserから開始
                                     # (config.iniのlast_pathがあればそこから。F-13)
./build/mugbs --start-dir /path/to/music
./build/mugbs Game.gbs              # 指定ファイルを直接Playerで開いて開始
./build/mugbs --window 720x720      # ホストでの別解像度レイアウト確認用
                                     # (省略時は検出した解像度でフルスクリーン)
./build/mugbs --config /path/to/config.ini  # 設定ファイルの場所を明示する
                                     # (省略時は環境変数MUGBS_CONFIG、
                                     # 無ければ ./config.ini)
```

キーボード操作（実機ではSDL_GameControllerのボタンに対応。SPEC 6.3参照）:

| キー | Browser | Player | TrackList | Settings |
|---|---|---|---|---|
| `↑` `↓` | カーソル移動 | 音量 +/- | カーソル移動 | 項目選択 |
| `←` `→` | ページ送り | シーク -5s/+5s | ページ送り | 値を増減 |
| `Z` (A相当) | 開く | 再生/一時停止 | ジャンプ再生 | 値を増やす |
| `X` (B相当) | 上の階層へ | Browserへ戻る | Playerへ戻る | 保存して戻る |
| `A` (X相当) | — | TrackListを開く | Playerへ戻る | — |
| `S` (Y相当) | — | リピートモード切替 | — | — |
| `Q`/`W` (L1/R1) | — | 前/次トラック | — | — |
| `1`/`2` (L2/R2) | — | 前/次ファイル | — | — |
| `Return` (Start相当) | Settingsを開く | Settingsを開く | — | 保存して戻る |
| `Esc` | 終了 | 終了 | 終了 | — |

Settings 画面は Browser/Player どちらからも Start で開ける（SPEC 6.3の表は
Player限定だが、ファイルを開くまで設定に入れないのは初回体験が悪いため
意図的に広げてある。`PLAN.md` 参照）。Volume・Repeat・Stereo depth・
Default length・Fade・Show all files の6項目を編集でき、抜けるときと
アプリ終了時に `config.ini` へ自動保存する（`sample_rate` と P8予定の
EQ/チャンネルミュートは対象外）。

実機の物理ボタンでの終了は **GUIDEボタン単体、または Start+Select 同時押し**
（SPEC 6.3「Menu長押し=終了」の代替。GUIDEがmuOS側のオーバーレイに
吸われて届かない場合の保険として両方実装してある）。

文字描画は外部フォントライブラリを使わず、内蔵のビットマップフォント
(`vendor/font8x8`) を使う。実機の `sysroot/` には SDL2 の `.so` しか
含まれておらず、SDL2_ttf が実機に存在するか未確認のため、新規の実行時
依存を増やさない選択をしている。UI文言は英語のみ対応（GBSのメタデータは
basic latin 以外は `?` にフォールバックする）。

## 設定ファイル (config.ini, P6)

SPEC 7 の全キーに加え、`[ui] show_all_files`/`last_path`（F-13）と
`[input] gamecontroller_db`/`controller_mapping` を持つ。`src/config.c`
が読み書きする（外部のINIライブラリは使わない。SPEC 12）。

- パス解決順: `--config PATH` > 環境変数 `MUGBS_CONFIG` > `./config.ini`
  （`mux_launch.sh` は起動前に `cd "$APP_DIR"` するため、実機では
  結果的に SPEC 9.1 の `<APP_DIR>/config.ini` と一致する。SPEC 7 が
  例示する絶対パスはSPEC 12/13のハードコード禁止に反するため採らない）
- 保存は正規形で書き直す（手書きしたコメントや並び順は保存されない。
  値そのものは保持される）
- `--duration`/`--fade-ms`/`--repeat` のいずれかをCLIで指定した場合、
  一回きりのテスト用オーバーライドを永続化しないよう終了時の自動保存を
  無効化する

## 入力 (P6): gamecontrollerdb 連携

当初はSPEC 6.3の「GameControllerとして認識されないJoystickへの
フォールバック」を自前実装する計画だったが、実際に公開されている
muOS向けアプリ（[XMPlayer](https://github.com/atalaygrgn/XMPlayer)
v0.2.1）の `.muxapp` を展開して調べたところ、そのような自前実装は
していなかった。muOSは `/usr/lib/gamecontrollerdb.txt`
（`retro.txt`/`modern.txt` へのシンボリックリンク）を実機に同梱しており、
`mux_launch.sh`/`func.sh` が起動時に `SDL_GAMECONTROLLERCONFIG_FILE`/
`SDL_GAMECONTROLLERCONFIG` を export するだけで物理ボタンが
`SDL_GameController` として認識される。実機（muOS 2601.0 JACARANDA）で
この環境変数を手動再現してクロスビルド済みバイナリを実行し、実際に
`GameControllerを検出しました: muOS-Keys` を確認した（詳細はPLAN.md）。

そのため `src/input.c` は生Joystickイベントを自前解釈せず、代わりに
`config.ini` の `[input] gamecontroller_db`/`controller_mapping` から
`SDL_GameControllerAddMappingsFromFile()`/`AddMapping()` を呼ぶ経路のみを
持つ（`mux_launch.sh` を経由しない開発時や、DBに載っていない機種向けの
上書き手段）。GameControllerとして認識されなかったJoystickは、
名前・GUID・ボタン/軸/ハット数をログに出すだけに留める。

## 実機（muOS）向けクロスビルドについて【実機で検証済み】

muOS 2601.0 (JACARANDA) 実機（Cortex-A53 aarch64）で、クロスビルドした
`mugbs` が実際に映像・音声とも正常動作することを確認済み。GUI
(Browser/Player画面) も実機の`mali`ドライバ上での描画を
スクリーンショットで確認済み。P6では物理ボタンが`SDL_GameController`
として認識されること（`GameControllerを検出しました: muOS-Keys`）も
実機で確認した。詳細な調査経緯は [`PLAN.md`](./PLAN.md) の
「P7準備メモ: SDL2の扱いに関する調査」節を参照。

**未確認（P7で対応）:** SSHから直接バイナリを起動する検証方法では
`SETUP_APP`（`func.sh`）を経由しないため `foreground_process` が
`muxfrontend` のままになり、muOS側のUIがフロントに残って物理ボタンでの
対話操作ができない（画面が薄く重なって描画される）。TrackList画面の
実機確認、および物理ボタンでのBrowser/Player/Settings操作・終了の
実地検証は、`.muxapp` 化して `mux_launch.sh` 経由で正式に起動できる
ようになってから（P7）行う。

muOS の SDL2 は独自のビデオドライバ（Allwinner H700系デバイス共通のMali
GPU直結フレームバッファドライバ `mali`）を内蔵しており、**Debian の
`libsdl2-dev:arm64` でビルドしたバイナリは実機で正しく動かない**
（依存する X11/Wayland/PulseAudio 等が実機に存在しないため、そもそも
ロードに失敗する）。

さらに実機の glibc (2.38) は Debian bullseye のクロスツールチェインが
持つ glibc (2.31) より新しく、素朴にリンクすると実機SDL2が要求する
新しいシンボルが解決できない。**Debian の `crossbuild-essential-arm64`
は `--sysroot` フラグを無視し、常に `/usr/aarch64-linux-gnu` を
sysrootとして使う**ため、`CMAKE_SYSROOT` の指定だけでは機能しない。

手順:

1. 実機からSSH/SCPで `sysroot/` を構成する（SDL2のバージョンを実機の
   `.so` から自動検出し、対応する upstream SDL2 のヘッダも取得する）
   ```sh
   ./scripts/fetch-sysroot.sh root@<実機のIP>
   ```
2. `docker/Dockerfile` がビルド時に `sysroot/` の内容を
   `/usr/aarch64-linux-gnu/{lib,include/SDL2}` へ上書きコピーする
   ```sh
   docker build -f docker/Dockerfile -t mugbs-crossbuild .
   ```
3. クロスビルド（`cmake/toolchain-aarch64.cmake` はコンパイラ指定のみ。
   SDL2は `/usr/aarch64-linux-gnu/include/SDL2` を直接参照する）
   ```sh
   docker run --rm -v "$(pwd):/work" -w /work mugbs-crossbuild bash -c '
     cmake -B build-aarch64 -DTARGET_HOST=OFF -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
     cmake --build build-aarch64 -j$(nproc)
   '
   ```
4. 実機へ転送して実行する。`mux_launch.sh` 経由なら `func.sh` が
   `XDG_RUNTIME_DIR`/`PIPEWIRE_RUNTIME_DIR` を自動でexportするが、
   SSH生シェルから直接実行する場合は手動でexportが必要
   （実機のオーディオはPipeWire経由のため）
   ```sh
   scp build-aarch64/mugbs root@<実機のIP>:/root/
   ssh root@<実機のIP> 'export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; /root/mugbs --cli Game.gbs'
   # GUIを実機のボタンで確認する場合(P6): SDL_GameControllerとして認識させるため
   # func.shが export する2つの環境変数を手動で再現する(値は実機の
   # /usr/lib/gamecontrollerdb.txt から「muOS-Keys」等のデバイス名で引く)。
   # ただしSETUP_APPを経由しないため muxfrontend がフロントに残ったままになり、
   # 実際の対話操作はできない(上記「未確認」参照。.muxapp化後のP7で確認する)。
   ssh root@<実機のIP> '. /opt/muos/script/var/func.sh; \
     export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; \
     export SDL_GAMECONTROLLERCONFIG_FILE=/usr/lib/gamecontrollerdb.txt; \
     export SDL_GAMECONTROLLERCONFIG="$(grep "$(GET_VAR device sdl/name)" "$SDL_GAMECONTROLLERCONFIG_FILE")"; \
     /root/mugbs --config /root/config.ini --start-dir /mnt/mmc/MUSIC'
   ```

`sysroot/` はバイナリを含むため git 管理しない（`.gitignore` 済み）。
再現性は `scripts/fetch-sysroot.sh` の再実行に依存する。

muxappパッケージング（`mux_launch.sh` の実配置、`.muxapp` 化）自体は
まだ未着手（P7本格着手時に対応）。

## ライセンス / 同梱ソースについて

- `vendor/game-music-emu`（libgme）: git submodule。LGPL/GPL（同梱の
  `license.txt` / `license.gpl2.txt` を参照）。GBS デコードと拡張M3U解析を
  委譲している。自前で GB APU は実装していない。
- `vendor/miniz`: MIT ライセンス。zip 展開に使用（P4 以降）。
  https://github.com/richgel999/miniz より split-file ソースを vendoring。
- `vendor/font8x8`: パブリックドメイン。UI (P5) の文字描画に使用。
  https://github.com/dhepper/font8x8 より `font8x8_basic.h`
  （basic latin, U+0000-U+007F）のみを vendoring。
  オリジナルは Marcel Sondaar / IBM の public domain VGA フォントを
  Daniel Hepper が整理したもの。

## 依存関係とアーキテクチャ上の注意

- `gme_play()` の第2引数は **ステレオインタリーブされた `short` の個数**
  であり、フレーム数でもバイト数でもない。SDL オーディオコールバックが
  渡す `len`（バイト数）は `len / sizeof(short)` で変換する。
  `len / 4` を渡すと倍速再生になる（`src/audio.c` 参照）。
- `gme_*` API はスレッドセーフでない。オーディオコールバックとメインスレッド
  の両方から `Music_Emu*` に触るため、`SDL_LockAudioDevice()` /
  `SDL_UnlockAudioDevice()` で保護する。
- 画面座標・文字サイズは `src/ui.c` の `ui_metrics_t`（`scale = min(w/640,
  h/480)` から導出）経由でのみ扱い、直接ハードコードしない（SPEC 6.2, 13）。
- GameController のボタン判定は `SDL_CONTROLLER_BUTTON_*` の論理名のみを
  使い、生のボタン番号を決め打ちしない（`src/input.c`。SPEC 13）。
- `config.ini` の設定は実行中プログラム全体で `main()` が持つ唯一の
  インスタンスだけが権威を持つ（`app_t`/`player_t` はポインタで参照する
  だけでコピーを持たない。P5までは3箇所に値コピーされ食い違う問題が
  あった。`src/app.c`/`src/player.c`）。
- `strtod()`/`printf("%f")` はロケール依存のため、`config.c` は
  `setlocale()` を一切呼ばない前提（常に `"C"` ロケール）で
  `stereo_depth` 等の小数値を読み書きする。

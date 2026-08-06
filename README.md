# muGBS

muOS 向けの GBS (Game Boy Sound System) プレーヤー。サブトラック構造と拡張M3U
（曲名・曲長・ループ指定）を正しく扱う。詳細仕様は [`SPEC.md`](./SPEC.md)、
実装進捗は [`PLAN.md`](./PLAN.md) を参照。

**P0〜P10 完了（v1.0.0 + 実機フィードバック対応）**。SPEC の MUST 要件
（F-01〜F-08）に加え、SHOULD/NICE 要件のうち F-14 ビジュアライザ・F-20 EQ・
F-25 シャッフル再生を実装済み（F-10 チャンネルミュートは実装した上で
不要と判断し削除した。`PLAN.md` 参照）。
SDL2 の Browser/Player/TrackList/Settings 画面をホスト上でキーボード操作から
通しで確認でき、`config.ini` の読み書き（終了時オートセーブ・直近パスの
記憶）にも対応する。CLI ハーネス（`--list`/`--cli`。CI・自動検証用）も
引き続き使える。`.muxapp` へパッケージして実機の Archive Manager から
インストールし、物理ボタンだけで Browser→Player→TrackList→Settings→終了
まで一巡できることを実機で確認済み（下記「muOS へのインストール」参照）。

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
| `↑` `↓` | カーソル移動 | ファイル一覧のカーソル移動 | カーソル移動 | 項目選択 |
| `←` `→` | ページ送り | 前/次トラック | ページ送り | 値を増減 |
| `Z` (A相当) | 開く | 決定(カーソルのファイルを開く) | ジャンプ再生 | 値を増やす |
| `X` (B相当) | 上の階層へ | Browserへ戻る | Playerへ戻る | 保存して戻る |
| `A` (X相当) | — | TrackListを開く | Playerへ戻る | リセット確認ダイアログ |
| `S` (Y相当) | — | (単体では未使用。下記「Yコンボ」参照) | — | — |
| `Q`/`W` (L1/R1) | — | シーク -5s/+5s | — | — |
| `1`/`2` (L2/R2) | — | 前/次ソース | — | — |
| `Return` (Start相当) | Settingsを開く | Settingsを開く | — | 保存して戻る |
| `Space` (Select相当) | — | 再生/一時停止 | — | — |
| `Esc` | 終了 | 終了 | 終了 | — |

`↑↓←→` は押しっぱなしで長押しリピートする(初回350ms後から70ms間隔)。
GameController(実機の物理ボタン)はキーボードと違いOSレベルのキー
リピートを持たないため、`src/input.c` が自前で追跡している。
リスト系画面(Browser/TrackList/Settings/Playerのファイル一覧)のカーソルは
端で反対側へ折り返す。ページ送り(Browserの`←``→`)は折り返さない。

Player画面の中央には、いま開いているファイルが置かれているディレクトリの
ファイル一覧が出る。カーソル(青)を`↑``↓`で動かして`Z`で決定すると、そのまま
別のファイルへ切り替えて再生できる(再生中のファイルは黄色で表示される)。
`1`/`2` の「前/次ソース」はこれとは別で、いま開いている m3u や zip の中で
参照先ファイルを跨ぐ移動。

**Yコンボ(P11)**: Player画面で `S`(Y相当)を押しながら方向キーを押すと、
Settingsへ入らずにRepeat/Shuffleを変えられる。`S`+`←`/`→` でRepeatモードを
1段ずつ進める/戻す(none→one→allの順で循環)、`S`+`↑`/`↓` でShuffleを
明示的にon/off(トグルではない)。`S`単体(押して離すだけ)は何もしない。
ステータス行(`repeat:xxx shuffle:on/off`)ですぐ確認できる。

音量調整機能は無い(常に最大出力)。本体側のハードウェア音量と非連動で
紛らわしいため廃止した。

Settings 画面は Browser/Player どちらからも Start で開ける（SPEC 6.3の表は
Player限定だが、ファイルを開くまで設定に入れないのは初回体験が悪いため
意図的に広げてある。`PLAN.md` 参照）。Repeat・**Shuffle**・Stereo depth・
**EQ bass**・**EQ treble**・Default length・Fade・Show all files の8項目を
編集でき、抜けるときとアプリ終了時に `config.ini` へ自動保存する
（`sample_rate` はデバイス再オープンが必要なため対象外）。`X`（キーボードは
`A`）でこれら8項目を一括で既定値に戻す確認ダイアログを開ける
（`last_path`やコントローラ設定など、Settings画面に出てこない値は対象外）。

Shuffle を有効にすると、次/前トラック（自動送りも含む）がランダムな順で
進む。1周（全エントリを1回ずつ）したら Repeat が `all` なら次の周のために
並びを作り直し、`none`なら停止する（順送りのときと同じ考え方）。
`Repeat: one` はシャッフルより優先し、常に同じトラックを繰り返す。

実機の物理ボタンでの終了は **GUIDEボタン単体、または Start+Select 同時押し**
（SPEC 6.3「Menu長押し=終了」の代替。GUIDEがmuOS側のオーバーレイに
吸われて届かない場合の保険として両方実装してある）。

文字描画は外部フォントライブラリを使わず、内蔵のビットマップフォント
(`vendor/font8x8`) を使う。実機の `sysroot/` には SDL2 の `.so` しか
含まれておらず、SDL2_ttf が実機に存在するか未確認のため、新規の実行時
依存を増やさない選択をしている。UI文言は英語のみ対応（GBSのメタデータは
basic latin 以外は `?` にフォールバックする）。

### ビジュアライザ (F-14)

Player 画面にはシークバーの下に簡易オシロスコープを表示する。
libgme の公開 C API にはチャンネル別の PCM を取り出す手段が無いため
（`gme_new_emu_multi_channel()` 以外に無く、それは `gme_open_*` と両立
しない）、SPEC F-14 が許容する「波形」の方を採った。詳しくは `PLAN.md`。

### EQ (F-20)

`config.ini` の `eq_bass` / `eq_treble` は **-100〜100 の対称なノブ**で、
0 が GBS の既定の音（libgme の `treble = -1.0 dB`, `bass = 120 Hz`）に
一致する。libgme の `bass` は「低音が落ち始める周波数」で値が大きいほど
低音が減るため、ノブとは向きが逆になる（`src/eq.c` が変換する）。
`+` 方向がそれぞれ「低音が増える」「高音が増える」で直感どおりに動く。

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
`mugbs` が実際に映像・音声とも正常動作することを確認済み。`.muxapp` に
パッケージして実機の Archive Manager からインストールし、
`mux_launch.sh` 経由の正式起動で、物理ボタンだけで
Browser→ファイルを開く→Player→TrackList→トラックジャンプ→Player→
Settingsで値変更→GUIDE単体ボタンで終了→再起動→前回の続きから復元、
という一連の操作が実機で完結することを確認した（P7）。詳細な調査経緯・
発見事項は [`PLAN.md`](./PLAN.md) の「P7の設計判断」節を参照。

P5/P6の時点では SSH から直接バイナリを起動する検証方法で `SETUP_APP`
（`func.sh`）を経由しないため `foreground_process` が `muxfrontend` の
ままになり、muOS側のUIがフロントに残って物理ボタンでの対話操作ができない
という制約があった。`.muxapp` 化して `mux_launch.sh` 経由で正式に起動する
ことでこの問題が解消することを実機で確認済み。

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
   `.so` から自動検出し、対応する upstream SDL2 のヘッダも取得する）。
   初回のみ。実機のSDL2やlibcが変わったときだけ再実行する
   ```sh
   ./scripts/fetch-sysroot.sh root@<実機のIP>
   ```
2. クロスビルドする
   ```sh
   ./scripts/build-aarch64.sh                  # -> build-aarch64/mugbs
   ./scripts/build-aarch64.sh --rebuild-image  # sysroot/ を更新したとき
   ```
   このスクリプトは Docker イメージが無ければ自動で
   `docker build -f docker/Dockerfile -t mugbs-crossbuild .` を実行する。
   `docker/Dockerfile` がビルド時に `sysroot/` の内容を
   `/usr/aarch64-linux-gnu/{lib,include/SDL2}` へ上書きコピーし、
   `cmake/toolchain-aarch64.cmake` はコンパイラ指定だけを行う
   （SDL2は `/usr/aarch64-linux-gnu/include/SDL2` を直接参照する）。

   `docker run` は呼び出しユーザーの uid:gid で実行するため、
   `build-aarch64/` が root 所有にならない。ビルド後に `file` と
   `readelf -d` の `NEEDED` を表示するので、libstdc++ が動的リンクに
   紛れ込む回帰（実機で `GLIBCXX_3.4.xx not found` になる）にすぐ気づける。
   期待する `NEEDED` は `libSDL2-2.0.so.0` / `libm.so.6` / `libc.so.6` の3つだけ
3. `.muxapp` を作る（strip・バージョン付けまで自動。詳細は次節）
   ```sh
   ./scripts/package.sh    # -> ./muGBS-1.0.0.muxapp
   ```

`sysroot/` はバイナリを含むため git 管理しない（`.gitignore` 済み）。
再現性は `scripts/fetch-sysroot.sh` の再実行に依存する。

SDLウィンドウを開かないCLIハーネス（`--cli`）だけを単発で試したい場合は、
`mux_launch.sh` を経由せず直接転送して実行してもよい（実機のオーディオは
PipeWire経由のため `XDG_RUNTIME_DIR`/`PIPEWIRE_RUNTIME_DIR` の手動exportが
必要。`mux_launch.sh` 経由ならこれらは `func.sh` が自動で行う）:

```sh
scp build-aarch64/mugbs root@<実機のIP>:/root/
ssh root@<実機のIP> 'export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; /root/mugbs --cli Game.gbs'
```

## muOS へのインストール（P7、実機で検証済み）

```sh
scp muGBS-1.0.0.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
```

実機で **Applications > Archive Manager** から `muGBS-1.0.0` を選んで
展開するとインストールされる（`/run/muos/storage/application/muGBS/`
に配置される。実体の物理パスは機種のSD構成によって変わるため
`/mnt/mmc` 等をコードにハードコードしていない）。以後はアプリ一覧
（Applications）に「muGBS プレーヤー」がアイコン付きで表示され、
物理ボタンで起動できる。

`config.ini` はアプリディレクトリ直下に置かれ、終了時にオートセーブ
される（前回開いた場所・EQなどの設定が復元される。F-13）。
`mux_launch.sh` は起動のたびに実機のSDカードから音楽ディレクトリを
自動検出し（`MUSIC`/`Music`/`ROMS/GBS`等を優先的に探索、無ければ
`ROMS`直下にフォールバック）、`config.ini` にまだ `last_path` が
無い初回起動時だけそこから始まる。

終了は **GUIDEボタン単体**、または **Start+Select同時押し**（実機で
GUIDE単体を確認済み）。

### `.muxapp` を自分でビルドする

`packaging/muGBS/` にアプリ本体以外の資材（`mux_launch.sh`・
`mux_lang.ini`・`config.ini`・アイコン）が入っている。
`scripts/package.sh` がこれとクロスビルド済みバイナリを合わせて
`.muxapp`（実体はzip）を作る。

```sh
./scripts/build-aarch64.sh   # -> build-aarch64/mugbs
./scripts/package.sh         # -> ./muGBS-<version>.muxapp
```

バージョンは `CMakeLists.txt` の `project(mugbs VERSION x.y.z ...)`
が唯一の情報源（`./build/mugbs --version` でも確認できる）。
`scripts/package.sh --bin PATH` で任意のバイナリを、`--no-strip` で
strip無し生成も指定できる（`ctest -R test_package` が構造検証に使う）。

アイコン（`packaging/muGBS/{glyph,grid}/mugbs.png`）は
`tools/make_glyph.py`（Pillow使用）で生成したものをコミット済み。
図案を変えたいときだけ再実行する。

## ライセンス / 同梱ソースについて

- `vendor/game-music-emu`（libgme）: git submodule。LGPL/GPL（同梱の
  `license.txt` / `license.gpl2.txt` を参照）。GBS デコードと拡張M3U解析を
  委譲している。自前で GB APU は実装していない。

  **upstream ではなくフォーク https://github.com/ka-zuu/game-music-emu の
  `mugbs` ブランチを参照している。** P12 で `gme/Gbs_Emu.cpp` に独自パッチ
  （10進の m3u トラック番号を 0 始まりとして扱う。upstream は 1 始まり前提で
  1 を引くため、zophar.net 配布の GBS パックで 1 曲ずれる）を当てており、
  そのコミットは upstream に存在しないため。パッチの内容と実証手順は
  当該コミットのメッセージと `PLAN.md` の P12 の節を参照。
  upstream への追従は
  `git -C vendor/game-music-emu fetch upstream` から行う。
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

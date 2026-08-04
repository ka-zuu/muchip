# SPEC.md — muGBS: muOS向け GBS (Game Boy Sound System) プレーヤー

> このドキュメントは Claude Code に実装を依頼するための仕様書です。
> 実装者（Claude Code）は、まず本仕様を読んだ上で `PLAN.md` を作成し、
> フェーズ単位で実装・ビルド・コミットしてください。

---

## 1. 目的とスコープ

### 1.1 背景

`.gbs`（Game Boy Sound System）は、ゲームボーイのサウンドドライバとデータをそのまま格納した
サウンドファイル形式で、**1ファイルに複数トラック（サブソング）が含まれる**。

既存のレトロゲーム機用（muOS / PortMaster / RetroArch）音楽プレーヤーは、
このサブトラック構造と、それを補完する `.m3u`（拡張M3U）を正しく扱えないものが大半である。
具体的には以下の問題がある。

- 1ファイル=1曲として扱い、2曲目以降にアクセスできない
- `.m3u` を「ファイルのリスト」としてしか解釈せず、`::GBS,3,...` のサブトラック指定を無視する
- 曲の長さ情報を持たないため、ループ曲が永久に終わらず「次の曲」に進まない
- zip 圧縮されたリップ（GBS + m3u のセット）を展開できない

本プロジェクトは、これらを正しく処理する専用プレーヤーを muOS 上に実装する。

### 1.2 スコープ外（やらないこと）

- GB APU の自前エミュレーション実装（libgme に委譲する）
- GBS 以外の形式の**積極的な**サポート（ただし libgme が対応する NSF/SPC/VGM 等は
  「たまたま動く」状態で構わない。UI上で排除しない）
- ネットワーク機能、オンラインDB連携
- 楽曲のエンコード／エクスポート

---

## 2. ターゲット環境

| 項目 | 内容 |
|---|---|
| OS | muOS (MustardOS) `2601.0 JACARANDA` / `2606.0 ANDROMEDA` 以降 |
| アーキテクチャ | `aarch64` (arm64) をプライマリターゲット |
| 想定デバイス | Anbernic RG35XX H / RG35XX SP / RG40XX / RG CubeXX 等 |
| 解像度 | **決め打ち禁止**。640x480 / 720x720 / 1024x768 等が混在する |
| 音声出力 | SDL2 Audio |
| 入力 | SDL2 GameController / Joystick |

> **重要**: 解像度・ボタン配置はデバイスごとに異なる。実行時に取得して適応すること。

---

## 3. 機能要件

### 3.1 MUST（必須）

| ID | 要件 |
|---|---|
| F-01 | `.gbs` ファイルを再生できる |
| F-02 | `.gbs` 内の**全サブトラックを列挙し、任意のトラックを選択して再生できる** |
| F-03 | `.m3u`（拡張M3U）を読み込み、曲名・トラック番号・演奏時間を反映できる |
| F-04 | `.zip` 内の `.gbs` / `.m3u` を、展開せずメモリ上で扱える |
| F-05 | ファイルブラウザでSDカード上のファイルを選択できる |
| F-06 | 再生／一時停止／次トラック／前トラック／シークができる |
| F-07 | 曲長が判明している場合、終端でフェードアウトし自動的に次トラックへ進む |
| F-08 | 曲長が不明な場合、設定した既定時間（デフォルト150秒）で次へ進む |

### 3.2 SHOULD（推奨）

| ID | 要件 |
|---|---|
| F-10 | GB APU の 4ch（Pulse1 / Pulse2 / Wave / Noise）を個別にミュートできる |
| F-11 | リピートモード（1曲リピート / 全曲リピート / リピートなし）を切り替えられる |
| F-12 | 再生位置とトラック情報を画面表示する（曲名・作者・著作権・n/N） |
| F-13 | 直近に開いたファイルを記憶し、次回起動時に復元する |
| F-14 | 簡易ビジュアライザ（4chのボリュームバー or 波形） |

### 3.3 NICE TO HAVE

| ID | 要件 |
|---|---|
| F-20 | イコライザ（bass / treble）設定 |
| F-21 | ステレオ深度（`gme_set_stereo_depth`）調整 |
| F-22 | テンポ調整（`gme_set_tempo`） |
| F-23 | スリープタイマー |
| F-24 | 画面消灯状態でのバックグラウンド再生 |

---

## 4. アーキテクチャ

### 4.1 依存ライブラリ

| ライブラリ | 用途 | 入手 | リンク方法 |
|---|---|---|---|
| **libgme** (game-music-emu) | GBSデコード・m3u解析 | https://github.com/libgme/game-music-emu | **静的リンク** |
| **SDL2** | 音声出力・入力・描画 | muOS実機の `/usr/lib` から取得 | 動的リンク |
| **miniz** | zip展開 | 単一ファイルをvendoring | 静的（ソース同梱） |
| SDL2_ttf *(任意)* | フォント描画 | 実機 or 静的 | どちらでも |

> **libgme は C++ で書かれている。** クロスコンパイル時は必ず
> `-static-libstdc++ -static-libgcc` を付けること。付けないと実機で
> `GLIBCXX_3.4.xx not found` で起動しない。

### 4.2 モジュール構成

```
src/
├── main.c              # エントリポイント、メインループ
├── audio.c/.h          # SDL2オーディオコールバック、libgmeとの橋渡し
├── player.c/.h         # 再生状態機械（play/pause/next/prev/seek/fade）
├── playlist.c/.h       # プレイリスト構築（m3u解析・トラック列挙）
├── m3u.c/.h            # 拡張M3Uパーサ（マルチファイル対応）
├── archive.c/.h        # zip展開（miniz）
├── browser.c/.h        # ファイルブラウザUI
├── ui.c/.h             # 描画・レイアウト（解像度非依存）
├── input.c/.h          # SDL GameController抽象化
└── config.c/.h         # 設定の読み書き（INI形式）
vendor/
├── miniz/
└── game-music-emu/     # submodule
```

---

## 5. 中核ロジック仕様

### 5.1 libgme の使い方（最重要）

自前でGB APUを実装してはならない。以下のAPIに委譲する。

```c
#include <gme/gme.h>

Music_Emu* emu = NULL;

/* --- ファイルから開く --- */
gme_err_t err = gme_open_file(path, &emu, 44100);
if (err) { /* エラー表示 */ }

/* --- メモリから開く（zip内ファイル用） --- */
err = gme_open_data(buf, buf_len, &emu, 44100);

/* --- m3u をロード（★これが本プロジェクトの肝） --- */
/* libgme は拡張M3U（サブトラック指定・曲長）をネイティブに解釈し、
   トラック名と play_length を自動で埋めてくれる。自前パーサより優先すること。 */
gme_load_m3u(emu, m3u_path);
/* zip内なら: */
gme_load_m3u_data(emu, m3u_buf, m3u_len);

/* --- トラック列挙 --- */
int n = gme_track_count(emu);

gme_info_t* info;
gme_track_info(emu, &info, track_index);
/* info->game, info->song, info->author, info->copyright, info->system,
   info->comment, info->dumper,
   info->length      : 曲全体の長さ(ms)、不明なら -1
   info->intro_length: イントロ長(ms)
   info->loop_length : ループ長(ms)
   info->play_length : 推奨再生時間(ms) ← 通常これを使う */
gme_free_info(info);

/* --- 再生開始 --- */
gme_start_track(emu, track_index);

/* --- フェードアウト設定（F-07） --- */
if (play_length > 0)
    gme_set_fade(emu, play_length);   /* 引数はフェード開始時刻(ms) */
else
    gme_set_fade(emu, default_secs * 1000);

/* --- サンプル生成（オーディオコールバック内） --- */
gme_play(emu, sample_count, buffer);

/* --- 終了判定 --- */
if (gme_track_ended(emu)) { /* 次トラックへ */ }

/* --- シーク --- */
gme_seek(emu, msec);
gme_tell(emu);          /* 現在位置(ms) */

/* --- チャンネルミュート（F-10） --- */
int voices = gme_voice_count(emu);           /* GBSなら4 */
const char* name = gme_voice_name(emu, i);
gme_mute_voices(emu, mask);                  /* ビットマスク */

/* --- 音質オプション --- */
gme_enable_accuracy(emu, 1);
gme_set_stereo_depth(emu, 0.15);

/* --- 解放 --- */
gme_delete(emu);
```

#### ★ 落とし穴 1: `gme_play()` のサンプル数

`gme_play()` の第2引数 `count` は **「ステレオインターリーブされた `short` の個数」**であり、
フレーム数ではない。**必ず偶数**。

SDL2のオーディオコールバックは `len` を**バイト数**で渡してくるため：

```c
void audio_callback(void* userdata, Uint8* stream, int len) {
    /* len はバイト数。int16 の個数に変換する */
    int count = len / sizeof(short);   /* ← これが gme_play の count */
    gme_play(emu, count, (short*)stream);
}
```

`len / 4`（フレーム数）を渡すと**倍速再生になる**。ここは頻出バグなので注意。

#### ★ 落とし穴 2: スレッド安全性

`gme_*` はスレッドセーフではない。オーディオコールバックとUIスレッドの両方から
`emu` を触るため、**必ずミューテックス（`SDL_mutex`）で保護**すること。
特に `gme_start_track` / `gme_seek` / `gme_delete` はコールバック実行中に呼ばれてはならない。
`SDL_LockAudioDevice()` を使うのが最も安全。

#### ★ 落とし穴 3: `gme_open_data` はバッファをコピーしない場合がある

zip展開したバッファは、`gme_delete()` するまで**解放してはならない**。
所有権をプレーヤー側で保持すること。

---

### 5.2 拡張M3U の扱い（F-03）

#### 形式

GBS/NSF系の拡張M3Uは以下の形式を取る（1行1トラック）：

```
# コメント行
Game.gbs::GBS,1,Title Screen,0:32,,0:05
Game.gbs::GBS,$02,Overworld,2:34,2:34,0:08
Game.gbs::GBS,3,Battle,1:45
```

フィールド（カンマ区切り、空欄あり）:

```
<file>::<TYPE>,<track>,<title>,<time>,<loop>,<fade>,<artist>,<amp>
```

- `<track>` は10進または `$` 始まりの16進
- `<time>` は `m:ss.mmm` 形式。`-` はループ扱い
- 空欄は省略可

#### 実装方針

1. **原則として `gme_load_m3u()` / `gme_load_m3u_data()` に委譲する。**
   libgme のパーサは上記形式に対応済みで、`gme_track_info()` の
   `song` と `play_length` に反映される。自前パーサで再実装しないこと。

2. **ただし、m3u が複数の異なるファイルを参照する場合は libgme では扱えない。**
   そのため `m3u.c` に薄いプリパーサを置き、以下を行う：

   - m3u を1行ずつ読み、`::` の前のファイル名を抽出
   - 参照ファイルが**1種類のみ** → そのファイルを開いて `gme_load_m3u()` に丸投げ（推奨パス）
   - 参照ファイルが**複数** → 自前でプレイリストエントリ
     `{ file_path, track_index, title, play_length_ms }` を構築し、
     ファイルをまたぐたびに `Music_Emu` を開き直す

3. m3u が存在しない場合は、`gme_track_count()` で全トラックを列挙し、
   `Track 01`, `Track 02` ... と自動命名する。

4. `.gbs` を直接開いた場合、**同ディレクトリに同名の `.m3u` があれば自動で読み込む**。
   （例: `Game.gbs` → `Game.m3u`）

---

### 5.3 zip 対応（F-04）

`archive.c` に miniz ベースで実装する。

```
zip を開く
 ├─ 中央ディレクトリを列挙
 ├─ 拡張子で分類: .gbs/.gb/.nsf/.spc → 音楽, .m3u → プレイリスト
 ├─ .m3u が1つ以上ある場合
 │    └─ m3u をメモリ展開 → 参照される .gbs もメモリ展開
 │       → gme_open_data() + gme_load_m3u_data()
 └─ .m3u が無い場合
      └─ 音楽ファイルを列挙してユーザーに選択させる
         （1つだけなら即座に開く）
```

**要件**:
- 一時ファイルをディスクに書き出さない（SDカードの寿命と速度のため）
- zip内のパス区切り・大文字小文字の揺れを吸収して m3u の参照を解決する
  （m3u が `Game.gbs` と書いていて zip 内が `game.GBS` でも一致させる）
- 展開後サイズが 32MB を超えるエントリは拒否（メモリ保護）

---

### 5.4 再生状態機械（player.c）

```
STOPPED ──open──► LOADED ──play──► PLAYING ⇄ PAUSED
                                      │
                                      ├─ track_ended → next_track()
                                      └─ user next/prev → start_track()
```

- `next_track()` はリピートモード（F-11）を考慮する
  - `REPEAT_NONE`: 最終トラックで STOPPED
  - `REPEAT_ONE`: 同一トラックを再開
  - `REPEAT_ALL`: 先頭に戻る
- 曲送りの際は必ず `SDL_LockAudioDevice()` で保護
- フェード中（`gme_track_ended()` 直前）にユーザーが next を押した場合、即座に切り替える

---

## 6. UI仕様

### 6.1 画面

| 画面 | 内容 |
|---|---|
| **Browser** | ファイル一覧。ディレクトリ階層を辿る。`.gbs` `.m3u` `.zip` のみ表示（設定で全表示可） |
| **Player** | 曲名・ゲーム名・作者・著作権・トラック `n/N`・経過/全体時間・シークバー・4chメータ |
| **TrackList** | 現在のファイルの全トラック一覧。直接ジャンプ可能 |
| **Settings** | デフォルト曲長・リピート・ステレオ深度・EQ・チャンネルミュート |

### 6.2 レイアウト規則（解像度非依存）

- 起動時に `SDL_GetCurrentDisplayMode()` で解像度を取得
- 640x480 を基準とし、`scale = min(w/640.0, h/480.0)` でスケーリング
- フォントサイズ・余白・行高すべてを `scale` 倍する
- **座標のハードコード禁止**

### 6.3 入力（SDL GameController 準拠）

| ボタン | Browser | Player |
|---|---|---|
| D-Pad Up/Down | カーソル移動 | 音量 +/- |
| D-Pad Left/Right | ページ送り | シーク -5s / +5s |
| A | 決定・開く | 再生／一時停止 |
| B | 上の階層へ | Browserへ戻る |
| X | — | TrackList を開く |
| Y | — | リピートモード切替 |
| L1 / R1 | — | 前トラック / 次トラック |
| L2 / R2 | — | 前ファイル / 次ファイル |
| Start | — | Settings |
| Select | — | チャンネルミュート パネル |
| Menu長押し | 終了 | 終了 |

> muOS はデバイスごとにボタン配置が異なる。`SDL_GameControllerOpen()` を使い、
> 生のボタン番号を決め打ちしないこと。GameController として認識されない場合は
> `SDL_Joystick` にフォールバックし、`config.ini` でマッピングを上書き可能にする。

---

## 7. 設定ファイル

`/run/muos/storage/application/muGBS/config.ini`

```ini
[playback]
default_length_sec = 150   ; 曲長不明時の再生秒数
fade_length_ms     = 8000
repeat_mode        = all   ; none | one | all
sample_rate        = 44100

[audio]
stereo_depth = 0.15
eq_bass      = 0
eq_treble    = 0
volume       = 80

[ui]
show_all_files = false
last_path      = /mnt/mmc/MUSIC

[voices]
mute_mask = 0
```

---

## 8. ビルド

### 8.1 方針

- **実機ビルドはしない。** Docker上でクロスコンパイルする。
- glibc のバージョン差で起動しなくなる事故を避けるため、**古めのベースイメージ**を使う。
- ビルドシステムは **CMake**。

### 8.2 Dockerfile（骨子）

```dockerfile
FROM debian:bullseye

RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    crossbuild-essential-arm64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
```

### 8.3 SDL2 の扱い（重要）

muOS の SDL2 はデバイス固有のバックエンド（KMS/DRM, fbdev, 回転処理）を含むため、
**Debian の `libsdl2-dev:arm64` でビルドしたバイナリは実機で正しく動かない可能性が高い。**

推奨手順:

1. 実機に SSH で入り、以下を取得してホスト側 `sysroot/` に配置する
   ```
   /usr/lib/libSDL2-2.0.so*
   /usr/lib/libSDL2_ttf*.so*      (使う場合)
   /usr/include/SDL2/             (無ければ同バージョンのヘッダをGitHubから取得)
   ```
2. CMake の `CMAKE_SYSROOT` / `CMAKE_FIND_ROOT_PATH` にこれを指定
3. リンクは動的（実機の SDL2 をそのまま使う）

**この手順は README に必ず記載すること。** 他の人がビルドできない最大の原因になる。

### 8.4 CMake ツールチェーンファイル

```cmake
# toolchain-aarch64.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_SYSROOT      ${CMAKE_CURRENT_LIST_DIR}/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### 8.5 リンクフラグ

```cmake
target_link_options(mugbs PRIVATE
    -static-libstdc++
    -static-libgcc
)
target_link_libraries(mugbs PRIVATE gme_static SDL2 m)
```

libgme は `BUILD_SHARED_LIBS=OFF`, `ENABLE_UBSAN=OFF` でビルドし、
不要なエミュレータを削って軽量化してもよい（`USE_GME_GBS=ON` は必須）。

---

## 9. muOS パッケージング

### 9.1 ディレクトリ構成（`.muxapp` の中身）

`.muxapp` は拡張子を変えた **zip** であり、SDカードのルートを基準とした構造を持つ。

```
mnt/mmc/MUOS/application/muGBS/
├── mux_launch.sh
├── mux_lang.ini
├── bin/
│   └── mugbs              (実行ファイル / chmod +x)
├── lib/                   (静的リンクできなかった依存があれば)
├── assets/
│   └── font.ttf
├── glyph/
│   └── mugbs.png
└── config.ini
```

### 9.2 `mux_launch.sh`

現行の muOS（JACARANDA / ANDROMEDA）の作法に厳密に従う。

```sh
#!/bin/sh

# HELP: A proper GBS (Game Boy Sound System) music player with full sub-track and M3U support.
# ICON: mugbs
# GRID: muGBS

. /opt/muos/script/var/func.sh

APP_BIN="mugbs"
SETUP_APP "$APP_BIN" "retro"

# -----------------------------------------------------------------------------

APP_DIR="/run/muos/storage/application/muGBS"

cd "$APP_DIR" || exit 1

export LD_LIBRARY_PATH="$APP_DIR/lib:$LD_LIBRARY_PATH"

./bin/mugbs > "$APP_DIR/log.txt" 2>&1
```

**厳守事項:**

- `. /opt/muos/script/var/func.sh` の行は**絶対に削除しない**
  （CPUガバナ設定・SDL環境変数・HOME設定・SD1/SD2判定を行っている）
- `SETUP_APP "$APP_BIN" ""` を呼ぶ。第2引数は `"modern"` / `"retro"` /
  空文字（ボタンレイアウトの強制指定）
- **`/mnt/mmc` や `/mnt/sdcard` をハードコードしない。**
  必ず bind mount された `/run/muos/storage/application/muGBS` を使う。
  ハードコードすると SD2 搭載機やストレージ構成の異なる機種で壊れる
- 標準出力を `log.txt` に落としておくとデバッグが劇的に楽になる

### 9.3 `mux_lang.ini`

```ini
[full]
English=muGBS Player
Japanese=muGBS プレーヤー

[grid]
English=muGBS
Japanese=muGBS

[help]
English=A proper GBS player with full sub-track and extended M3U support.
Japanese=サブトラックと拡張M3Uに正しく対応したGBSプレーヤーです。
```

### 9.4 パッケージ生成スクリプト

`scripts/package.sh` を用意し、`build/` の成果物から
`muGBS-<version>.muxapp` を生成する。

```sh
cd package_root && zip -r ../muGBS-1.0.0.muxapp . -x '.*' -x '__MACOSX/*'
```

インストールは実機の `Applications > Archive Manager` から行う。

---

## 10. テスト計画

### 10.1 ホスト側（x86_64 Linux）でのテスト

実機に転送する前に、**必ずホスト側でネイティブビルドして動作確認できる状態を保つ**こと。
CMake オプション `-DTARGET_HOST=ON` でホストビルドできるようにする。

### 10.2 テストケース

| # | ケース | 期待結果 |
|---|---|---|
| T-01 | m3u なしの単体 `.gbs` を開く | 全トラックが `Track NN` で列挙される |
| T-02 | 同名 `.m3u` がある `.gbs` を開く | 曲名と曲長がm3u通りに反映される |
| T-03 | `.m3u` を直接開く | 同上。トラック順もm3uの記載順 |
| T-04 | 16進トラック番号（`$0A`）を含むm3u | 10番トラックとして解釈される |
| T-05 | 複数ファイルを参照するm3u | ファイルをまたいで連続再生できる |
| T-06 | `.gbs` + `.m3u` の zip | 展開せず再生できる |
| T-07 | `.gbs` のみの zip | 音楽ファイル一覧が出る |
| T-08 | 曲長不明のトラック | `default_length_sec` 経過後に次へ進む |
| T-09 | ループ曲 | `play_length` でフェードアウトして次へ |
| T-10 | 4chミュート | 該当チャンネルのみ無音になる |
| T-11 | 再生中に next 連打 | クラッシュせず、音が途切れず切り替わる |
| T-12 | 壊れた `.gbs` | エラーメッセージを表示し、クラッシュしない |
| T-13 | 存在しないファイルを参照するm3u | 該当エントリをスキップし警告表示 |

### 10.3 テスト用素材

著作権上の理由からリポジトリに `.gbs` は含めない。
`tests/fixtures/` には **合成した最小GBSファイル**（ヘッダのみ有効な擬似ファイル）と、
各種パターンの `.m3u` テキストのみを置く。m3uパーサのユニットテストはこれで行う。

---

## 11. 実装フェーズ

Claude Code は以下の順で実装し、各フェーズ末尾でビルドが通ることを確認してからコミットする。

| Phase | 内容 | 完了条件 |
|---|---|---|
| **P0** | リポジトリ初期化、CMake、libgme submodule、Docker | ホスト側で空ウィンドウが出る |
| **P1** | libgme連携＋SDL2音声出力 | CLI引数で渡した `.gbs` の1トラック目が鳴る |
| **P2** | 再生状態機械、トラック送り、フェード | 全トラックを自動で順に再生できる（T-01, T-08, T-09） |
| **P3** | m3u対応（`gme_load_m3u` + 自前プリパーサ） | T-02〜T-05 が通る |
| **P4** | zip対応（miniz） | T-06, T-07 が通る |
| **P5** | UI（Browser / Player / TrackList） | ホスト上でキーボード操作で完結する |
| **P6** | 入力抽象化、解像度非依存化、設定ファイル | 複数解像度でレイアウトが破綻しない |
| **P7** | クロスコンパイル、muxappパッケージング | 実機で起動・再生できる |
| **P8** | チャンネルミュート、ビジュアライザ、EQ | SHOULD/NICE要件 |

---

## 12. コーディング規約

- 言語: **C11**（libgme連携部のみ C++ でも可だが、極力Cで完結させる）
- 命名: `snake_case`。モジュール名をプレフィックスに（`player_next_track()`）
- エラー処理: 戻り値でエラーを返す。`assert` に頼らない
- **メモリ**: 全ての `malloc` に対応する `free` を明示。zip展開バッファの所有権を
  コメントで明記する
- **絶対パスのハードコード禁止**（`/mnt/mmc` 等）
- ログ: `LOG_INFO` / `LOG_WARN` / `LOG_ERR` マクロを用意し、stderr に出す
- 依存追加は事前に相談すること（バイナリサイズと実機の glibc 互換性に直結するため）

---

## 13. 既知の落とし穴チェックリスト

実装完了前に以下を確認すること。

- [ ] `gme_play()` の count はバイト数でもフレーム数でもなく **int16の個数**（偶数）
- [ ] `-static-libstdc++ -static-libgcc` を付けたか
- [ ] SDL2 は**実機から抜いたもの**に対してリンクしたか
- [ ] `emu` へのアクセスを `SDL_LockAudioDevice()` で保護したか
- [ ] `gme_open_data()` に渡したバッファを `gme_delete()` 前に free していないか
- [ ] `gme_free_info()` を呼んでいるか（`gme_track_info` はヒープを返す）
- [ ] `mux_launch.sh` の `func.sh` 読み込みを消していないか
- [ ] `/mnt/mmc` をハードコードしていないか
- [ ] 実行ファイルに実行権限が付いた状態で zip 化しているか
- [ ] 画面座標を 640x480 決め打ちしていないか
- [ ] ボタン番号を決め打ちしていないか

---

## 14. 参考

- game-music-emu: https://github.com/libgme/game-music-emu
- muOS Application Runner: https://community.muos.dev/t/application-runner/1282
- miniz: https://github.com/richgel999/miniz
- GBS形式仕様: https://www.tauwasser.eu/wiki/GBS

---

## 付録A: プランB（本プロジェクトを作らない場合）

自宅サーバ（Ubuntu）上で `gbsplay` を使い、トラック単位で opus/flac に
事前レンダリングしてタグ付けすれば、既存の音楽プレーヤーで再生できる。

```sh
gbsplay -o stdout -E l -r 44100 -t 150 Game.gbs 1 1 | \
  ffmpeg -f s16le -ar 44100 -ac 2 -i - -c:a libopus -b:a 128k out.opus
```

メリット: 実装コストゼロ、バッテリー消費が少ない
デメリット: 曲長を事前に決め打ちする必要がある、ファイルが増える、
`.gbs` をそのまま持ち歩けない


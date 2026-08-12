# muChip

muOS 向けの chiptune プレーヤー。GBS (Game Boy Sound System) と
`.nsf`/`.nsfe`（NSF, Nintendo Sound Format）を同格の一級市民として扱い、
サブトラック構造と拡張M3U（曲名・曲長・ループ指定）を正しく扱う。
（旧名 `muGBS`。詳細は [`docs/design-notes.md`](./docs/design-notes.md)
参照）。

- 主な機能: GBS/NSF/拡張M3U/zip対応、Browser/Player/TrackList/Settings
  のGUI、EQ・ビジュアライザ・シャッフル・ながさチェンジ・短い曲の
  スキップ・バッテリー残量表示・日本語メタデータ表示
- 詳細仕様: [`SPEC.md`](./SPEC.md)
- 設計判断（なぜこうなっているか）: [`docs/design-notes.md`](./docs/design-notes.md)
- 開発・ビルド・リリース手順: [`docs/development.md`](./docs/development.md)
- 変更履歴: [`CHANGELOG.md`](./CHANGELOG.md)
- 作業規約（Claude Code向け）: [`CLAUDE.md`](./CLAUDE.md)

## ビルド（ホスト / 開発機）

前提パッケージ（Ubuntu/Debian系）:

```sh
sudo apt update && sudo apt install -y \
    pkg-config libsdl2-dev cmake build-essential git
```

SDL2_ttf は不要（フォントは `vendor/font8x8`/`vendor/misaki` をコンパイル時に
埋め込んでいる）。

初回のみ submodule を取得:

```sh
git submodule update --init --recursive
```

ビルドとテスト:

```sh
./scripts/build-host.sh
ctest --test-dir build --output-on-failure
```

実行:

```sh
./build/muchip --list Game.gbs      # プレイリストを列挙するだけ（無音）
./build/muchip --cli Game.gbs       # 1トラック目を再生
```

クロスビルド・ASan/UBSan・パッケージング・CI・リリースの手順は
[`docs/development.md`](./docs/development.md) を参照。

## GUI (Browser / Player / TrackList / Settings)

引数無し、または `--list`/`--cli` 以外の起動でGUI本体が立ち上がる。

```sh
./build/muchip                       # カレントディレクトリのBrowserから開始
                                     # (config.iniのlast_pathがあればそこから)
./build/muchip --start-dir /path/to/music
./build/muchip Game.gbs              # 指定ファイルを直接Playerで開いて開始
./build/muchip --window 720x720      # ホストでの別解像度レイアウト確認用
                                     # (省略時は検出した解像度でフルスクリーン)
./build/muchip --config /path/to/config.ini  # 設定ファイルの場所を明示する
                                     # (省略時は環境変数MUCHIP_CONFIG、
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
リスト系画面(Browser/TrackList/Settings/Playerのファイル一覧)のカーソルは
端で反対側へ折り返す。ページ送り(Browserの`←``→`)は折り返さない。

Player画面の中央には、いま開いているファイルが置かれているディレクトリの
ファイル一覧が出る。カーソル(青)を`↑``↓`で動かして`Z`で決定すると、そのまま
別のファイルへ切り替えて再生できる(再生中のファイルは黄色で表示される)。
`1`/`2` の「前/次ソース」はこれとは別で、いま開いている m3u や zip の中で
参照先ファイルを跨ぐ移動。

**Yコンボ**: Player画面で `S`(Y相当)を押しながら方向キーを押すと、
Settingsへ入らずにRepeat/Shuffleを変えられる。`S`+`←`/`→` でRepeatモードを
1段ずつ進める/戻す(none→one→allの順で循環)、`S`+`↑`/`↓` でShuffleを
明示的にon/offする(トグルではない)。`S`単体(押して離すだけ)は何もしない。
ステータス行(`repeat:xxx shuffle:on/off`)ですぐ確認できる。

音量調整機能は無い(常に最大出力)。本体側のハードウェア音量と非連動で
紛らわしいため廃止した。

Settings 画面は Browser/Player どちらからも Start で開ける。**Length**
（ながさチェンジ）・**Default length**・**Skip short**（短い曲の
スキップ）・Repeat・**Shuffle**・Stereo depth・**EQ bass**・
**EQ treble**・Fade・Show all files・Scroll title・Show battery
の12項目を編集でき、抜けるときとアプリ終了時に `config.ini` へ自動保存
する（`sample_rate` はデバイス再オープンが必要なため対象外）。`X`
（キーボードは `A`）でこれら12項目を一括で既定値に戻す確認ダイアログを
開ける（`last_path`やコントローラ設定など、Settings画面に出てこない値は
対象外）。Default length は `config.ini` 上は秒のまま（既定180秒）だが、
Settings画面では `3 min` のように分単位・1分刻みで編集する。

**Length（ながさチェンジ）**: 既定は `auto`（今までどおり、m3uの曲長や
実測値があればそれを使い、無い曲だけ Default length へフォールバック
する）。`auto` 以外（5分刻みで5〜30分）を選ぶと、曲長不明の曲とループ
する曲（延長しても実際に鳴り続けられる曲）はその値へ延びる。ただし
m3u の曲長欄などで実測値は分かるがループしない曲は、延長はされず、
指定値より実測値が短ければそのまま、長ければ指定値で短縮される
（表示上の合計時間と実際に鳴る長さが食い違わないようにするため）。
`Repeat: one` のエンドレス再生はこれより優先する。値は `config.ini` 上は
秒（`length_override_sec`。`0`が`auto`）で、Default lengthと同じ吸着
ルールで分単位に見せる。変更はいま鳴っている曲へ即座に反映され、有効な
間はPlayer画面のステータス行に `len:15m` のように追記される。

**Skip short（短い曲のスキップ）**: 既定は `off`。`off` 以外（1秒刻みで
0〜30秒）を選ぶと、実測曲長がその秒数以下のトラック（効果音・ジングル
など）をTrackList一覧・再生順の両方から隠す。値は `config.ini` 上も
秒のまま（`skip_short_sec`。`0`が`off`）で、Default lengthと違い分に
丸めず `5s` のように秒で表示する。判定は常に実測曲長で行われ、Length
（上のながさチェンジ）で見かけの曲長を上書き中でも中身が短い曲は隠れる。
曲長が分からない曲（Default lengthのフォールバック対象）は対象外なので
消えない。いま再生中のトラックはしきい値を変えても消えない。

Shuffle を有効にすると、次/前トラック（自動送りも含む）がランダムな順で
進む。1周（全エントリを1回ずつ）したら Repeat が `all` なら次の周のために
並びを作り直し、`none`なら停止する。`Repeat: one` はシャッフルより優先し、
常に同じトラックを繰り返す（曲の終端でのフェードアウトも行わず、
そのままエンドレスに再生し続ける）。

### バッテリー残量表示

4画面すべてのタイトル行右端に残量ゲージ（矩形の枠＋残量ぶんの塗り。
8x8フォントはASCIIのみで絵文字が無いため数値は出さない）を表示できる。
Settings の `Show battery`（`config.ini` の `[ui] battery_show`）で
`off`/`low`/`always` を選ぶ（既定は `low`＝残量が少ないときだけ）。色は
通常グレー、残量が少ないと赤、充電中は緑。「少ない」のしきい値は、
実機では `mux_launch.sh` が muOS 側の設定を探して見つかればそれを使い、
見つからなければ既定の10%。バッテリーの無いホスト機では常に非表示になる。
開発中に見え方を確認したい場合は `MUCHIP_BATTERY_FAKE=85`
（非充電・残量85%）や `MUCHIP_BATTERY_FAKE=+5`（先頭`+`で充電中扱い・
残量5%）を付けて起動すると、`SDL_GetPowerInfo()` の代わりにその値を使う
（`--screenshot`と同格の非公開の開発用オプション）。

実機の物理ボタンでの終了は **GUIDEボタン単体、または Start+Select 同時押し**。

文字描画は外部フォントライブラリを使わず、内蔵のビットマップフォントを
使う。UI文言は英語のみ対応。GBS/NSF/M3Uのメタデータ（曲名・ゲーム名・
作者・著作権）はASCII用(`vendor/font8x8`)と非ASCII用(`vendor/misaki`、
8x8のJIS第1・第2水準相当)の2枚のフォントアトラスで描画し、Shift_JIS
(CP932)で書かれた日本語のメタデータも自動判定してUTF-8へ正規化してから
表示する（`src/text.c`）。どちらのアトラスにも無い文字は `?` に
フォールバックする。

### ビジュアライザ

Player 画面にはシークバーの下に簡易オシロスコープを表示する（混合出力
からの波形。理由は [`docs/design-notes.md`](./docs/design-notes.md) 参照）。

### EQ

`config.ini` の `eq_bass` / `eq_treble` は **-100〜100 の対称なノブ**で、
0 が GBS の既定の音に一致する。`+` 方向がそれぞれ「低音が増える」
「高音が増える」で直感どおりに動く。

## 設定ファイル (config.ini)

SPEC 7 の全キーに加え、`[ui] show_all_files`/`title_scroll`/
`battery_show`/`last_path` と `[input] gamecontroller_db`/
`controller_mapping` を持つ。`src/config.c` が読み書きする（外部のINI
ライブラリは使わない）。

- パス解決順: `--config PATH` > 環境変数 `MUCHIP_CONFIG` > `./config.ini`
- 保存は正規形で書き直す（手書きしたコメントや並び順は保存されない。
  値そのものは保持される）
- `--duration`/`--length`/`--skip-short`/`--fade-ms`/`--repeat` のいずれかを
  CLIで指定した場合、一回きりのテスト用オーバーライドを永続化しないよう
  終了時の自動保存を無効化する

## 入力: gamecontrollerdb 連携

muOSは `/usr/lib/gamecontrollerdb.txt` を実機に同梱しており、
`mux_launch.sh`/`func.sh` が起動時に `SDL_GAMECONTROLLERCONFIG_FILE`/
`SDL_GAMECONTROLLERCONFIG` を export するだけで物理ボタンが
`SDL_GameController` として認識される。`src/input.c` は生Joystick
イベントを自前解釈せず、`config.ini` の `[input] gamecontroller_db`/
`controller_mapping` から `SDL_GameControllerAddMappingsFromFile()`/
`AddMapping()` を呼ぶ経路のみを持つ（`mux_launch.sh` を経由しない開発時
や、DBに載っていない機種向けの上書き手段）。GameControllerとして認識
されなかったJoystickは、名前・GUID・ボタン/軸/ハット数をログに出すだけに
留める。

## muOS へのインストール

```sh
scp muChip-<version>.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
```

実機で **Applications > Archive Manager** から選んで展開するとインストール
される（`/run/muos/storage/application/muChip/` に配置される。実体の物理
パスは機種のSD構成によって変わるため `/mnt/mmc` 等をコードにハードコード
していない）。以後はアプリ一覧（Applications）に「muChip プレーヤー」が
アイコン付きで表示され、物理ボタンで起動できる。旧 `muGBS` からの移行の
場合、実機の `application/muGBS/` は自動では消えないので手動で削除する
こと（設定移行は行わない）。

`config.ini` はアプリディレクトリ直下に置かれ、終了時にオートセーブ
される（前回開いた場所・EQなどの設定が復元される）。`mux_launch.sh` は
起動のたびに実機のSDカードから音楽ディレクトリを自動検出し
（`MUSIC`/`Music`/`ROMS/GBS`等を優先的に探索、無ければ `ROMS`直下に
フォールバック）、`config.ini` にまだ `last_path` が無い初回起動時だけ
そこから始まる。

終了は **GUIDEボタン単体**、または **Start+Select同時押し**。

バージョンは `CMakeLists.txt` の `project(muchip VERSION x.y.z ...)` が
唯一の情報源（`./build/muchip --version` でも確認できる）。自分で
`.muxapp` をビルドする手順・実機検証の詳しい手順は
[`docs/development.md`](./docs/development.md) を参照。

## ライセンス / 同梱ソースについて

- `vendor/game-music-emu`（libgme）: git submodule。LGPL/GPL（同梱の
  `license.txt` / `license.gpl2.txt` を参照）。GBS デコードと拡張M3U解析を
  委譲している。自前で GB APU は実装していない。

  **upstream ではなくフォーク https://github.com/ka-zuu/game-music-emu の
  `mugbs` ブランチを参照している。** GBSのm3uトラック番号を0始まりとして
  扱う独自パッチを当てているため（詳細は
  [`docs/design-notes.md`](./docs/design-notes.md)「libgmeフォーク運用」
  参照）。upstream への追従は `git -C vendor/game-music-emu fetch upstream`
  から行う。
- `vendor/miniz`: MIT ライセンス。zip 展開に使用。
  https://github.com/richgel999/miniz より split-file ソースを vendoring。
- `vendor/font8x8`: パブリックドメイン。UI の文字描画（ASCII）に使用。
  https://github.com/dhepper/font8x8 より `font8x8_basic.h`
  （basic latin, U+0000-U+007F）のみを vendoring。
  オリジナルは Marcel Sondaar / IBM の public domain VGA フォントを
  Daniel Hepper が整理したもの。
- `vendor/misaki`: フリーソフトウェア（改変・商用利用・再配布可、無保証。
  詳細は `vendor/misaki/README.md`）。UI の文字描画（非ASCII）に使用。
  https://littlelimit.net/misaki.htm の美咲ゴシック BDF版から
  `tools/make_misaki_font.py` で `misaki_gothic.h` を生成して vendoring。
  Num Kadoma 氏によるフォント。

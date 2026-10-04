# muChip

muOS 向けの chiptune プレーヤー。GBS (Game Boy Sound System)、
`.nsf`/`.nsfe`（NSF, Nintendo Sound Format）、`.spc`（SPC, SNES SPC700
Sound File）を同格の一級市民として扱い、サブトラック構造と拡張M3U
（曲名・曲長・ループ指定）を正しく扱う。
（旧名 `muGBS`。詳細は [`docs/design-notes.md`](./docs/design-notes.md)
参照）。

- 主な機能: GBS/NSF/SPC/拡張M3U/zip対応、Browser/Player/TrackList/Settings
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
./build/muchip                       # 設定に従ってBrowserまたはPlayerから開始
                                     # (下記「起動モード」参照)
./build/muchip --start-dir /path/to/music  # 起動モードより優先してこの
                                     # ディレクトリのBrowserから始める
./build/muchip Game.gbs              # 指定ファイルを直接Playerで開いて開始
./build/muchip --window 720x720      # ホストでの別解像度レイアウト確認用
                                     # (省略時は検出した解像度でフルスクリーン)
./build/muchip --config /path/to/config.ini  # 設定ファイルの場所を明示する
                                     # (省略時は環境変数MUCHIP_CONFIG、
                                     # 無ければ ./config.ini)
```

**起動モード**: Settings画面の **Start with** で `Folder`（既定）と
`Last played`（Resume）を切り替える。`Folder` は **Start folder**
（同じくSettings画面。`A`でディレクトリ専用の選択サブ画面を開く）が
設定済みならそこから、未設定なら前回開いていた場所（F-13）から、それも
無ければ環境変数 `MUCHIP_START_DIR` かカレントディレクトリからBrowserを
開始する。`Last played` は前回再生していたファイル・トラック・再生位置を
復元し、**Player画面から自動再生で**始める（復元に失敗した場合は
`Folder` と同じフォールバック列に従う）。`--start-dir` はどちらの
モードより常に優先する。

キーボード操作（実機ではSDL_GameControllerのボタンに対応。SPEC 6.3参照）:

| キー | Browser | Player | TrackList | Settings |
|---|---|---|---|---|
| `↑` `↓` | カーソル移動 | 同ディレクトリの前/次ファイルへ送る | カーソル移動 | 項目選択 |
| `←` `→` | ページ送り | 前/次トラック | ページ送り | 値を増減 |
| `Z` (A相当) | 開く | お気に入りへ追加/削除 | ジャンプ再生 | 値を増やす |
| `X` (B相当) | 上の階層へ | Browserへ戻る | Playerへ戻る | 保存して戻る |
| `A` (X相当) | — | TrackListを開く | Playerへ戻る | リセット確認ダイアログ |
| `S` (Y相当) | お気に入りを開く | (単体では未使用。下記「Yコンボ」参照) | お気に入りへ追加/削除 | — |
| `Q`/`W` (L1/R1) | — | シーク -5s/+5s | — | — |
| `1`/`2` (L2/R2) | — | 前/次ソース | — | — |
| `Return` (Start相当) | Settingsを開く | Settingsを開く | — | 保存して戻る |
| `Space` (Select相当) | — | 再生/一時停止 | — | — |
| `Esc` | 終了 | 終了 | 終了 | — |

`↑↓←→` は押しっぱなしで長押しリピートする(初回350ms後から70ms間隔)。
リスト系画面(Browser/TrackList/Settings/Folder Pick)のカーソルは端で
反対側へ折り返す。Player画面の`↑↓`(ファイル送り)も同じ折り返し規約に
従うが、押しっぱなしにしても連射で曲が切り替わり続けない(1回押すごとに
1回だけ切り替わる)。ページ送り(Browserの`←``→`)は折り返さない。

Player画面は曲情報とシークバーの下を丸ごと波形ビジュアライザに使う
（同ディレクトリのファイル一覧UIは廃止した）。`↑``↓`を押すと、いま開いて
いるファイルと同じディレクトリの前/次の音楽ファイルへ即座に切り替わる
（端まで行くと反対側へ折り返す。長押しリピートでは連射されない — 1回
押すごとに1回だけ切り替わる）。`1`/`2` の「前/次ソース」はこれとは別で、
いま開いている m3u や zip の中で参照先ファイルを跨ぐ移動。

**Yコンボ**: Player画面で `S`(Y相当)を押しながら方向キーを押すと、
Settingsへ入らずにRepeat/Shuffleを変えられる。`S`+`←`/`→` でRepeatモードを
1段ずつ進める/戻す(none→one→allの順で循環)、`S`+`↑`/`↓` でShuffleを
明示的にon/offする(トグルではない)。`S`単体(押して離すだけ)は何もしない。
ステータス行(`repeat:xxx shuffle:on/off`)ですぐ確認できる。

**Theme Editor**(Settingsの`Edit theme`から`Z`で開く): `↑↓`でスロット
選択、`Q`/`W`(L1/R1相当)で編集するチャンネル(R/G/B)を切替、`←→`で値を
8刻みで増減、`A`(X相当)で確認ダイアログ無しに開いたときの状態へ戻す、
`X`(B相当)または`Return`(Start相当)でSettingsへ戻る(保存される)。

音量調整機能は無い(常に最大出力)。本体側のハードウェア音量と非連動で
紛らわしいため廃止した。

Settings 画面は Browser/Player どちらからも Start で開ける。**Length**
（ながさチェンジ）・**Default length**・**Skip short**（短い曲の
スキップ）・Repeat・**Shuffle**・Stereo depth・**EQ bass**・
**EQ treble**・Fade・Show all files・Scroll title・Show battery・
**Theme**（カラーテーマ）・**Start with**（起動モード）の14項目を編集
でき、抜けるときとアプリ終了時に `config.ini` へ自動保存する
（`sample_rate` はデバイス再オープンが必要なため対象外）。**Edit theme**
と **Start folder** は値を持たず、`A` でそれぞれ `Theme` の `custom`
パレットを編集するサブ画面（Theme Editor）・起動ディレクトリを選ぶ
サブ画面（ディレクトリのみ列挙。`A`で入る、`B`で上へ、`X`で確定、`Y`で
未設定に戻す、`Start`でキャンセル）を開く。`X`（キーボードは `A`）で
これら14項目を一括で既定値に戻す確認ダイアログを開ける（`last_path`や
コントローラ設定など、Settings画面に出てこない値は対象外。ただし
`Theme` の `custom` パレットと `Start folder` はSettings画面の一覧行
そのものには出てこないが、それぞれ `Theme`/`Start with` 自体のリセット
対象なので一緒に既定へ戻る）。Default length は `config.ini` 上は秒の
まま（既定180秒）だが、Settings画面では `3 min` のように分単位・1分
刻みで編集する。

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

### お気に入り

固定の1本のお気に入りリストを持つ。Player画面の `A`、またはTrackList画面の
`Y` で、再生中/選択中のトラックを追加・削除する（追加済みの曲名の前には
★が付く）。Browser画面の `Y` で、追加した順の1本のプレイリストとして
開く。フォルダをまたいでも、zip内の曲（zophar配布パック等）でも入れられ、
TrackList・リピート・シャッフル・Start with: `Last played` がそのまま使える。
お気に入り再生中は、Player画面の `↑↓`（同ディレクトリのファイル送り）は
無効になる。

保存先は `config.ini` と同じディレクトリの `favorites.txt`（環境変数
`MUCHIP_FAVORITES` で場所を変えられる）。1行1曲のタブ区切りテキストで、
音楽ファイルが無くなった行は再生時に読み飛ばされる。

### バッテリー残量表示

全6画面（Browser/Player/TrackList/Settings/Theme Editor/Folder Pick）
すべてのタイトル行右端に残量ゲージ（矩形の枠＋残量ぶんの塗り。
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

### カラーテーマ

Settings の `Theme` で5つのプリセット（`midnight`〈既定〉/`gameboy`/
`mono`/`amber`/`synthwave`。実機の反射型/低輝度パネル向けにすべてダーク系）
と `custom` を循環選択できる。`Theme` の次の `Edit theme`（`A`）で開く
Theme Editor サブ画面では、9つの色スロット（背景・パネル・本文・副文・
アクセント・選択行・再生中マーク・警告・充電中）を一覧から編集できる。
`UP`/`DOWN` でスロット選択、`L1`/`R1` で編集するチャンネル(R/G/B)を切替、
`LEFT`/`RIGHT` で値を8刻みで増減する。値を変えた瞬間に `Theme` は
`custom` へ切り替わり、全6画面へ即座にライブプレビューされる。`X`
で確認ダイアログ無しに開いたときの状態へ戻せる（パレットは1枚だけなので
これがそのままundoになる）。`custom` のパレットは `config.ini` の
`[theme]` セクション（9キー、`RRGGBB` の16進）を手編集しても変更できる。

実機の物理ボタンでの終了は **GUIDEボタン単体、または Start+Select 同時押し**。

文字描画は外部フォントライブラリを使わず、内蔵のビットマップフォントを
使う。UI文言は英語のみ対応。GBS/NSF/SPC/M3Uのメタデータ（曲名・ゲーム名・
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
`battery_show`/`theme`/`last_path`/`start_mode`/`start_folder`、
`[theme]` の9キー（`custom` テーマのパレット）、
`[input] gamecontroller_db`/`controller_mapping`、
`[resume] path`/`source`/`track`/`position_ms`（`start_mode=resume`の
ときだけ意味を持つ）を持つ。`src/config.c` が読み書きする（外部のINI
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
フォールバック）、`Start folder` が未設定のとき（`Start with` が `Folder` の場合）
そこから始まる。前回の続きから始めたい場合は `Start with` を
`Last played` にする。

終了は **GUIDEボタン単体**、または **Start+Select同時押し**。

バージョンは `CMakeLists.txt` の `project(muchip VERSION x.y.z ...)` が
唯一の情報源（`./build/muchip --version` でも確認できる）。自分で
`.muxapp` をビルドする手順・実機検証の詳しい手順は
[`docs/development.md`](./docs/development.md) を参照。

## ライセンス

muChip 自体は **MITライセンス**（[`LICENSE`](./LICENSE)）。同梱・リンク
しているサードパーティ製ソフトウェア（libgme、miniz、font8x8、美咲フォント
等）の一覧とライセンス条件、および LGPL-2.1 の libgme を静的リンクした
`.muxapp` を再リンク可能にする手順は [`THIRD-PARTY.md`](./THIRD-PARTY.md)
を参照。

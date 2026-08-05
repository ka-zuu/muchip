# PLAN.md — muGBS 実装計画

このドキュメントは `SPEC.md` に基づく実装計画と進捗を記録する。
詳細な設計判断・SPECとの乖離点は各フェーズのコミットログおよび
`docs/` (追加され次第) を参照。

現在のスコープ: **P0〜P6（コア再生エンジン + UI + 設定ファイル +
解像度非依存化）**。クロスコンパイルとmuxappパッケージング (P7)・
SHOULD/NICE要件 (P8) は次段の別プランとする。

## SPEC からの既知の乖離（実装前に libgme 本体を確認して判明）

`libgme/game-music-emu` @ `fe8da4b6d3876d7542c2fb69d94487e19836d678` (GME_VERSION 0.6.6) を
基準にした。SPEC の記述と食い違う点:

1. **`gme_info_t.play_length` は曲長不明時に `-1` ではなく `150000`（既定150秒）を返す**
   （`gme/gme.h` のコメントに明記）。曲長が既知かどうかの判定は `length` および
   `intro_length`+`loop_length` の組で行う。`play_length` で判定すると
   F-08（既定秒数フォールバック）が永久に発火しない。
2. `gme_set_fade(emu, start_msec)` に加えて **`gme_set_fade_msecs(emu, start_msec, length_msec)`**
   があり、`config.ini` の `fade_length_ms` はこちらで反映する。
3. SPEC 落とし穴3「`gme_open_data` はバッファをコピーしない場合がある」は本バージョンでは
   誤り。`gme_open_data()` / `gme_load_m3u_data()` は**データをコピーする**（ヘッダのコメントに
   "Makes copy of data." と明記）。ただし所有権の扱いは安全側に倒し、archive.c は
   1ファイル分だけを保持する遅延展開方針を採る。
4. CMake オプション名は `BUILD_SHARED_LIBS` ではなく **`GME_BUILD_SHARED` / `GME_BUILD_STATIC` /
   `GME_ENABLE_UBSAN`**。静的ターゲット名は **`gme_static`**。

## フェーズ進捗

- [x] **P0** — リポジトリ初期化、CMake、libgme submodule、Docker骨子
      完了条件: ホストで `mugbs` がビルドでき、引数なしで空のSDLウィンドウが出る
- [x] **P1** — libgme連携＋SDL2音声出力
      完了条件: `--cli` でCLI引数の `.gbs` の1トラック目が鳴る。合成GBS+PCMキャプチャで実測検証済み
- [x] **P2** — 再生状態機械、トラック送り、フェード
      完了条件: T-01, T-08 は3トラック合成GBSで実測検証済み（`--repeat none`で自動的に
      全トラックを再生してSTOPPEDに到達、REPEAT_ALL/ONEの巻き戻り・再開も確認）。
      T-09（既知曲長でのフェード）はGBS単体にはループ長メタデータが存在しないため、
      実質的な検証はm3uが入るP3まで持ち越し。フェード判定ロジック自体はP1で実装・
      レビュー済み(fade_start_ms)
- [x] **P3** — m3u対応（`gme_load_m3u` + 自前プリパーサ）
      完了条件: T-02〜T-05 に加え T-09/T-13 も実測・CTestで検証済み。
      m3u.c(セグメント分割) / playlist.c(統一データモデル) / CTest
      (tests/test_m3u.c, tests/test_playlist.c) を追加。
      合成GBS+m3uのCLIスモークテストで、単一ファイル参照・ファイル跨ぎ
      参照・16進トラック・存在しないファイル参照・既知曲長でのフェード
      すべてを実測確認した
- [x] **P4** — zip対応（miniz）
      完了条件: T-06, T-07 を実測検証済み。src/archive.c(miniz split-file
      ソースをラップ)を追加し、zipを開いたまま保持して必要なエントリだけ
      その都度メモリ展開する(gme_open_data/gme_load_m3u_dataはコピーする
      ため展開バッファは即解放できる)。展開後32MB超は事前拒否
      (mz_zip_reader_file_stat)。パス区切り・大小文字の揺れはbasename+
      小文字化で吸収(archive_find)。CTest(tests/test_archive.c)で
      分類・大小文字揺れ・展開・32MB拒否を検証し、さらに実際に元の
      .gbs/.m3uファイルを削除した状態でzipから正常に列挙・再生できる
      ことをCLIで実測し、ディスクへの一時展開が発生していないことを
      確認した
- [x] **P5** — UI（Browser / Player / TrackList）
      完了条件: ホスト上でキーボード操作だけでBrowser→ファイルを開く→
      Player→TrackList→トラックジャンプ→Browser、という一連の操作が
      完結することを確認済み（後述「P5の設計判断」参照）。
      T-01/T-02/T-06/T-07/T-11/T-12 をGUI経由でも実機さながらの手順
      （ASan/UBSanビルド + `--ui-script`によるヘッドレス操作列注入）で
      再検証し、クラッシュ・リークが無いことを確認した。
      **実機（muOS 2601.0 JACARANDA）でも実際に確認済み**: `/dev/fb0`を
      直接ダンプしてBrowser/Player画面が実機の`mali`ドライバ上で正しく
      描画されることをスクリーンショットで確認した（内蔵ビットマップ
      フォント・レイアウト・リストハイライト・プログレスバーすべて実機で
      正常動作）。この過程で **P5の実装バグを1件発見・修正した**
      （下記「実機確認で発見したバグ」参照）。TrackList画面は実機の
      物理ボタンがまだGameControllerとして未認識(P6予定)のため、実機上
      では未撮影(Browser/TrackListは共通コード`ui_draw_list()`を使うため
      リスクは低いと判断)。
- [x] **P6** — 入力の仕上げ、解像度非依存化の完成、設定ファイル
      完了条件: 実機で物理ボタンだけを使ってBrowser→ファイルを開く→
      Player→TrackList→Settingsで値を変更→終了、まで到達でき、
      再起動時に設定と`last_path`が復元される。ホストでは6解像度の
      CTestが緑。詳細は下記「P6の設計判断」参照。
      **達成**: config.c(INI読み書き)・設定の単一所有権化・
      ui_metrics_compute()の抽出と複数解像度CTest・Settings画面と
      オートセーブ・F-13(last_path)復元は全てホストで実測・
      ASan/UBSanで検証済み。入力については当初計画（生Joystick
      イベントの自前解釈）から方針転換し、muOSが実機に同梱する
      gamecontrollerdb.txtへ委譲する方式に変更、実機で
      `GameControllerを検出しました: muOS-Keys` を確認した。
      **未達（P7へ持ち越し）**: 物理ボタンでの対話操作(Browser/Player/
      Settings/終了)そのものの実地確認。SSH直接起動では`SETUP_APP`が
      呼ばれずmuxfrontendがフロントに残ったままになるため
      (下記「P6実機確認の制約」参照)、`.muxapp`化してmux_launch.sh
      経由で起動できるようになってから確認する
- [x] **P7** — クロスコンパイル、muxappパッケージング
      完了条件: 実機で起動・再生できる。**達成**: `.muxapp`化し、muOS本番の
      `extract.sh`（Archive Managerが内部で呼ぶのと同じスクリプト）経由での
      インストール、`mux_launch.sh`経由での正式起動、物理ボタンだけでの
      Browser→ファイルを開く→Player→TrackList→トラックジャンプ→Player→
      Settingsで値変更→GUIDE単体で終了→再起動→F-13復元、まで実機で
      すべて実測確認した。詳細は下記「P7の設計判断」参照
- [ ] P8 — チャンネルミュート、ビジュアライザ、EQ … 別プランで着手

## P5の設計判断（SPEC 4.2/6章 との差分・前倒し）

- **`src/app.c`/`app.h` を新規追加した。** SPEC 4.2 のモジュール構成表には
  無い。`ui.c` を「解像度非依存の描画プリミティブ」に徹させ、画面遷移
  （Browser/Player/TrackList の状態機械）・入力ディスパッチ・レイアウトの
  組み立ては `app.c` に集約した。`main.c` は引数解釈とCLIハーネス
  （`--list`/`--cli`。CI・自動検証用に維持）のままで、GUI起動時は
  `app_run()` に委譲するだけになっている。
- **解像度非依存化(SPEC 6.2)とSDL_GameController対応をP6から前倒しした。**
  座標をハードコードしてから後で直すのは手戻りが大きいため、`ui.c` の
  `ui_metrics_t`（`scale = min(w/640, h/480)` から全座標を導出）を最初から
  導入した。入力も `input.c` でSDL_GameControllerの論理ボタン名
  （`SDL_CONTROLLER_BUTTON_*`）に対応済み。P6に残っているのは
  「GameControllerとして認識されないJoystickへのフォールバック実装」と
  「config.iniによるマッピング上書き」で、`input.c` は未対応Joystickの
  名前とボタン番号を`LOG_INFO`に出す仕込みだけ済ませてある。
- **文字描画はSDL2_ttf等を使わず内蔵ビットマップフォント。**
  `vendor/font8x8/font8x8_basic.h`（dhepper/font8x8、パブリックドメイン、
  basic latin U+0000-U+007Fのみ）を起動時に1枚のテクスチャへ展開して使う。
  実機の`sysroot/`にはlibSDL2の`.so`しか置いておらず、SDL2_ttf(+freetype)
  が実機に存在するか未確認だったため、新規の実行時依存を増やさない選択を
  した。UI文言は英語、GBSメタデータもほぼASCIIのため実用上問題ない。
  非対応文字は`?`にフォールバックする。
- **音量調整(D-Pad上下)をP5で先取り実装した。** SPECの機能要件表には無いが
  SPEC 6.3の入力表がPlayer画面のD-Pad上下に音量を明記しているため、
  `audio.c`にソフトウェアミキシング（整数ゲイン、100%時は無演算で
  従来と完全に同一出力）を追加した。
- **`playlist.c`の曲長判定ロジックを一本化した。** 元々`player.c`内の
  `static fade_start_ms()`にあった判定（PLAN.md記載の乖離#1:
  `gme_info_t.play_length`は不明時-1でなく150000を返す）を
  `playlist_effective_length_ms()`として`playlist.c`へ切り出し、
  `pl_scan_source()`（UI表示用の`playlist_entry_t.duration_ms`を
  スキャン時点で確定）と`player.c`の再生開始時の両方から呼ぶようにした。
  これにより表示される曲長とフェード開始時刻が常に一致する。
- **検証範囲**: ホストでのASan/UBSanビルド + `--ui-script`
  （非公開オプション。テキストで列挙したアクション名を注入するヘッドレス
  実行モード）で画面遷移・ファイル切り替え・エラー系（壊れたGBS）を
  一通り走らせ、クラッシュ・リーク無しを確認した。CTestにも
  `test_browser`（ディレクトリ走査の単体テスト）と
  `test_ui_smoke`（`SDL_VIDEODRIVER=dummy`/`SDL_AUDIODRIVER=dummy`下で
  `--ui-script`を使い、Browser→ファイルを開く→TrackList→ジャンプ→
  Player→Browserの一巡を毎ビルドで回帰確認する）を追加した。
  その後、実機（muOS 2601.0 JACARANDA、192.168.0.20）にSSHでアクセスでき
  たため、P7で確立済みの`fetch-sysroot.sh`→クロスビルド→転送手順で
  実際にBrowser/Player画面を`/dev/fb0`のダンプ経由でスクリーンショット
  撮影し、見た目を確認した（下記「実機確認で発見したバグ」参照）。

## 実機確認で発見したバグ: Player画面の合計時間表示がフェード分を含んでいなかった

実機でのPlayer画面スクリーンショットで、経過時間が表示上の合計時間を
追い越して見える不具合（例: `0:12 / 0:08`）をユーザーが発見した。

**原因**: `player.c`の`start_track_at()`は`gme_set_fade_msecs(emu, fade_at,
fade_len)`を呼び、`fade_at`(=`playlist_entry_t.duration_ms`、UIの「合計
時間」表示に使っていた値)からフェードを開始し、そこからさらに`fade_len`
(既定8秒)かけてフェードアウトする。実際に`gme_track_ended()`が真になり
次トラックへ進むのは`fade_at + fade_len`時点であり、`fade_at`だけでは
ない。そのため:
- Player画面の「経過/合計」表示・シークバーの`duration_ms`が`fade_at`
  のままだと、フェード中(`fade_at`〜`fade_at+fade_len`)の間、経過時間が
  表示上の合計を追い越して見える
- 同じ`duration_ms`をシークの上限クランプにも使っていたため、
  右シーク(+5秒)がフェード開始時刻で頭打ちになり、実際には再生中の
  フェード区間へシークできないという実害もあった

**修正**: `player_t`に`fade_len_ms`フィールドを追加し、`start_track_at()`
で計算済みの`fade_len`をそこへ保持。`player_current_duration_ms()`は
`entries[current_entry].duration_ms`ではなく
`entries[current_entry].duration_ms + fade_len_ms`(=実際に無音になる時刻)
を返すよう変更した(`src/player.h`/`player.c`)。Player画面・シーク上限は
このアクセサ経由でしか値を取らないため、呼び出し側(`app.c`)の変更は
不要だった。

**検証**: 修正後に実機で再クロスビルド・転送し、同じ`0:08`宣言のm3uの
トラックを再生してスクリーンショットを2枚撮影。4秒経過時点で
`0:04 / 0:16`(合計がフェード込みの16秒=8+8になっている)、14秒経過時点で
`0:14 / 0:16`となり、経過が合計を追い越さないこと・プログレスバーが
正しく部分的に埋まることを確認した。ホスト側のCTest(全5件)も再実行し
影響が無いことを確認済み。

## m3u の設計方針（SPEC 5.2 の解釈）

SPEC 5.2 は「参照ファイルが複数なら自前でプレイリストエントリを構築する」としているが、
素直に実装するとトラック番号（10進/16進）・時間パースを自前で再実装することになり、
同節の「自前パーサで再実装しないこと」という原則と衝突する。

そこで `m3u.c` は **m3u をファイル参照ごとの連続区間（セグメント）に分割し、
区間ごとに m3u テキストを再構成して `gme_load_m3u_data()` に投げ直す**という
薄い前処理に徹する。トラック番号解釈・曲名・曲長の抽出は常に libgme に委譲される。
参照ファイルが1種類だけの場合はセグメントが1つになるため、特別扱いの分岐は不要。

## P6の設計判断

### 入力の方針転換: gamecontrollerdb.txt への委譲

P5時点の計画では、「GameControllerとして認識されないJoystickは
`SDL_Joystick`の生イベント(`SDL_JOYBUTTONDOWN`/`JOYHATMOTION`/
`JOYAXISMOTION`)を`config.ini`のマッピング表に従って自前で解釈する」
実装を予定していた(SPEC 6.3の文言どおり)。実機のボタン番号を実測して
既定マッピングを作る作業も計画に含めていた。

しかし実装前に、実際に公開されているmuOS向けアプリ
([XMPlayer](https://github.com/atalaygrgn/XMPlayer) v0.2.1)の
`.muxapp`を実際にダウンロード・展開して`mux_launch.sh`と同梱バイナリの
依存関係を確認したところ、そのような自前実装は一切していないことが
判明した。XMPlayerの`mux_launch.sh`は:

```sh
export SDL_GAMECONTROLLERCONFIG_FILE="/usr/lib/gamecontrollerdb.txt"
./bin/gptokeyb2 "love" -c "$APP_DIR/config/xmplayer.gptk" &
./bin/love .
```

の2行だけでゲームパッド対応を済ませている。`SDL_GAMECONTROLLERCONFIG_FILE`
をexportするだけでmuOS同梱のDBが読み込まれ、物理ボタンが
`SDL_GameController`として認識される(`gptokeyb2`はさらにそれを
キーボードイベントへ変換しているが、これはlove2d/XMPlayer側がSDL
GameControllerではなくキーボード入力を前提にしているため付加している
もので、mugbsは元々`SDL_CONTROLLER_BUTTON_*`にもキーボードにも
対応済みなので不要)。

P5で実機のボタンが反応しなかったのは、SSH直接起動で`mux_launch.sh`
(＝`func.sh`の`SETUP_APP`)を経由せず、この環境変数が設定されていな
かったためと判断した。

そこでP6では計画を変更し、**生Joystickイベントの自前解釈は実装しない**
こととした。代わりに:

1. `mugbs_config_t`に`[input] gamecontroller_db`/`controller_mapping`を
   追加し、`input_init()`が`SDL_GameControllerAddMappingsFromFile()`/
   `SDL_GameControllerAddMapping()`でデバイス走査前に登録する
   (`mux_launch.sh`を経由しない開発時や、DBに載っていない機種向けの
   上書き手段。SPEC 6.3の「config.iniでマッピング上書き可能にする」は
   この形で満たす)
2. GameControllerとして認識されなかったJoystickは、名前・GUID・
   ボタン/軸/ハット数をログに出すだけに留める(生イベントへは変換しない)
3. `SDL_IsGameController()`が真でも`SDL_GameControllerOpen()`が
   失敗する場合に(1)のログ経路へフォールスルーしない、というP5からの
   潜在バグを修正した(以前は該当デバイスが完全に無視され入力が
   死んでいた)

この判断は実機での検証で裏付けられている(下記「P6実機確認の結果」参照)。

### 設定の三重コピー問題

P5までは`mugbs_config_t`が`main()`のスタック→`app_t.cfg`→
`player_t.config`の3箇所に値でコピーされており、`volume`/`repeat_mode`
にだけ場当たり的なsetterがある状態だった(呼び出し側とsetterの双方が
別々のコピーへ書き込む二重更新になっていた)。P6でSettings画面が
フィールドを増やす前に、`player_t.config`を`const mugbs_config_t*`
(所有権は`app_t`、実体は`main()`のローカル変数1つ)へ変更し、
プログラム全体で権威あるインスタンスを1つに統一した
(`player_apply_config()`で反映)。`audio_callback`は`player_t`を
一切参照しない(`a->emu`と`SDL_atomic_t`な`a->volume`のみ)ため、
ポインタ化しても新たなスレッド間共有は生じないことを確認済み。

### 解像度非依存化: メトリクス再計算経路の欠如

P5時点では`ui_metrics_t`が`ui_init()`内で一度だけ計算され、以降
再計算する経路が無かった(実機は起動時に一度だけ解像度を取得すれば
十分なため、これ自体はP5の完了条件を満たしていた)。P6では
`ui_metrics_compute()`(純関数)を抽出し`ui_handle_resize()`を追加、
`SDL_WINDOWEVENT_SIZE_CHANGED`を検出して再計算する経路を通した
(ホストで`--window`使用時のみ`SDL_WINDOW_RESIZABLE`を付与)。この過程で
`pad`が`scale∈[0.5, 1.18)`の全域で4に固定される既存バグ
(`ui_glyph_size()`の8px下限の影響を受けていた)を発見・修正した。

### Settings画面のスコープ

SPEC 6.1のSettings画面はEQ・チャンネルミュートを含むが、両方P8未実装
のため、P6では Volume・Repeat・Stereo depth・Default length・Fade・
Show all files の6項目のみを実装した(`sample_rate`はデバイス再オープン
が必要なため除外)。SPEC 6.3はStartをPlayer画面専用としているが、
ファイルを開くまで設定に入れないのは初回体験として悪いため、Browser
画面からもStartで開けるようにした(意図的な逸脱)。

## P6実機確認の結果

実機(muOS 2601.0 JACARANDA、192.168.0.20)にSSH(root/root)で接続し、
以下を確認した。

**gamecontrollerdb.txtの実在確認**: `/usr/lib/gamecontrollerdb.txt`は
`/opt/muos/share/info/gamecontrollerdb/retro.txt`(または`modern.txt`。
`SETUP_APP`の第2引数で選択される)へのシンボリックリンクで実在する。
物理ボタンデバイスの名前は`muOS-Keys`(`/proc/bus/input/devices`で
`Bus=0019`、`Handlers=kbd js0 event1`。カーネルレベルでは既に
joystickとして登録されている)で、`retro.txt`に完全なマッピング行が
存在することを確認した:

```
19000000010000000100000000010000,muOS-Keys,a:b3,b:b4,x:b6,y:b5,
leftshoulder:b7,rightshoulder:b8,lefttrigger:b13,righttrigger:b14,
guide:b11,start:b10,back:b9,dpup:h0.1,dpleft:h0.8,dpright:h0.2,
dpdown:h0.4,volumedown:b1,volumeup:b2,leftx:a0,lefty:a1,leftstick:b12,
rightx:a2,righty:a3,rightstick:b15,platform:Linux,
```

A/B/X/Y/L1/R1/L2/R2/Guide/Start/Back/D-Pad/両スティック全てを網羅した
muOS公式のマッピングであり、mugbsの`controller_button_to_action()`が
前提とする`SDL_CONTROLLER_BUTTON_*`論理名の空間を過不足なく埋める。

**実機での動作確認**: `docker build -f docker/Dockerfile` →
`cmake --toolchain cmake/toolchain-aarch64.cmake`でクロスビルドした
`mugbs`をscpで転送し、`mux_launch.sh`/`func.sh`が実際にexportする
`SDL_GAMECONTROLLERCONFIG_FILE`/`SDL_GAMECONTROLLERCONFIG`を手動で
再現して実行したところ、ログに以下が出ることを確認した:

```
[INFO] GameControllerを検出しました: muOS-Keys
```

8秒間のアイドル実行でも(L2/R2のトリガー誤発火等の)余計なログは出ず、
`config.ini`の`[input] gamecontroller_db`経由(env var無し、691件の
マッピングを`input_init()`自身がロード)でも同じ検出に成功した。
`kill`(SIGTERM)を送るとSDL2の既定シグナルハンドラ経由で`INPUT_QUIT`
相当として処理され、`config.ini`が正しく保存されることも確認した。

**P6実機確認の制約(物理ボタンでの対話操作は未確認)**: 上記はいずれも
SSHで直接バイナリを起動する方式で行った。この方式では`func.sh`の
`SETUP_APP`(→`SET_VAR "system" "foreground_process" "$1"`)を経由
しないため、`foreground_process`が`muxfrontend`のままになり、
muxfrontend自身が前面から退避しないまま画面更新を続ける
(P5の「実機確認で発見したバグ」節で記録した現象と同一原因)。
実際にユーザーが実機で試したところ、カーソルに追従してmuOS側のUIが
部分的に描画され、mugbs側は物理ボタン入力を受け取れない状態だった
(スクリーンショットで確認)。**GameController自体は認識されている
ため、これはmugbsの入力処理の不具合ではなく、SETUP_APPを経由しない
検証手順そのものの制約である。** Browser/Player/Settings画面の実際の
ボタン操作・TrackList画面の実機確認・GUIDE/Start+Selectでの終了確認は、
`.muxapp`化して`mux_launch.sh`経由で正式に起動できるようになってから
(P7)行う。

## P7準備メモ: SDL2の扱いに関する調査（実装前の事前調査）

P7（クロスコンパイル・muxappパッケージング）に本格着手する前に、SPEC 8.3 が
警告する「実機から SDL2 を抜かないと動かない可能性が高い」を、実際に検証
する／裏付けを取る調査を行った。まだ実機での検証（sysroot抽出・実機ビルド）
はしていないが、机上調査の結論を記録する。

### 調査した選択肢と結果

1. **Debian bullseye の `libsdl2-dev:arm64` をそのままクロスリンクする案**
   `libSDL2-2.0.so.0` の `NEEDED` を実際に `readelf -d` で確認したところ、
   X11 / Wayland / PulseAudio / libdrm / libgbm 等のデスクトップ環境向け
   ライブラリに**直接リンク**されていた（dlopenベースの遅延ロードではない）。
   ELFの仕様上、これらが1つでも実機に無ければ `libSDL2.so` 自体のロードが
   失敗する。muOSのようなヘッドレス組み込みLinuxにX11/Waylandフルスタックが
   入っている可能性は低く、**この案は成立しない可能性が高いと判断し、
   実機での検証(tools/sdl_probe.c を使った比較実験)は行わずに見送った**。

2. **`rg35xx-cfw`（Batocera系）の SDK** (`arm-buildroot-linux-gnueabihf_sdk`)
   armhf（32bit）向けであり、SPEC が指定する aarch64（64bit）と
   アーキテクチャが異なるため不採用。

3. **`simotek/rg35xx-plus-aarch64-SDL2-SDK`**（RG35XX plus/H向け、aarch64）
   README に "The SDL2 implementation on the device is different from the
   standard ubuntu one" と明記されており、**同系統デバイスで実機SDL2が
   標準ディストリのものと異なる**ことが実例として確認できた
   （SPEC 8.3の警告を裏付ける）。ただしこれはRG35XX plus/Hの(muOSとは限らない)
   OS環境向けであり、そのまま流用はしない。`find_dev_packages.sh` で実機の
   インストール済みパッケージを列挙し `download_and_extract_debs.sh` で
   対応するdevパッケージを取得する、という自動化アプローチは参考になる。

4. **XMPlayer (`atalaygrgn/XMPlayer`) の実際の `.muxapp` を展開して検証**
   （最も参考になった）。`v0.2.1` の `.muxapp` を実際にダウンロード・展開し、
   同梱バイナリの `NEEDED` を確認した:
   - `bin/love` → `liblove-11.5.so` に依存
   - `libs/liblove-11.5.so` → `libSDL2-2.0.so.0` に依存**するが、
     `libs/` フォルダには libSDL2 自体は同梱されていない**
   - muOS純正の入力ヘルパー `bin/gptokeyb2` も同様に `libSDL2-2.0.so.0` を
     動的リンクで要求するが同梱していない

   **結論**: muOSは標準の共有ライブラリ検索パス上に、動作するSDL2を
   既に提供している。実際に公開され動作している複数のバイナリ（XMPlayer、
   muOS純正 gptokeyb2）がこれを裏付けている。

### 実機検証結果（確定）

ユーザーの実機（**muOS 2601.0 JACARANDA、RG35XX PRO相当、Cortex-A53 aarch64**）に
SSHで接続し、上記の暫定方針を実際に検証した。

**実機の環境:**
- OS: `MustardOS 2601.0 (JACARANDA)`、カーネル `4.9.170`
- glibc: **2.38**（Buildroot、2024年ビルド）— Debian bullseyeの
  crossbuild-essential-arm64が持つ glibc **2.31 より新しい**。
  SPEC 8.2 は「新しすぎるglibcでリンクすると実機の古いglibcで動かない」
  前提で「古めのベースイメージを使う」よう指示していたが、**実際は逆**
  だった。この差分により、素朴にDebianのクロスgccでリンクすると
  実機のSDL2が要求する新しいシンボル(`GLIBC_2.34`以降。pthread/dlopen等が
  libc.so.6に統合された影響)が `undefined reference` になりリンクが通らない
- SDL2: **バージョン2.28.5**、`NEEDED`は `libc.so.6`/`libm.so.6` のみという
  極めてミニマルなビルド（X11/Wayland/PulseAudio等への依存なし）。
  ビデオドライバは独自の **`mali`**（Allwinner H700系デバイス共通の
  Mali GPU直結フレームバッファドライバ。`knulli-cfw/knulli-linux` に
  同種のパッチが公開されている: `add-video-malifb-driver` 等）
- オーディオ: ALSAの `default` PCM が **PipeWire** 経由
  （`/etc/asound.conf`: `pcm.!default { slave.pcm { type pipewire } } }`）。
  `func.sh` が `XDG_RUNTIME_DIR=/run` / `PIPEWIRE_RUNTIME_DIR=/run` を
  exportしており、`mux_launch.sh` 経由の起動ではこれが自動的に設定される
  （SSH生シェルから検証する際は手動でexportが必要）

**クロスビルドで判明した重要な制約（Debian crossbuild-essential-arm64 固有）:**
`aarch64-linux-gnu-gcc --sysroot=X` を指定しても、libc/libm等の暗黙リンクに
使われるsysrootは無視され、常に `/usr/aarch64-linux-gnu`
（パッケージビルド時に固定）が使われる
（`gcc --sysroot=X -print-file-name=libc.so` で実測確認）。
そのため **`CMAKE_SYSROOT` では正しく機能しない**。回避策として、
実機から取得した `libc.so.6` / `libm.so.6` / `libpthread.so.0` /
`libdl.so.2` / `ld-linux-aarch64.so.1` / `libSDL2*` を
`/usr/aarch64-linux-gnu/{lib,include/SDL2}` へ直接上書き配置する
（`docker/Dockerfile` の `COPY` で実施）。

**検証結果:** `tools/sdl_probe.c` をこの方式でビルドし実機で実行したところ、
映像（`mali`ドライバ、640x480@60Hz、ウィンドウ/レンダラ作成）・音声
（`XDG_RUNTIME_DIR`設定後、ALSA経由で44100Hz/2ch再生）とも正常動作を確認。
さらに **`mugbs` 本体**（合成GBS、2トラック）も実機で実際にクロスビルド・
転送・実行し、正しくプレイリストを再生してトラック送りすることを確認した。

**確立した構成要素:**
- `scripts/fetch-sysroot.sh` — 実機からSSH/SCPで `sysroot/` を構成する
  スクリプト（パスワード等は埋め込まない。SDL2バージョンを実機の`.so`から
  自動検出し、対応するupstream SDL2のヘッダを取得する）
- `docker/Dockerfile` — `sysroot/` の内容をクロスツールチェイン既定の
  sysrootへ上書きコピーする
- `cmake/toolchain-aarch64.cmake` — コンパイラ指定のみ
  （`CMAKE_SYSROOT`は使わない。上記の理由をコメントに明記）
- ルート `CMakeLists.txt` — クロスビルド時のSDL2は `find_package` を使わず
  `/usr/aarch64-linux-gnu/include/SDL2` を直接参照する
- `tools/sdl_probe.c` は実機検証で実際に使用（video/audioドライバの
  診断に有効だったため、`docker/Dockerfile.sdl-probe`
  （Debian標準SDL2実験用、未使用のまま）は削除した

**まだ未検証（P7本格着手時に対応）:**
- muxappパッケージング（`mux_launch.sh`, `lib/`構成, `.muxapp`化）。
  **`mux_launch.sh`には`export SDL_GAMECONTROLLERCONFIG_FILE="/usr/lib/gamecontrollerdb.txt"`
  を追加すること**（XMPlayerと同じ。P6でconfig.iniの`gamecontroller_db`
  でも代替できることを確認済みなので、どちらか一方があれば動く二重化になる）
- 複数デバイス（RG35XX H/SP, RG40XX, RG CubeXX等）での動作差異
- `sysroot/` の内容の版管理方針（バイナリはgit管理しない。再現性は
  `fetch-sysroot.sh` の再実行に依存する）
- **P5で追加したGUIのTrackList画面**、および**Browser/Player/Settings
  画面での実際の物理ボタン操作・GUIDE/Start+Selectでの終了**は実機で
  未確認のまま（Browser/Playerの描画自体はP5・P6とも確認済み）
- **P5の実機スクリーンショットに写り込んでいたオーバーレイの原因は、
  P6で確定した。** `GET_VAR system foreground_process` が `muxfrontend`
  のままだったことをP5時点で突き止めていたが、P6で改めてSSH直接起動
  すると同じ現象がより明確な形で再現した:
  `mux_launch.sh`（＝`func.sh`の`SETUP_APP()`。
  `SET_VAR "system" "foreground_process" "$1"`で「今このアプリが
  前面にいる」とOSへ伝える）を経由せずにバイナリを直接起動すると、
  `muxfrontend`が自身を前面から退避させないまま`/dev/fb0`への描画を
  続け、mugbsの描画に薄く重なる。P6ではユーザーが実機で物理ボタンを
  試したところ、この重なりに加えて**物理ボタン入力がmugbsへ渡らない**
  ことも確認された（`SDL_GameController`自体は認識されているため、
  これはmugbsの入力処理ではなくmuxfrontendが入力もろとも掴んだままに
  なっていることが原因と考えられる）。`mugbs`側の不具合ではなく、
  `SETUP_APP`を経由しない検証手順固有の制約であることが再確認できた。
  **`.muxapp`化して`mux_launch.sh`経由で正式に起動した状態で、
  (1) このオーバーレイが再現しないこと、(2) 物理ボタンでの対話操作が
  実際にできることの両方をP7で必ず確認すること**
  （`SETUP_APP`呼び出しで`foreground_process`が`mugbs`に切り替わるか
  を見る）。

## P7の設計判断

### SPEC 9章からの逸脱（muOSの実装ソースと実物の`.muxapp`で裏取り済み）

SPEC.md 9章は現行世代(JACARANDA/2601)に対して誤っている箇所がある。
`MustardOS/internal`・`MustardOS/frontend`のソースと、実際に動作している
`XMPlayer v0.2.1`の`.muxapp`を展開して確認した結果:

1. **zip内トップは`mnt/mmc/MUOS/application/muGBS/`ではなく`muGBS/`**。
   `internal/script/mux/extract.sh`が
   `EXTRACT_ARCHIVE "Application" "$ARCHIVE" "$MUOS_STORE_DIR/application"`
   (= `unzip -d /run/muos/storage/application`)に展開するため。XMPlayerの
   全118エントリも`XMPlayer/`始まりだった。SPEC通りにすると
   `.../application/mnt/mmc/MUOS/application/muGBS/`に展開されて動かない。
   実機の`extract.sh`でも同じ実装であることを直接確認した。
2. **`assets/font.ttf`は同梱しない**。`src/ui.c:7`が`vendor/font8x8`を
   コンパイル時埋め込みしており、実行時の外部アセットロードはゼロ
   (P5から既知)。
3. **`lib/`は同梱しない**。SDL2だけが実機の動的ライブラリで、libgme/miniz
   は静的、libstdc++/libgccも`-static-*`済み。
4. **`SETUP_APP "$APP_BIN" ""`**とした(SPECのコードブロックは`"retro"`)。
   第2引数はgamecontrollerdbのレイアウト強制で、retro/modernの差はA/B・
   X/Yの入れ替えだけ。mugbsは論理ボタン(`SDL_CONTROLLER_BUTTON_*`)で
   解釈するため、ユーザーがmuOS全体で選んでいるレイアウトを尊重すべき。
   XMPlayerも`""`。SPEC 9.2自体がコードブロック(`"retro"`)と散文(`""`)で
   自己矛盾している。
5. **`glyph/`だけでなく`grid/`も必要**。`frontend/common/ui/glyph.c`の
   `apply_app_glyph()`が`<app>/glyph/<ICON>.png`を、
   `get_app_grid_glyph()`が`<app>/grid/<ICON>.png`を引く。既定テーマの
   640x480はグリッド表示ではなくリスト表示だったため実機では`glyph/`側
   が使われることを確認したが、テーマによってはグリッド表示になるため
   両方同梱する判断は維持した。

### `MUGBS_START_DIR`環境変数の追加(F-13を潰さないため)

`mux_launch.sh`は起動のたびに音楽ディレクトリを自動検出する
(`GET_VAR device storage/{rom,sdcard,usb}/mount`から候補を組み立て、
`/mnt/mmc`等のハードコードはしない。`/run/muos/storage/music`はmuOSの
BGM(.ogg)置き場でユーザーの音楽ライブラリではないので候補から除外)。

これを`--start-dir`で渡すと`app.c`の優先順位が`--start-dir > last_path`の
ため、P6で実装したF-13(前回開いた場所の復元)が実機で永久に発火しなく
なる。そこで優先度の低い環境変数`MUGBS_START_DIR`を追加し、
`--start-dir > last_path > MUGBS_START_DIR > "."`とした
(`src/app.c`/`app.h`/`main.c`)。実機での再起動テストで、初回相当の
起動時のみ自動検出フォルダから始まり、2回目以降は前回終了時の場所
(ファイル単位で)に戻ることを実測確認した。

### バージョン管理とパッケージング

`CMakeLists.txt`の`project(mugbs VERSION 0.9.0 ...)`を唯一のバージョン
情報源とした。0.9.0とした理由: MUST要件(F-01〜F-08)は満たしているが
SHOULD/NICE要件(F-10チャンネルミュート/F-14ビジュアライザ/F-20 EQ等、
P8)が未実装のため。P8完了で1.0.0に上げる想定。

`scripts/package.sh`はSPEC 9.4の`cd package_root && zip -r ../x.muxapp .`
をそのまま使わなかった。`./`エントリが混ざりトップレベル名も入らないため、
上記の「zip内トップはmuGBS/」を満たせない。代わりに
`(cd "$STAGE" && zip -r -X -q "$OUT" muGBS -x '.*' '*/.*' '__MACOSX/*')`
とした。strip(実測: 3,710,736 -> 587,536バイト)はホストにクロスbinutils
が無いため、無ければクロスビルド用Dockerイメージ経由で実行し、どちらも
使えなければエラーで止める(黙ってstrip無しを出荷しない)。

`tests/test_package.sh`はクロスビルド成果物が無いホスト/CIでも動くよう
ダミーバイナリで構造だけを検証する。`# ICON:`の値と`glyph/`/`grid/`の
ファイル名照合など、実機に持って行くまで気づけない種類のバグを機械的に
潰す設計にした。

### P7実機検証の結果(すべて確認済み)

実機(muOS 2601.0 JACARANDA、192.168.0.20)で以下を確認した。

**インストール**: `.muxapp`を`/mnt/mmc/ARCHIVE/`に置き、muOS本番の
`/opt/muos/script/mux/extract.sh`(Archive Managerが内部で呼ぶのと同じ
スクリプト)経由でインストールした。`/run/muos/storage/application/muGBS/`
(実体は`/mnt/sdcard/MUOS/application/muGBS/`。`storage/rom/mount`=`/mnt/mmc`
ではなく`/mnt/sdcard`側にbind mountされていた)に正しいレイアウト・
パーミッション(`mux_launch.sh`/`bin/mugbs`とも`-rwxr-xr-x`)で展開された。

**P5/P6の未解決問題(オーバーレイ・入力不達)の解消を確認**: アプリ一覧
から物理ボタンでmuGBSを起動したところ、
`cat /opt/muos/config/system/foreground_process`が`mugbs`を返し
(`muxfrontend`ではない)、`pidof muxfrontend`が失敗する(プロセスが
存在しない)ことを確認した。`/dev/fb0`のスクリーンショットでもmuOSの
UIの重なりは一切無く、P5/P6で記録した不具合が`mux_launch.sh`経由の
正式起動で完全に解消することを実機で裏付けた。

**物理ボタンでの一連の操作(すべてユーザーが実機で操作、Claudeは
SSH側でfb0スクリーンショット取得とログ確認を担当)**:
Browser(`GB`フォルダ)→合成GBS+m3u(4トラック)を開く→Player(再生中の
経過時間が進むことを確認)→X でTrackList(**P5から実機未確認だった
持ち越し項目。今回初めて実機で撮影・動作確認できた**)→トラックへ
ジャンプ(Track 1→Track 3、ログの`再生: [3/4] "Track 03"`で確認)→
Player→Start でSettings→LEFT/RIGHTでVolumeを80→60に変更→B で戻る
(`config.ini`に`volume = 60`が保存される)→**GUIDE単体ボタンで終了**
(muOSのアプリ一覧`アプリケーション`に戻り、`muGBS プレーヤー`が
アイコン付き・日本語名で表示されることを確認)。

**F-13(前回の続きから開く)の実機確認**: 再度起動したところ、
`last_path`に保存されていた`/mnt/sdcard/ROMS/GB/Game.m3u`から
「ディレクトリとして開こうとして失敗→ファイルとして扱いカーソルを
合わせる」というフォールバック(`src/app.c`の`restore_last_path()`)が
実機でも正しく動作し、`GB`フォルダが開いて`Game.m3u`にカーソルが
合った状態(終了直前と同じ状態)で復元された。`volume = 60`も維持
されていた。

**SIGTERM(スリープ・電源断相当)での終了確認**: `kill -TERM $(pidof mugbs)`
を送ったところ1秒以内に終了し、`config.ini`が保存され(`md5sum`で変化を
確認)、`foreground_process`が`muxfrontend`に正常復帰した。`quit.sh`が
スリープ・電源断時にこの経路を通るため、正常系と同じ結果になることを
確認できた。

**アプリ一覧でのグリフ・言語表示**: `アプリケーション`一覧に
`glyph/mugbs.png`のアイコンと`mux_lang.ini`の日本語名
`muGBS プレーヤー`が正しく表示されることを確認した。

**未実施(GUIDE単体で終了が確認できたため優先度を下げた)**:
Start+Select同時押しでの終了は別経路として実装済みだが、今回は未検証
のまま。

**発見した副産物**: 実機のSD2(`/mnt/sdcard`、`storage/sdcard/mount`)
には`ROMS/GB`等コンソール別のジャンルフォルダはあるが、専用の
`MUSIC`/`ROMS/GBS`フォルダは無かった。`mux_launch.sh`の自動検出は
候補が無い場合`ROMS`直下にフォールバックする設計だったため、実機でも
`start dir: /mnt/sdcard/ROMS`とログに出て正しくフォールバックすることを
確認した(汎用ROMSフォルダしか無い機体では、ユーザーは手動で該当フォルダ
まで潜る必要がある。P8以降でメディアライブラリのディレクトリ指定機能を
実装する際に解消する想定)。

### 実機検証中に発生したインシデントと安全策(記録として残す)

C5の当初計画では「muOSの内部制御ファイル(`/tmp/app_go`/`/tmp/act_go`)を
直接書き換えてアプリ起動をトリガーする」ことで物理ボタン操作を代替
しようとしたが、`frontend.sh`のループが想定外の状態でスタックし、
画面が固まる事象が発生した(`frontend.sh`を再起動することで実機は
正常復旧し、データ破損等は無かった)。原因は特定できていない
(`EXEC_MUX`が同期実行のため、実行中のUIモジュールプロセスを
外部から`kill`すると、次のループ反復に必要な状態が壊れる可能性がある
と推測している)。

この経験を踏まえ、**物理ボタン操作が必要な検証はユーザーに実機で
直接操作してもらい、Claudeはその間SSH側でスクリーンショット取得・
ログ確認・状態確認を並行して行う**という分担に切り替えた。以降の
検証(TrackList〜終了確認まで)はすべてこの方式で安全に完了できた。
**今後、muOSの内部制御ファイルを外部から書き換えてUI遷移を
トリガーする手法は、たとえ実機アクセス権があっても使わないこと。**

## 検証手順

```sh
# 前提（初回のみ）
sudo apt update && sudo apt install -y pkg-config libsdl2-dev libsdl2-ttf-dev

# ビルド
./scripts/build-host.sh

# 単体テスト
ctest --test-dir build --output-on-failure

# プレイリスト構築の確認（音を出さない）
./build/mugbs --list Game.gbs
./build/mugbs --list Game.m3u

# 実再生（要 .gbs 実素材）
./build/mugbs --cli Game.gbs
./build/mugbs --cli --duration 8 Game.gbs

# GUI: Browser/Player/TrackList/Settings をキーボードで操作する
./build/mugbs                         # カレントディレクトリのBrowserから開始
./build/mugbs --start-dir /path/to/music
./build/mugbs --window 720x720        # 別解像度でレイアウト確認(ホストのみ。掴んで伸縮も可)
./build/mugbs Game.gbs                # 指定ファイルを直接Playerで開いて開始

# GUI (P6): config.ini の読み書きとSettings画面
./build/mugbs --config /tmp/test.ini --start-dir /path/to/music
#   Start で Settings を開く -> LEFT/RIGHT で値変更 -> B で戻る(保存される) -> Esc で終了
cat /tmp/test.ini                     # 変更が保存されていることを確認
./build/mugbs --config /tmp/test.ini  # last_path 等が復元されることを確認
```

### クロスビルド・パッケージング・実機検証（P7で確立。実証済みの手順）

```sh
# 1. 実機からsysrootを構成 (初回のみ。実機のIP/ユーザーに合わせる)
./scripts/fetch-sysroot.sh root@<実機のIP>

# 2. クロスビルド (Dockerイメージが無ければ自動で作る)
./scripts/build-aarch64.sh
#   sysroot/ を更新した場合は ./scripts/build-aarch64.sh --rebuild-image

# 3. .muxapp を作る (strip・バージョン付けまで自動)
./scripts/package.sh
#   -> ./muGBS-0.9.0.muxapp

# 4. 実機へ転送してインストール (正式ルート: Archive Manager)
scp muGBS-0.9.0.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
# 実機で Applications > Archive Manager > muGBS-0.9.0 を選んで展開する
# (SSH越しに /opt/muos/script/mux/extract.sh /mnt/mmc/ARCHIVE/muGBS-0.9.0.muxapp
#  を直接叩いても同じ結果になる。Archive Managerが内部で呼ぶのと同一スクリプト)

# 5. アプリ一覧(Applications)から物理ボタンで起動する
#    ログは実機の /run/muos/storage/application/muGBS/log.txt (=SD上の
#    MUOS/application/muGBS/log.txt) に出る
```

P6以前にあった「SSH直接起動でXDG_RUNTIME_DIR等を手動exportする」手順は、
`mux_launch.sh`経由の正式起動であれば`func.sh`が自動で行うため不要になった。
CLIハーネス(`--cli`)での単発確認だけしたい場合は、以前どおり手動exportが必要
(`mux_launch.sh`を経由しないため)。

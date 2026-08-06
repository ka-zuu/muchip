# PLAN.md — muGBS 実装計画

このドキュメントは `SPEC.md` に基づく実装計画と進捗を記録する。
詳細な設計判断・SPECとの乖離点は各フェーズのコミットログおよび
`docs/` (追加され次第) を参照。

現在のスコープ: **P0〜P8 完了 (v1.0.0)**。コア再生エンジン + UI +
設定ファイル + 解像度非依存化 + muOS向けパッケージング + SHOULD/NICE要件の
うち F-14 ビジュアライザ / F-20 EQ まで実装済み(F-10 チャンネルミュートは
実装した上でユーザー判断により削除。下記「P8の設計判断」参照)。

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
- [x] **P8** — ビジュアライザ(F-14)、EQ(F-20)。**チャンネルミュート(F-10)は
      実装した後、ユーザーの判断で不要と削除した**
      完了条件: 残した2要件が音とUIの両方に繋がり、実機で確認できる。
      **達成**: `[audio] eq_bass`/`eq_treble` は P6 で読み書きだけ実装済み
      だったものを、`player.c` 経由で実際に `gme_set_equalizer()` へ
      流し込むようにした。EQ は Settings の2項目。ビジュアライザは
      混合出力からの簡易オシロスコープ(SPEC F-14 が許容する「波形」の方)。
      **F-10 チャンネルミュートは一度実装した**(`gme_mute_voices()`を
      emu焼き込み・再生中両方に配線、Player画面SELECTでのミュートパネル、
      T-10を自動検証する`tests/test_mute.c`、そのための「実際に4ch鳴る」
      合成GBSフィクスチャまで揃え、ホストで動作・実機投入直前まで確認した)
      が、リリース直前にユーザーから「この機能は要らない」との判断があり、
      関連コード一式(`SCREEN_VOICES`・`voice_mute_mask`・`test_mute.c`等)を
      削除した。合成GBSフィクスチャが音を出す実装だけはビジュアライザの
      目視確認に有用なため残した(詳細は下記「P8の設計判断」)。
      **実機検証(すべて確認済み)**: `.muxapp`化して実機(muOS 2601.0
      JACARANDA)へインストールし、アプリ一覧から物理ボタンで起動、
      Player画面で波形が実際に動くこと、SELECTを押してもミュートパネルが
      出ない(削除済みの確認)こと、Settings で EQ bass/treble を振ると
      音色が変わること、終了(GUIDE)→`config.ini`に`[voices]`セクションが
      残っていないこと、を確認した。この過程で「十字キー押しっぱなしで
      連続操作できない」という実機特有の課題が見つかり、D-padの長押し
      リピートを追加した(下記「D-pad長押しリピートを追加した」参照)。
- [x] **P9** — 実機フィードバック対応(完了。実機検証も確認済み)
      v1.0.0を実機で使い込んだユーザーからの5件のフィードバックに対応した。
      C1〜C6の6コミットに分けてある。詳細な背景は下記
      「P9で対応する実機フィードバック」、計画からの変更点は
      「P9の設計判断」を参照。
      1. zip内に複数の`.m3u`があるとき1曲しか再生されないバグの修正
         (SPEC T-14。全m3uを名前順に連結して1つとして解析する)
      2. 音量調整機能の完全削除(常に最大出力)
      3. リスト画面のカーソルが端で反対側へ折り返す
         (ページ送りは対象外)
      4. Player画面のボタン再割当(UP/DOWN=一覧のカーソル、A=決定、
         SELECT=再生/一時停止、LEFT/RIGHT=トラック切替、L1/R1=シーク)
      5. Player画面に現ディレクトリのファイル一覧を追加、波形を下部へ移動
      **ctest 15件すべて緑、ASan/UBSanビルドでも緑。**
      320x240/480x320/640x480 のスクリーンショットでレイアウトを目視確認済み。
      **実機検証も完了**(下記「P9の残作業」参照。1.0.0を実機muOS
      2601.0 JACARANDAへ`.muxapp`で投入し、6項目すべてユーザーに確認して
      もらい問題なしとの回答を得た)。
- [x] **P10** — 実機フィードバック対応・第2弾(完了。実機検証も確認済み)
      P9を実機で確認したユーザーから続けて4件のフィードバックがあり対応した。
      詳細は下記「P10で対応する実機フィードバック」を参照。
      1. Settings画面でDefault length/Fadeを触ったときのフッタ注記
         `(next track)` を削除
      2. Settings画面に設定リセット機能を追加(`X`→確認ダイアログ→`A`で
         SETTINGS[]の項目だけを既定値に戻す)
      3. Player画面の波形をフッタ近くまで広げる(一時メッセージ用の固定行
         確保をやめ、波形の高さ上限も撤廃)
      4. シャッフル再生(F-25)を追加。純ロジックを`src/shuffle.c`に切り出し、
         `tests/test_shuffle.c`で単体テストした
      **ctest 16件すべて緑(test_shuffle追加)、ASan/UBSanビルドでも緑
      (mugbs本体をui_smoke.script経由でLeakSanitizer有効のまま走らせても
      リーク無し)。** 320x240/480x320/640x480/720x720のスクリーンショットで
      レイアウトと確認ダイアログを目視確認済み。**実機検証も完了**
      (下記「P10の実機検証」参照。ユーザーから「動作は確認できました」との
      回答を得た)。
- [x] **P11** — Player画面からRepeat/Shuffleを直接変えるYコンボ(完了。
      実機検証も完了)
      P10を実機で確認したユーザーから、「Shuffleの設定が面倒(Settings画面
      まで入らないと切り替えられない)」というフィードバックがあった。
      Player画面で`Y`を押しながらD-Padを押す「コンボ」操作を追加した:
      `Y+Left`/`Y+Right`でRepeatモードを1段ずつ進める/戻す、
      `Y+Up`/`Y+Down`でShuffleを明示的にon/off。詳細は下記
      「P11で対応する実機フィードバック」を参照。
      `Y`単体(押して離すだけ)は何もしなくなった(以前はリピートモード
      切替だったが、`Y+Left`/`Y+Right`へ移した)。
      **ctest 16件すべて緑、ASan/UBSanビルドでも緑。** ui_smoke.scriptに
      `Y_LEFT`/`Y_RIGHT`/`Y_UP`/`Y_DOWN`を追加し、640x480のスクリーン
      ショットでPlayerのステータス行(`repeat:xxx shuffle:on/off`)が
      正しく変わることを確認済み。**実機での「押しながら」の押下感自体も
      確認済み**(下記「P11の実機検証」参照。ユーザーから「それも確認
      済みです」との回答を得た)。
- [x] **P12** — m3uの10進トラック番号が0始まりであることに対応(完了。
      実機での最終確認も完了)
      ユーザーから「m3uで2曲目を再生すると、GBS内の1曲目が再生される」
      という報告があり調査した。原因は同梱のlibgme(`game-music-emu`)側の
      前提の誤りで、GBSのm3uにある10進トラック番号を「1始まり」とみなし
      内部で-1していたため、実際に多くのGBS配布パック(zophar.net製など)
      が使う「0始まり」の慣習と1つズレていた。
      `vendor/game-music-emu/gme/Gbs_Emu.cpp`に1行のパッチ(KSSと同じ
      `flags_ |= 0x02`)を当てて修正した。詳細は下記
      「P12: m3uトラック番号の0始まり/1始まり問題」を参照。
      **実機の実データ(Parodius (EMU).zophar)を使い、SDLのdiskオーディオ
      ドライバでPCMを書き出してFFTピーク周波数を比較し、修正前は
      「宣言0」が本来「宣言1」の音(441Hz)ではなく別の不正な音(329Hz。
      不正index -1 由来)を再生していたこと、修正後は「宣言0」が
      正しく441Hz(旧「宣言1」と統計値まで一致)、「宣言1」が新たな
      330Hzになったことを実測で確認した。** ctest 16件すべて緑
      (タイトルだけを見ていた既存テストの10進トラック番号を0始まりへ
      更新)、ASan/UBSanビルドでも緑。**実機muxappでの最終的な耳での
      確認も完了**(下記「P12の実機検証」参照。ユーザーから「OKでした」
      との回答を得た)。

- [x] **P13** — GitHub の PR 運用・CI・リリース自動化(完了)
      1.0.0 として機能が揃ったので、足回り(PR運用・CI・リリース)を整えた。
      着手直後に**リポジトリがこの開発機の外で再現不能**という致命的な
      問題が見つかったので、まずそこから潰している(P12 で当てた libgme の
      独自パッチがローカルにしか存在せず、`git clone --recurse-submodules`
      が他所では失敗する状態だった)。
      成果物: libgme を public フォークへ切り替え / shellcheck の検査を
      実際に効かせて全シェルスクリプトへ拡張 / テストハーネスのリークを
      潰して LeakSanitizer を有効化 / GitHub Actions の CI(ホストビルド +
      CTest、ASan・UBSan) / master 直 push を防ぐ pre-push フックと PR
      テンプレート / `CHANGELOG.md` / `scripts/release.sh` /
      タグ検証ワークフロー(Release Guard)。
      詳細は下記「P13の設計判断」を参照。

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

`CMakeLists.txt`の`project(mugbs VERSION x.y.z ...)`を唯一のバージョン
情報源とした。P7時点で0.9.0とした理由: MUST要件(F-01〜F-08)は満たしているが
SHOULD/NICE要件(F-10チャンネルミュート/F-14ビジュアライザ/F-20 EQ等、
P8)が未実装のため。**P8完了に伴い1.0.0へ上げた**(下記「P8の設計判断」参照)。

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

## P8の設計判断

### ビジュアライザ(F-14): multi-channelを採らず混合出力の波形にした

SPEC 6.1 は Player 画面に「4chメータ」と書いており、チャンネルごとの
音量バーが理想ではある。しかし libgme の公開C APIで**チャンネル別のPCMを
取り出せる手段は `gme_new_emu_multi_channel()` の1つしかない**ことを
ヘッダとソースで確認した(`vendor/game-music-emu/gme/gme.h:246-289`)。
これは次の理由で採らなかった:

1. `gme_open_file()`/`gme_open_data()` は emu を single channel mode に
   固定する(`gme.h:257` に明記)。`Music_Emu::set_multi_channel_()` は
   `require(!sample_rate())` を持つため、**開いた後に切り替えることはできない**。
   採用するなら `gme_identify_file` → `gme_new_emu_multi_channel` →
   `gme_load_file`/`gme_load_data` へ `player.c` の emu 生成経路を
   全面的に書き換える必要がある(zip/m3u の分岐も含めて)。
2. multi-channel では `gme_play()` の出力が常に **8ボイス×ステレオ=16ch**
   固定になり(`Music_Emu.h:180`)、オーディオコールバックが毎回
   8ペアぶんのダウンミックスを行うことになる。加えて
   `Effects_Buffer(1)` が `Effects_Buffer(8)` に変わり
   (`gme.cpp:228-240`)、Blip_Buffer の本数が 7 から 56 へ増える。
   実機は Cortex-A53 で、この増分が許容できるか事前に読めなかった。
3. SPEC 3.2 F-14 自身が「4chのボリュームバー **or** 波形」と
   どちらでもよいとしている。

そこで、既存のオーディオ経路に一切手を入れずに済む**混合出力からの
簡易オシロスコープ**を採った。`audio_callback()` が音量適用後の出力を
モノラルへ落として間引き、`mugbs_audio_t` のリングバッファへ積む。
描画側は `audio_snapshot_scope()` で古い順に取り出す。

- **`SDL_atomic_t` ではなく `audio_lock` を使う**: `volume`/`track_ended`
  と違って単一の値ではなく配列全体の一貫性が要るため。512バイトの
  memcpy でオーディオコールバックを止める時間は無視できる。
- **トリガ(立ち上がりゼロ交差)を入れた**: 単に直近N点を並べるだけだと
  オーディオコールバック(約46ms周期)と描画フレーム(約16ms周期)の位相が
  毎回ずれ、波形が横に流れて何も読めない。リングの前半からゼロ交差を
  探して左端に揃え、常に後半の128点を描くことで、表示する時間窓の
  長さも一定に保っている。
- **表示ゲイン3倍(`ui.c` の `WAVE_DISPLAY_GAIN`)**: GBSの出力は
  libgmeの既定ゲイン(`Gbs_Emu` は `set_gain(1.2)`)でもshortの
  フルスケールには届かず、素直に写すと中央に潰れて形が読めない。
- 一時停止・停止時はリングを無音で埋める(`audio_clear_scope()`)。
  そうしないと直前の波形が凍りついたまま残る。

### チャンネルミュート(F-10): 実装後にユーザー判断で削除した

当初は SPEC 6.3(「Player の Select でチャンネルミュートパネル」)に従い、
`gme_mute_voices()` を emu オープン直後と再生中(`player_apply_config()`)の
両方に配線し、Player 画面 SELECT で開くパネル(`SCREEN_VOICES`)、
T-10 を自動検証する `tests/test_mute.c`、そのための「実際に4ch鳴る」
合成GBSフィクスチャまで実装し、ホストで動作確認済みだった
(SPEC 6.1 の「Settings に EQ・チャンネルミュート」と 6.3 の食い違いは
6.3 を採る判断もしていた -- 波形を見ながら切り替えられる方が実用的で、
ミュートは「設定」より演奏中の操作に近いため)。

しかし実機投入直前にユーザーから「この機能は要らない」との判断があり、
関連コードを一式削除した(`SCREEN_VOICES`・`voices_*`関連の入力/描画
ハンドラ・`player_t.voice_count`/`voice_names[]`・
`config.h`の`voice_mute_mask`/`MUGBS_MUTABLE_VOICES`・
`tests/test_mute.c`・`ui_smoke.script`のパネル操作)。
`config.c`の`KEYS[]`から`[voices] mute_mask`も除いたため、
このキーを含む古い`config.ini`は「未知のキー」としてWARN付きで
無視される(既存の後方許容の仕組みがそのまま効く)。

合成GBSフィクスチャ(`tests/gen_fixture_gbs.c`)が「実際に4ch鳴る」実装
だけは残した。ミュート判定には使わなくなったが、ビジュアライザ(F-14)の
波形が実際に動くことをホストで目視確認する手段として引き続き有用なため。

EQ(`eq_bass`/`eq_treble`)は削除対象ではないため、そのまま
`SETTINGS[]` へ2行足すだけで実装が完結している。

### EQ(F-20): ノブ(-100..100)から libgme の物理量への変換

`config.ini` の `eq_bass`/`eq_treble` は SPEC 7 が「0が中立」の対称な
整数として例示しており、`config.c` は -100..100 にクランプする。一方
libgme の `gme_equalizer_t` は物理量そのもので、単位も向きも違う:

- `treble` … dB (0=フラット, -50=こもる, +5=きらびやか)
- `bass` … 低音が落ち始める周波数(Hz)。**値が大きいほど低音が減る**

特に `bass` は大小の向きが直感と逆で、素通しすると Settings で
「+ を押すほど低音が痩せる」ことになる。そこで `src/eq.c`(新規、
純libc。SPEC 4.2 のモジュール表には無い追加。app.c と同じ扱い)に
変換関数を切り出した:

- `eq_treble_db()`: 0 → -1.0dB、-100 → -47.0dB(`Gbs_Emu::handheld_eq`)、
  +100 → +5.0dB。負側と正側で可動域が大きく違う(46dB対6dB)ため区分線形。
- `eq_bass_freq()`: 0 → 120Hz、-100 → 2000Hz、+100 → 15Hz。**向きが反転する**。
  周波数なので等比(対数)補間する -- 線形にするとノブの効きが中央に偏る。

**0 が `Gbs_Emu` の既定 `make_equalizer(-1.0, 120)`(`Gbs_Emu.cpp:44`)に
一致していることが重要**。ここがずれると「EQ を触っていないのに
config.ini を作っただけで音が変わる」ことになる。`gme_equalizer()` で
読み戻して既定値が正確に `(-1.00, 120.00)` であることを実測確認した。

適用箇所は `gme_set_stereo_depth` と同じ2箇所(emuを開いた直後の
`configure_new_emu()` と、再生中の `player_apply_config()`)。
`Classic_Emu::set_equalizer_()` は `update_eq()` と `buf->bass_freq()` を
呼ぶだけで再確保しないため、`audio_lock` 内なら再生中に呼んで安全。
なお**必ず `gme_open_*()` の後に呼ぶこと** --
`Classic_Emu::setup_buffer()` がロード時に `set_equalizer(equalizer())` を
呼ぶが、その前は `buf` が未確定で `bass_freq` が反映されない。

### 合成GBSフィクスチャを「音が出る」ものへ作り替えた

P7 までの `tests/gen_fixture_gbs.c` は init/play とも `RET` だけで、
**一切音を出さない**擬似GBSだった(プレイリスト構築の検証には十分だった)。
これでは(当時実装していた)T-10「4chミュート: 該当チャンネルのみ無音に
なる」を機械的に検証できないため、init ルーチンに GB APU のレジスタ
書き込み列(`LD A,n` / `LDH (n),A` の羅列)を生成させ、4ボイスすべてが
同時に鳴るようにした。長さカウンタもエンベロープも使わないので、
トリガ後は一定音量で鳴り続ける。

レジスタの書き順には意味がある。libgme の `Gb_Apu::write_register()` は
電源OFF中に NR51 を書くと `osc.enabled` を落とすため、**NR52(電源)を
最初に**書く必要がある。

ミュート機能自体は削除したが(上記参照)、この「音が出る」実装は
ビジュアライザ(F-14)の波形をホストで目視確認する手段として残した。

### 開発用オプション `--screenshot` を追加した

実機には `/dev/fb0` をダンプするという確認手段があるが(P5/P7で活用)、
ホストにはそれが無く、波形のレイアウトを目で見る手段が無かった。
`--ui-script` と同じ非公開オプションとして `--screenshot FILE` を足し、
`ui_save_screenshot()`(`SDL_RenderReadPixels`+`SDL_SaveBMP`)で終了直前の
1フレームをBMPへ書き出せるようにした。`SDL_VIDEODRIVER=offscreen`でも
動くため、ヘッドレスのまま6解像度ぶんのレイアウトを機械的に確認できる。
これで実機へ持って行く前に「320x240で波形が他の要素を押し出さないか」を
潰せた。

### ASan/UBSanでの既知の失敗(P8とは無関係)

ASan ビルドでは `test_playlist`/`test_archive`/`test_browser` の3つが
LeakSanitizer で失敗する。いずれもテストハーネス自身の `path_in()` が
`strdup()` した文字列を意図的に解放していないためで(「プロセスは短命な
テストなので解放しない」とコメントに明記されている)、プロダクション
コードのリークではない。P8で追加・変更したコード(`test_eq`/`test_scope`/
6解像度の`test_ui_smoke`)はすべて ASan/UBSan で緑。

### D-pad長押しリピートを追加した(実機検証で発覚したUX課題)

.muxapp を実機(muOS 2601.0 JACARANDA、192.168.0.20)へインストールし、
アプリ一覧から物理ボタンで起動して Player→SELECT無反応確認→Settings→EQ
変更、という一連の操作をユーザーに試してもらったところ、
「十字キー押しっぱなしで値を変えたりカーソル移動できないのがしんどい」
というフィードバックがあった。

原因はキーボードと GameController の非対称性: `input.c` の
`SDL_KEYDOWN` 処理は `ev.key.repeat`(OSレベルのキーリピート)に乗って
UP/DOWN/LEFT/RIGHT だけリピートを通していたが、`SDL_CONTROLLERBUTTONDOWN`
は押した瞬間の単発イベントしか来ない(SDL_GameController にOSレベルの
キーリピートに相当するものが無い)。ホストでの開発・CIは主にキーボードか
`--ui-script` (単発アクションの注入)で検証していたため、この非対称性は
実機で物理ボタンを使うまで顕在化しなかった。

`input_t` に `dpad_held[4]`/`dpad_next_repeat_at[4]` を追加し、
`SDL_CONTROLLERBUTTONDOWN`/`UP` で押下状態を追跡、`input_poll()` が
`SDL_PollEvent` にイベントが無いとき(＝それ以上処理すべき入力が無い
アイドルなポーリング)にリピートを合成して返す方式にした。新しいSDLイベント
を待つ専用のティック関数を足さずに済むよう、既存の「イベントが無ければ0を
返す」契約の中に埋め込んだ(呼び出し側の `app.c` メインループは無改造)。
初回リピートまで350ms、以降70ms間隔(キーボードのOSリピートに近い体感)。
GameController切断時は `dpad_held[]` をクリアし、幽霊リピートが残らない
ようにした。対象はキーボード側と同じくUP/DOWN/LEFT/RIGHTのみ(A/B等は
意図的な単発操作を守るため対象外)。

実機で D-pad を押しっぱなしにしてカーソル移動・値変更が連続で効くことを
ユーザーに確認してもらい、問題ないとの回答を得た。

## P9で対応する実機フィードバック（対応済み。以下は着手前に書いた設計メモ）

> **注意**: この節は実装前に書いた調査メモで、実装時に方針を変えた箇所が
> ある(1番のマージ方法と、4/5番のUP/DOWNの割当)。確定した内容は次節
> 「P9の設計判断」を参照すること。行番号も実装で動いているので当てにしない。


v1.0.0を実機で使い込んだユーザーから5件のフィードバックがあった。
実装はまだしていない（この節は次セッションが着手する際の設計メモ）。
5件のうち曖昧さが残る箇所は `AskUserQuestion` で確認済みで、
以下は確定した決定事項として記録する。

### 1. zip内に複数の`.m3u`があると1曲しか再生されないバグ

**原因（実データで確認済み）**: `src/playlist.c` の `playlist_open_zip()`
(`m3u_idx` を探すループ、`archive_find`/`is_m3u` 判定の直後)が、zip内に
`.m3u` が複数あった場合「最初の1つだけ使い、残りは警告して捨てる」実装に
なっている。ところが zophar.net 配布パックには**1曲ごとに個別の`.m3u`を
同梱する形式**が実在する。実機の
`Downtown Special - ... (EMU).zophar.zip` を `unzip -l` で確認したところ:

```
DMG-JXJ.gbs
01 BGM #01.m3u   (163 bytes、1トラックだけを指す)
02 BGM #02.m3u
...
18 Jingle #01.m3u
```

のように **18個の単曲m3u** が入っており、内容は
`DMG-JXJ.gbs::GBS,0,BGM #01 - ...,0:39,,10` のように1エントリだけ。
現状の実装は `01 BGM #01.m3u` だけを採用するため `[1/1]` としか
再生できない(実機ログの `再生: [1/1] "BGM #01 - ..."` で確認済み)。

**修正方針**: `playlist_open_zip()` の「複数m3uは最初の1つだけ」ロジックを
撤廃し、**zip内の全`.m3u`を `build_from_m3u_text_zip()` に順番に通して
1つのプレイリストへマージする**(既存の「1ファイルが複数セグメントを持つ
m3u」を複数ソースとして扱うのと同じモデル)。ファイル名のソート順
(zip列挙順。`archive_list()` は中央ディレクトリの列挙順を返す。
辞書順ソートしたい場合は列挙後にqsortが要るか要確認)で連結すれば
`01, 02, ..., 18` の順に自然と並ぶ想定。同じ`.gbs`を18回開き直す
(ソースごとに`archive_extract`+`gme_open_data`)ことになるが、
初回スキャン(`playlist_open`)は1回きりの処理であり許容範囲。

**検証**: SPEC T-14として追加済み。`tests/test_playlist.c` に近い形で、
1つの合成GBSを指す複数の単曲m3uをzipに固めて `playlist_open()` に通し、
`entry_count` が m3u の数だけ増えることを確認するテストを足すとよい
(miniz でメモリ上にzipを組み立てるヘルパが要る。`tests/test_archive.c`
の zip構築方法を参考にできる)。

### 2. 音量調整機能を完全に削除する

実機のハードウェア音量(muOS/OS側のミキサー)と本アプリのソフトウェア
音量が別々に効き、「アプリ内の音量を上げたつもりが本体側が絞られたまま」
のような紛らわしさがあるとのフィードバック。ユーザーの判断で
**音量調整機能そのものを削除し、常に最大出力(100)で `gme_play()` する**
ことにした(Settingsに「100固定で表示だけ残す」案もあったが、
「完全に削除」を選択)。

削除対象(このセッションでの調査で洗い出し済み。すべて `grep -n volume`
で見つかる):
- `src/config.h:37` の `int volume;` フィールド
- `src/config.c:59` の `KEYS[]` エントリ(`{"audio","volume",...}`)、
  `src/config.c:171` の既定値代入(`c->volume = 80;`)
- `src/audio.h`/`src/audio.c` の `mugbs_audio_t.volume`
  (`SDL_atomic_t`)、`audio_set_volume()`、`audio_callback()` 内の
  ソフト音量の乗算ループ(`src/audio.c:31-35`あたり)。**乗算ループごと
  削除してよい**(常に100倍のショートカット経路だけが残る形になる)
- `src/player.c:110`(`player_init`)と `src/player.c:284`
  (`player_apply_config`)の `audio_set_volume(&p->audio, ...)` 呼び出し
- `src/app.c:328-338`あたりの `handle_player_input()` の
  `INPUT_UP`/`INPUT_DOWN`(音量+/-)ケース。ここを削除すると
  Player画面のUP/DOWNが空くので、そのまま下記4番目のファイル切替に使う
- `src/app.c:452`あたりの `SETTINGS[]` の `"Volume"` 行
- `src/app.c:683`あたりの `draw_player()` の `status_line` から
  `vol:%d` 表示を除く(`repeat:%s` だけになる)
- `SPEC.md` の config.ini サンプル・6.1画面表は今回のセッションで
  先行して修正済み(`[audio] volume` キー削除、Playerの音量表示記述削除)

`audio_callback()` の音量ループを削除すると、`Uint8 *stream` を直接
`gme_play()` へ渡すだけの経路になり、既存の「volume==100のときは
乗算をスキップする」高速パスがコード上も実体として常時有効になる
(コメントもそれに合わせて更新すること)。

### 3. リスト画面のカーソルを端で折り返す(ラップアラウンド)

**対象**: 単純なUP/DOWNカーソル移動のみ。**ページ送り(Browserの
LEFT/RIGHT、`browser_page()`)は対象外(現状のクランプのまま)**
—— 複数件ジャンプするページ送りを折り返すと、押した回数によって
着地位置が毎回変わり分かりにくくなるため、意図的に対象から外す
(ユーザーへの確認では明示的に問うていないが、依頼文が
「カーソル移動」と言っている範囲で解釈した。実装時に違和感があれば
再度確認すること)。

修正箇所:
- `src/browser.c:116-121` の `browser_move()`。現状は
  page移動と共用の関数なので、単純1ステップの場合だけ折り返す
  引数(例: `browser_move(browser_t*, int delta, int wrap)`)を足すか、
  `INPUT_UP`/`INPUT_DOWN`専用の薄いラッパを新設するか、どちらかを選ぶ。
  `browser_page()` は既存の(wrap無し)呼び出しのままにすること
- `src/app.c` の `handle_tracklist_input()` の `INPUT_UP`/`INPUT_DOWN`
  (現在は `if (sel > 0) sel--;` 式のクランプ)
- `src/app.c` の `handle_settings_input()` の `INPUT_UP`/`INPUT_DOWN`
  (同様のクランプ)
- 下記4/5番目で新設する Player画面のファイル一覧のカーソルにも同様に
  適用すること

### 4. Player画面のボタン再割当

**新マッピング**(SPEC 6.3は先行して更新済み):

| ボタン | 旧 | 新 |
|---|---|---|
| UP/DOWN | 音量+/-(2番の削除で空く) | 前/次の`.gbs`ファイルへ切替(即再生) |
| LEFT/RIGHT | シーク -5s/+5s | 前/次トラック |
| L1/R1 | 前/次トラック | シーク -5s/+5s |
| L2/R2 | 前/次ファイル | (未使用) |
| Select | (P8でミュートパネル→既に未使用) | (未使用のまま) |

`src/app.c` の `handle_player_input()`(現状 `INPUT_UP`/`INPUT_DOWN`が
音量、`INPUT_LEFT`/`INPUT_RIGHT`がシーク、`INPUT_L1`/`INPUT_R1`が
`player_prev_track`/`player_next_track`、`INPUT_L2`/`INPUT_R2`が
`app_prev_source`/`app_next_source`)を丸ごと入れ替える。
`app_prev_source()`/`app_next_source()`(`src/app.c:255-291`、
source(=ファイル)を跨ぐ最初のエントリへジャンプするロジック)は
そのまま使い、呼び出し元を`INPUT_L2`/`INPUT_R2`から`INPUT_UP`/`INPUT_DOWN`
へ移すだけでよい。同様に`player_prev_track`/`player_next_track`の
呼び出しを`INPUT_L1`/`INPUT_R1`から`INPUT_LEFT`/`INPUT_RIGHT`へ、
シーク処理(`player_tell_ms`+`player_seek`、現在`INPUT_LEFT`/`INPUT_RIGHT`
ケースの中身)を`INPUT_L1`/`INPUT_R1`へ、それぞれ移設する。

### 5. Player画面にファイル一覧を追加、波形を下部へ移動

**内容**: Player画面の中央に「現在のファイルが属するディレクトリの
ファイル一覧」(Browserと同じ`.gbs`/`.m3u`/`.zip`一覧。
`show_all_files`設定も反映)を表示し、再生中のファイルをハイライトする。
波形ビジュアライザ(現在ステータス行の下、`src/app.c:692-699`あたり)は
その下に移動する。UP/DOWN(4番の新マッピング)を押すと、この一覧上で
カーソルが連動して動き、即座に該当ファイルへ切り替わって再生を始める
(TrackListのようにカーソルだけ動かして決定ボタンで開く方式ではなく、
即切替。ユーザー確認済み)。

**実装方針**:
- `app_t`(`src/app.c`)に `browser_t player_files;` のような専用の
  `browser_t` インスタンスを追加する(Browser画面の `app->browser` とは
  別物。Player中は`app->browser`を破壊したくない —— Browserへ戻ったとき
  にカーソル位置が保持されている必要があるため)。ファイルを開くたび
  (`app_open_path()`)に、そのファイルの`dirname`で
  `browser_open_dir(&app->player_files, dir, app->cfg->show_all_files)`
  を呼んで再構築し、`browser_select_by_name()`で現在のファイル名へ
  カーソルを合わせる
  - ただしzip内の`.gbs`を開いている場合(`playlist_source_t.zip_entry`
    が非NULL)、「ディレクトリ」の概念がファイルシステム上に無いので
    どう扱うか要検討(zipのあるディレクトリ = zipファイル自身を1項目
    として見せる、等)。今回のフィードバックの主眼はSDカード上の
    `.gbs`を直接ブラウズする場合と思われるので、zip内ファイルの扱いは
    実装時に確認するか、素直に「zipファイルが属するディレクトリ」を
    見せる(=zip自身がハイライトされる1項目として一覧に出る)のが
    自然
  - UP/DOWNで一覧のカーソルを動かしたら、`browser_selected_path()`で
    パスを取り、`app_open_path()`(既存。ファイルを開いて即再生開始する
    関数)をそのまま呼べばよい
- 描画(`draw_player()`, `src/app.c:622`以降): タイトル・メタ情報・
  シークバー・ステータス行のレイアウトはそのまま、その下に
  `ui_draw_list()` + 新規`player_files_item_text()`でファイル一覧を挟み、
  波形(`ui_draw_waveform()`呼び出し)をさらにその下へ移動する。
  高さ配分は要調整(現状波形は`line_h*5`固定。一覧を挟むぶん、
  低解像度での省略ガード(`wave_avail`判定と同様のもの)を一覧側にも
  用意すること)
- カーソルのラップアラウンドは3番の対応と合わせて、このファイル一覧にも
  適用する

## P9の設計判断（上のメモからの変更点・確定した内容）

### 1. zip内の複数m3uは「連結して1回だけ解析」した

メモでは「各m3uを順に`build_from_m3u_text_zip()`へ通してsourceを18個作る」
案だったが、実装時に`src/m3u.c`の`m3u_split_segments()`を読み直して
**全m3uのテキストを名前順に改行区切りで連結し、1回だけ通す**方式に変えた。

`m3u_split_segments()`は「参照ファイル名が直前と変わったときだけ新しい
セグメントを開始する」実装なので、同じ`.gbs`を指す18行は連結すると自然に
1セグメント＝1ソースにまとまり、`gme_load_m3u_data()`へ18エントリのm3uが
渡って18トラックになる。個別方式に対する利点:

- 表示が`Track 1/18`と正しくなる(個別方式では全曲が`1/1`になる)
- トラックを移るたびに`archive_extract`+`gme_open_data`をやり直さずに済む
- 異なる`.gbs`を指すm3uが混在しても既存のセグメント分割が正しく分ける

`archive_list()`は中央ディレクトリの列挙順を返すだけでソートしないため、
連結前に`strcasecmp`で名前順へ並べ替えて曲順を保証している(`01`〜`18`)。
`archive_extract()`の戻りはNUL終端されないので、連結は一貫して長さで扱う。

### 2. Player の UP/DOWN は「カーソル移動のみ」、決定は A

メモの4番(UP/DOWN=`app_prev_source`/`app_next_source`)と5番(UP/DOWN=
ファイル一覧のカーソル+即再生)は食い違っていた。ユーザーに確認し、
**5番を採用**、さらに**「即再生」ではなく確定ボタン(`A`)を押して初めて開く**
方式に変更した。

`A`を決定に回したぶん、再生/一時停止は全画面で未使用だった`SELECT`へ移した
(Browser/TrackListと同じく`A`＝決定で揃えるほうが実機で迷わないため)。
`app_prev_source()`/`app_next_source()`は削除せず`L2`/`R2`に残した。
UP/DOWNの「ディレクトリ内の別ファイルへ切替」とは別概念で、サイドカーm3uが
複数の`.gbs`を参照する場合などに今も効くため。

確定ボタン方式にしたことで、D-padの長押しリピート(350ms後から70ms間隔)で
`playlist_open()`が連打される問題が起きず、長押しで一覧を高速スクロール
できるという副次的な利点も得られた。

`SELECT`単独押しは`START+SELECT`同時押しでの終了と衝突しない。`input.c`は
`held_mask`で同時押しを判定してから`a`を`INPUT_QUIT`へ上書きする実装なので、
`START`を押していないときの`SELECT`は素通りする。

### 3. Player画面のファイル一覧の作り

- **`app_t`に2つ目の`browser_t`(`player_list`)を持たせた。** `app->browser`と
  共有しないのは、再生中にBrowserで別フォルダを眺めてもPlayer側の一覧が
  動いてはならず、逆にPlayer側でファイルを送ってもBrowserのカーソル/
  スクロールを壊してはならないため(`browser_open_dir()`は`selected`/`scroll`
  を0へ戻す)。
- **一覧の元にするパスは`app_open_path()`に渡されたパスそのもの**
  (`app_t.player_path`)。`playlist_source_t.fs_path`はzip内ソースではNULLで、
  かつ`.zip`を開いたときに見せたいのは「zipを含むディレクトリ」
  (ハイライトされるのはその`.zip`自身)なので、`.gbs`/`.m3u`/`.zip`のどれでも
  一貫して使える値はこれしかない。
- **ディレクトリは一覧に出さない。** この一覧は「いま聴いているフォルダの中で
  ファイルを選び直す」ためのもので、階層を辿るのはBrowserの役目(`B`で戻れる)。
  `items[]`は「ディレクトリ優先→名前順」(`browser.c`の`item_cmp`)なので、
  最初の非ディレクトリ添字(`player_list_first_file`)を1つ覚えておけば
  ファイル領域は連続スライスとして切り出せる。
- **添字の空間が2つある。** `player_list.selected`と`player_list_playing`は
  `items[]`の添字(アイテム空間)、`player_list.scroll`は`ui_draw_list()`だけが
  触る表示空間(ファイル領域の先頭が0)。`player_list`に対して
  `browser_move()`/`browser_page()`/`browser_enter()`/`browser_up()`を
  呼んではならない。
- **同じディレクトリなら`readdir`をやり直さない。** SDカード上で無駄なだけで
  なく、`browser_open_dir()`の`scroll`リセットで一覧が跳ねる。
  `show_all_files`を切り替えたときだけ`force_rescan=1`で作り直す。
- 同期は`app_open_path()`の成功時1箇所にフックするだけでよい。これがGUI側で
  `playlist_open()`を呼ぶ唯一の関数なので、Browserの決定・argvの
  `initial_path`・Player一覧からの決定の3経路すべてを覆える。
  `restore_last_path()`/`--start-dir`はファイルを開かない(意図的に自動再生
  しない)のでフック不要。
- `browser_path_at()`を新設し、`browser_selected_path()`をその薄いラッパに
  した。カーソルを動かさずに移動先候補のパスを取るため。

### 4. レイアウトの高さ配分

ステータス行の下からフッタ帯の上までを
`[一覧] → [波形] → [一時メッセージ用の1行]` で**行単位に**分け合う。
操作に必要な一覧を優先し(最大7行)、装飾である波形が余り(2〜5行)をもらう。
両立できない極端な低解像度では波形を諦め、それも取れなければ両方省く
(フッタへはみ出させない。SPEC 6.2)。

`ui_draw_list()`は`r.h == 0`でも`visible`を1へ切り上げて1行描いてしまうので、
**行数0のときは絶対に呼ばないこと**。

一時ステータスメッセージはフッタ帯の直上に固定配置してある。上の行割り当ての
端数で位置が動かないようにするため、ここでは描画カーソル`y`を使わない。

### 5. カーソル折り返しは新関数にした

`browser_move()`に引数を足すのではなく`browser_move_wrap()`を新設した。
`browser_page()`が`browser_move()`を呼んでいるため、既存関数を折り返すように
変えるとページ送りまで巻き添えになるから。ページ送りを折り返さないのは、
何件も飛ぶ移動が折り返すと押した回数によって着地位置が毎回変わり
分かりにくくなるため(意図的に対象外)。

TrackListはエントリ0件のとき`n-1`が負になるので剰余の前にガードが要る。
Settingsは`SETTINGS_COUNT`がコンパイル時定数なのでガード不要。

### 6. 旧バージョンのconfig.iniへの配慮

実機には`[audio] volume`(P9で廃止)と`[voices] mute_mask`(P8で廃止)を含む
`config.ini`が残っている。どちらも未知のキー/セクションとして黙って飛ばされ、
前後の有効なキーは通常どおり効く。これを保証する
`test_legacy_keys_from_older_version()`を`tests/test_config.c`に追加した。

## P9の実機検証（完了）

`.muxapp`(muGBS-1.0.0)を実機(muOS 2601.0 JACARANDA、192.168.0.20)へ
`scp`+`extract.sh`で投入し、以下6項目をユーザーに確認してもらい、
「動作確認OK」との回答を得た(詳細な項目別の当たり外れは聞いていないが、
全体としてP9の変更が実機で問題なく動くことを確認済み):

1. zophar.netの複数m3u入りzip(`Downtown Special … (EMU).zophar.zip`)を開き、
   `Track 1/18`と表示され18曲すべて再生できること
2. Settingsに Volume が無く、終了後の`config.ini`に`[audio] volume`が
   書かれないこと
3. Browser/TrackList/Settings/Playerの一覧のカーソルが端で折り返すこと、
   ページ送り(BrowserのLEFT/RIGHT)は折り返さないこと
4. Playerで UP/DOWN が一覧のカーソル移動、`A`がそのファイルを開く、
   `SELECT`が再生/一時停止、LEFT/RIGHTがトラック切替、L1/R1がシーク、
   L2/R2が前/次ソースになっていること
5. Player中央にファイル一覧が出て、カーソル(青)と再生中(黄)が別々に
   描き分けられること。UP/DOWN長押しで一覧を高速スクロールしても破綻しない
   こと
6. `SELECT`単独押しで誤終了しないこと(START+SELECT同時押しの終了は残っている
   こと)

引き続き実機で使ったユーザーから新たに4件のフィードバックがあり、P10として
下記に記録して対応する。

## P10で対応する実機フィードバック

### 1. Settingsフッタの `(next track)` 注記を削除

`setting_def_t.note`(Default length/Fadeにだけ付いていた「次のトラックから
反映される」という注記)がフッタに `(next track)` と表示されるのを、
ユーザー判断でUIから消した。反映タイミングの説明自体は
`app_apply_settings()`直前のコメントに残っており、情報が失われるわけではない。
`note`フィールドごと`setting_def_t`から削除した(使われなくなったフィールドを
残さない)。

### 2. Settings画面に設定リセット機能を追加

`X`でリセット確認ダイアログを開き、`A`でSETTINGS[]に載っている項目
(Repeat・Shuffle・Stereo depth・EQ bass・EQ treble・Default length・Fade・
Show all files)だけを既定値に戻す。`B`/`X`/`START`でキャンセル。

`last_path`・`gamecontroller_db`・`controller_mapping`・`sample_rate`は
Settings画面に出てこない値なので対象外にした。これらをユーザーの意図せず
消してしまうと、直近パスやコントローラマッピングが吹き飛んで実機での
体験を損なう。`config_set_defaults()`で作った一時的な`mugbs_config_t`から
`SETTINGS[]`に載っている分だけ`setting_get`/`setting_set`でコピーする実装
にしたことで、この対象範囲の限定が自然に実現できている。

確認ダイアログは新しい画面(SCREEN_*)を作らず、`app_t.settings_confirm_reset`
という1つのbool フラグで表現した。`handle_settings_input()`の先頭で
このフラグを見て、真なら通常の入力(カーソル移動・値調整)を全てブロックし
A(確定)/B・X・START(キャンセル)だけを受け付ける専用分岐へ飛ばす。
描画側(`draw_settings_confirm_reset()`)は通常のSettingsリストの上に
矩形+文字を重ねて描くだけの薄いオーバーレイ。

### 3. Player画面の波形をフッタ近くまで下げる

「波形の下限がフッターに付くくらい」という要望。P9時点のレイアウトには
2つの無駄があった: (a) 一時ステータスメッセージ用に`line_h`1行ぶんを
常時確保していた(実際にメッセージが出るのは稀)。(b) 波形の高さに
`PLAYER_WAVE_MAX_ROWS=5`という上限を設けていた。

両方外した: 一時メッセージは専用の行を確保するのをやめ、フッタ帯の直上に
オーバーレイとして(背景を塗ってから)描くよう変更。波形の上限は撤廃し、
一覧(最大`PLAYER_LIST_MAX_ROWS=7`行)に割り当てた残り全部を波形が使う。
List→Waveの間に挟む`pad`ぶんだけは、波形の下端が意図せずフッタ帯へ
はみ出さないよう`band_h`の計算から先に差し引いてある(詳細は
`draw_player()`のコメント参照)。

### 4. シャッフル再生(F-25)

`src/shuffle.c`/`shuffle.h`という新規の純ロジックモジュールを追加した。
SDLにもlibgmeにも依存しない(`eq.c`と同じ設計方針)ので
`tests/test_shuffle.c`でSDL_Initなしに単体テストできる。

**なぜ「並び順を保持するモデル」にしたか**: シンプルな実装として
「nextのたびに毎回ランダムな1件を選ぶ」も考えたが、それだと同じ曲が
連続して選ばれたり、一部の曲が長時間再生されなかったりする上、"prev"が
何を意味するか定義できない。代わりに、`[0, entry_count)`の順列
(`shuffle_t.order`)をFisher-Yatesで1回作り、`pos`というカーソルで
その中を前後する設計にした。これなら「prevはnextをちょうど巻き戻す」が
自然に成り立ち、1周すれば全エントリがちょうど1回ずつ再生されることも
保証される。

**shuffleと再生の同期(`sync_shuffle`, player.c)**: TrackListからのジャンプ・
L2/R2でのソース切替・Player一覧からの決定など、next/prevを経由しない
曲変更が何種類もある。これらの後で`shuffle.pos`が古いままだと、次に
next/prevを押したときに「いま聞いている曲」と無関係な場所から進んでしまう。
これを避けるため、`player_play_entry()`の最後(=あらゆる曲変更が必ず通る
唯一の関所)で`sync_shuffle()`を呼び、`shuffle.order`が現在の
`playlist->entry_count`と一致していなければ作り直し、`current_entry`の
位置へ`pos`を合わせ直す。`player_next_track()`/`player_prev_track()`の
先頭でも同じ関数を呼ぶ(shuffleを設定画面でオンにした直後、まだ
play_entryを経由していない場合の保険。冪等なので二重に呼んでも安全)。

**wrap時の再シャッフルの非対称性**: `shuffle_next()`が末尾から先頭へ
回り込むときは新しい並びを作り直す(同じ周回を繰り返さないため)。
一方`shuffle_prev()`が先頭から末尾へ回り込むときは並びを変えない
(直前まで見えていた並びを壊すと「前へ」の直感に反するため)。

**REPEAT_ONEとの関係**: `REPEAT_ONE`はシャッフルより優先する。
`player_next_track()`で`repeat_mode==REPEAT_ONE`なら`shuffle.pos`を
一切動かさず同じトラックを再開する(既存のシーケンシャル版と同じ扱い)。

**メモリ管理**: `shuffle.order`は`player_load_playlist()`(新しい
プレイリストに差し替えるとき)と`player_shutdown()`で明示的に解放する。
`config->shuffle`が偽になったときは`sync_shuffle()`が解放する。
ASanでmugbs本体を`ui_smoke.script`経由(LeakSanitizer有効のまま)走らせて
リークが無いことを確認済み。

**乱数のseed**: `main()`の先頭で`srand((unsigned)time(NULL))`を1回呼ぶ
(GUI・CLIハーネスの両方に効く)。テスト(`test_shuffle.c`)側ではseedを
固定せず、「有効な順列であること」「wrap/no-wrapの境界」「syncの正しさ」
といった、具体的な乱数値に依存しない構造的性質だけを検証している。

**UIへの露出**: シャッフルは全画面で使われていない専用ボタンが無かった
(P9でPlayer画面の全ボタンが埋まったため)ため、Settings画面の
`SET_BOOL`項目として追加した(`Show all files`と同じ扱い)。Player画面の
ステータス行にも`shuffle:on/off`を追記し、Settingsに入らなくても状態が
分かるようにした。

## P10の実機検証（完了）

`.muxapp`(muGBS-1.0.0、P10反映版)を実機(192.168.0.20)へ再転送・上書き
インストールし、ユーザーに動作確認してもらい「動作は確認できました」との
回答を得た。確認済み項目:

1. Settingsで Default length/Fade を変えてもフッタに `(next track)` が
   出ないこと
2. Settingsで `X` を押すとリセット確認ダイアログが出て、`A`で全項目が
   既定値に戻り、`B`でキャンセルできること(last_pathやコントローラ設定は
   変わらないこと)
3. Player画面の波形がフッタ帯の近くまで大きく表示されること
4. Settingsで Shuffle を有効にすると、次/前トラック(自動送りも含む)が
   ランダムな順になること。1周したら(Repeat:allなら)続けてランダムに
   再生されること。Repeat:oneがシャッフルより優先され、同じ曲が続くこと

実機確認の際、Settings画面まで入らないとShuffleを切り替えられないのが
面倒というフィードバックがあり、P11としてPlayer画面からの即時切り替えを
追加する(下記「P11で対応する実機フィードバック」参照)。

## P11で対応する実機フィードバック

「シャッフルの設定が面倒(Settings画面まで入らないと切り替えられない)」
というフィードバックへの対応。Player画面で`Y`を押しながらD-Padを押す
「コンボ」操作として、Repeat/Shuffleの2つを直接変えられるようにした。

**なぜ新しい画面やボタンを増やさなかったか**: P9でPlayer画面の全ボタン
(UP/DOWN/LEFT/RIGHT/A/B/X/Y/L1/R1/L2/R2/START/SELECT)が使用済みになって
おり、単独の空きボタンが無い。`Y`はP10まで「単体で押すとRepeatモードを
循環する」という単発アクションだったが、これを「押しながらD-Padの意味を
変えるモディファイア」に転用することで、新しいボタンを増やさずに
Repeat(左右)とShuffle(上下)の2つを同時に露出できた。

**設計判断**:
- **`input_t.y_held`という状態をinput.cに追加した。** D-pad長押しリピート
  (`dpad_held[]`)と同じ発想で、「Yが押されている間」を状態として持つ。
  キーボード(`S`キー)とGameControllerのYボタンの両方をここに集約する
  (`input.c`の`SDL_KEYDOWN`/`SDL_KEYUP`と`SDL_CONTROLLERBUTTONDOWN`/
  `SDL_CONTROLLERBUTTONUP`双方で更新)。
- **組み替えは「その時点のy_held」を見て動的に行う。** `apply_y_modifier()`
  という1つの関数に集約し、通常のボタン押下イベントだけでなくD-padの
  長押しリピート合成(`input_poll()`の無イベント分岐)からも呼ぶ。これにより
  「先にD-padを押してリピートが始まってから後でYを押す」「Yを押している
  途中でD-padを離して別方向を押す」といった押す順序の違いに関わらず、
  常にその瞬間の`y_held`で正しいアクションが選ばれる。
- **`Y+Up`/`Y+Down`はトグルではなく明示的なon/offにした。** D-Padの長押し
  リピート(初回350ms後から70ms間隔)でUP/DOWNが連射されるため、トグルだと
  押しっぱなしでシャッフルがちらつく。`Y+Up`=on、`Y+Down`=offという
  向きにすれば、連射されても同じ値を再代入するだけで安全。`Y+Left`/
  `Y+Right`のRepeat循環は3値の巡回なので連射で多少行き過ぎても実害が
  小さく、既存のD-pad長押しリピートの仕組みをそのまま使っている。
- **`Y`単体(押して離すだけ)は何もしなくなった。** 以前はYがリピート
  モード循環の単発アクションだったが、`Y+Left`/`Y+Right`へ移したことで
  重複を避けた(「Yを押すと何が起きるか」を「押しながらD-Pad」の1つの
  メンタルモデルに統一する)。
- **新しい`input_action_t`(`INPUT_Y_LEFT`/`_RIGHT`/`_UP`/`_DOWN`)を
  input.hに追加した。** app.c側は`handle_player_input()`にこの4つの
  caseを追加するだけで済み、Browser/TrackList/Settings側は対応する
  caseが無いので単に無視される(それらの画面でYを押しながらD-Padを
  操作しても、D-Padの通常動作が一時的に効かなくなるだけで実害はない
  - Y自体がそれらの画面で元々何もしないボタンだったため)。
- **`app_step_repeat_mode()`/`app_set_shuffle()`という2つの薄い
  ヘルパーをapp.cに新設した。** どちらも`app->cfg`への直接代入のみで、
  Settings画面の`adjust_setting()`のような`app_apply_settings()`呼び出しは
  不要(`repeat_mode`/`shuffle`は`player.c`が使う際に`cfg`ポインタ経由で
  都度読むだけで、即時反映のための特別な適用処理を持たないため。これは
  P10までの実装で確立済みの前提)。

**テスト**: `input.c`のY保持状態機械そのものはSDLイベント駆動で、
プロジェクト内に単体テストの前例が無い(D-pad長押しリピートも同様に
コード内テストは無く、実機/手動確認のみで検証してきた)。今回もその方針を
踏襲し、`tests/ui_smoke.script`に`Y_LEFT`/`Y_RIGHT`/`Y_UP`/`Y_DOWN`という
コンボアクションを直接注入するテストを追加した(`--ui-script`は
`input.c`を経由せず`input_action_t`をapp.cへ直接注入する仕組みなので、
「押しながら」のタイミング検出そのものはカバーできないが、
コンボアクションを受けたapp.c側の処理がクラッシュしないこと・
`app->cfg`が正しく変わることは確認できる)。640x480のスクリーンショットで
Playerのステータス行が`repeat:none shuffle:on`のように正しく変わることを
目視確認した。

## P11の実機検証（完了）

「Yを押しながらD-Padを押す」という実際の押下感を、P12修正と同じmuxapp
(既にインストール済みだったため再投入は不要だった)でユーザーに確認して
もらい、「それも確認済みです」との回答を得た:

1. Player画面で`Y`を押しながら`←`/`→`を押すと、Repeatモードが
   none→one→all→none…の順で(押した方向に応じて)進む/戻ること
2. Player画面で`Y`を押しながら`↑`/`↓`を押すと、Shuffleが明示的に
   on/offになること。押しっぱなしでもちらつかないこと
3. `Y`を単体で押して離しても何も起きないこと(以前のリピートモード循環が
   無くなっていること)
4. `Y`を押しながらでも一覧のカーソル移動(UP/DOWN)・トラック切替
   (LEFT/RIGHT)といった通常のD-Pad操作へ意図せず影響しないこと
   (Yを離せば通常どおり動くこと)
5. GameControllerが実機で認識されている状態で、Y+D-Padの組み合わせが
   キーボードと同じように機能すること

## P12: m3uトラック番号の0始まり/1始まり問題

P11を実機で確認したユーザーから「m3uで2曲目を再生すると、GBS内の1曲目が
再生される」という報告があった。

### 原因

同梱している`vendor/game-music-emu`(libgme)は、m3uに10進数で書かれた
トラック番号を「1始まり」とみなし、実際の索引として使う前に**内部で-1する**
仕様になっている(`gme/Gme_File.cpp`の`Gme_File::remap_track_()`)。
`$`始まりの16進数はこの-1の対象外(常に0始まりの生索引として扱われる)。

```cpp
if ( e.track >= 0 )
{
    *track_io = e.track;
    if ( !(type_->flags_ & 0x02) )
        *track_io -= e.decimal_track;   /* decimal_track: 10進で書かれていれば1、16進なら0 */
}
```

`flags_ & 0x02`が立っている形式(現状KSSのみ、`gme/Kss_Emu.cpp`)はこの-1が
適用されない。GBS(`gme/Gbs_Emu.cpp`)にはこのビットが立っておらず、
常に10進トラック番号から-1される。

実機の実データ(muOS機、`/mnt/sdcard/ROMS/VGM/GBS/Parodius (EMU).zophar/`)を
SSHで確認したところ、各m3uの10進トラック番号は**0始まり**だった:

```
01 Parodius Ondo.m3u        → DMG-PVJ.gbs::GBS,0,Parodius Ondo...
02 Hello.m3u                 → DMG-PVJ.gbs::GBS,1,Hello...
03 Theme of Vic Viper.m3u   → DMG-PVJ.gbs::GBS,2,...
...
28 Game Over!!.m3u            → DMG-PVJ.gbs::GBS,23,Game Over!!...
```

(zophar.net配布のGBSパックに共通の慣習。P9で複数m3uzip対応を入れたときの
`Downtown Special...zophar.zip`も同じく`GBS,0,...`だった。)

libgmeの「10進は1始まり」という前提とこの実際の慣習が食い違うため:
- 宣言「1」(`02 Hello.m3u`)は 1-1=0 されて、本来「宣言0」
  (`01 Parodius Ondo.m3u`)が指すはずの物理トラックを再生してしまう
  (ユーザー報告の「2曲目→1曲目」はこれ)
- 宣言「0」(`01 Parodius Ondo.m3u`)は 0-1=-1 という不正な索引になる。
  `remap_track_()`の範囲チェックは`*track_io >= raw_track_count_`だけで
  **負の値を弾かない**ため、エラーにはならず不正な索引のまま
  `Gbs_Emu::start_track_()`の`cpu::r.a = track;`まで届く(8bitレジスタへの
  暗黙変換で0xFFになり、意味の無いsubtrackが再生される)

GBSヘッダには`header_.first_track`というフィールドが存在するが、
`Gbs_Emu.cpp`内では一切参照されていない(`track_count`しか使わない)。
つまりGBSのネイティブなsubtrack選択(`cpu::r.a`に渡す値)はもともと
0始まりであり、「1始まり変換」という発想自体がGBSの実装と整合しない。

### 修正

`vendor/game-music-emu/gme/Gbs_Emu.cpp`の`gme_gbs_type_`の`flags_`に、
KSSと同じ`0x02`ビットを立てた(1行差分)。これでGBSの10進トラック番号も
16進と同様、-1されずそのまま生索引として使われる。

```cpp
static gme_type_t_ const gme_gbs_type_ =
    { "Game Boy", 0, &new_gbs_emu, &new_gbs_file, "GBS", 0x01 | 0x02 };
```

`vendor/game-music-emu`は本物のgit submoduleなので、修正はsubmodule側で
コミットしてから(ローカルのみ。upstream `libgme/game-music-emu`への
push権限は無い)、親リポジトリ側でgitlinkを更新してコミットする2段階に
なる。

### なぜ実機のGBSヘッダ側を疑わなかったか

`header_.first_track`が未使用という事実(grep一発で確認できた)が
決め手になった。もしGBSが「1始まり」を前提にしていたなら、
このフィールドを使って`cpu::r.a`に渡す値を補正するはずだが、
そのようなコードは存在しない。よって「1始まり」はlibgmeのM3U実装が
(おそらくGBS以外の一部形式を想定して)一律に適用した仮定であり、
GBS自体の設計とは無関係と判断した。

### なぜタイトルベースのテストではこのバグを検出できないか

`Gme_File::track_info()`の「m3u情報で上書きする」ロジックは、
`playlist[track]`(`track`は**remap前**、呼び出し側が渡したm3u内での
出現順)から`song`(曲名)を取る。GBS自体の`track_info_()`は渡された
トラック番号を一切使わない(GBSヘッダのgame/author/copyrightを
返すだけ)。つまり**曲名は常に「m3u上の何番目のエントリか」だけで決まり、
remapの正しさ(実際に鳴る物理トラック)を一切反映しない**。
これが、既存のタイトルだけを見るテスト群がこのバグを何年も検出
できなかった(そしてtest_playlist.cに新設したテストも、
libgmeパッチの有無に関わらず成功することを確認済み)理由。

### 検証方法(実データでのA/Bテスト)

ユニットテストでは検出できないため、実機のSDカードから実際の
`DMG-PVJ.gbs`と`01 Parodius Ondo.m3u`/`02 Hello.m3u`をSSH経由で
ローカルへコピーし、`SDL_AUDIODRIVER=disk`(SDLの組み込み機能。
実デバイスの代わりにPCMをファイルへ書き出す)を使って
`--cli --repeat none --track 1`でレンダリングし、Pythonで
FFTピーク周波数とRMSを比較した:

```
パッチ前 宣言0 (01 Ondo)  → 329Hz, rms 3721.3   (不正index由来の別物)
パッチ前 宣言1 (02 Hello) → 441Hz, rms 3522.6
パッチ後 宣言0 (01 Ondo)  → 441Hz, rms 3522.6   ← パッチ前の宣言1と統計値まで一致
パッチ後 宣言1 (02 Hello) → 330Hz, rms 3836.4   ← 新たに出てきた別の値
```

「パッチ前の宣言1」と「パッチ後の宣言0」が数値まで一致していることが、
「1つズレて再生されていた」ことと「そのズレが解消された」ことの
直接証拠になっている。

### 既存テストへの影響

`tests/test_playlist.c`のm3uテスト用フィクスチャは、上記の修正前提(1始まり)
で10進トラック番号を書いていたため(例: 3トラックのGBSに対し
`GBS,3,...`のように末尾トラックを1始まりで指定)、パッチ適用後は
範囲外(0始まりなら有効な索引は0..2)になって落ちた。全て0始まりに
書き換えた(`test_sidecar_m3u`/`test_open_m3u_directly`/
`test_multi_file_and_missing`/`test_zip_multiple_m3u`/`test_zip_single_m3u`)。
16進を使う`test_hex_track_number`はこの修正の影響を受けないため
変更していない。`tests/test_m3u.c`は`m3u_split_segments()`
(ファイル名だけを見るセグメント分割ロジック)の単体テストで、
トラック番号自体は解釈しないため無関係(変更不要)。

## P12の実機検証（完了）

`.muxapp`(muGBS-1.0.0、P12反映版)を実機(192.168.0.20)へ再転送・上書き
インストールし(md5一致でバイナリの取り違え無しを確認済み)、ユーザーに
以下を確認してもらい「OKでした」との回答を得た:

1. `Parodius (EMU).zophar`パック(またはユーザーが実際に使っている他の
   zophar.net配布パック)を開き、`01`から`28`まで順に再生して、
   曲名(m3uファイル名)と実際に鳴る曲が対応していること
   (「2曲目を再生すると1曲目が鳴る」というズレが解消されていること)
2. P9で扱った複数m3uzip(`Downtown Special...zophar.zip`)も、
   `GBS,0,...`始まりの実データだったため、このパッチの影響を受ける。
   18曲が正しい順・正しい曲で再生されること
3. 16進トラック番号を使うm3u(あれば)がこれまでどおり正しく再生されること
   (このパッチの影響を受けないはずだが、実データで確認)

なお、この検証で使ったmuxappはP11(Yコンボ)のコードも含んでいるが、
P11単体の実機検証(Yを押しながらD-Padを押す実際の押下感)はまだ行っていない
(下記「P11の残作業」参照。既にインストール済みなので再投入は不要、
Player画面でそのまま試せる)。

## P13の設計判断

### submodule が upstream に無いコミットを指していた（着手して最初に発覚）

CI を書く前に調べたところ、`.gitmodules` は
`https://github.com/libgme/game-music-emu`（upstream）を指しているのに、
親リポジトリが記録している gitlink は

```
160000 commit 84bcfe31111828de75b66385781352cc70000908  vendor/game-music-emu
```

で、これは **P12 でローカルに作った独自コミット**（`Gbs_Emu: treat decimal
m3u track numbers as 0-based, not 1-based`）だった。
`git -C vendor/game-music-emu branch -r --contains 84bcfe31` は空、つまり
どのリモートにも存在しない。この状態では他所での
`git clone --recurse-submodules` が
`Fetched in submodule path ... but it did not contain 84bcfe3` で失敗する。
**CI 以前に、リポジトリがこの開発機の外で再現不能だった。**

libgme を **public** でフォークし（`ka-zuu/game-music-emu` の `mugbs`
ブランチ）、`.gitmodules` の url をそちらへ向けて解決した。gitlink の SHA は
変わらないので親リポジトリの実質的な変更は `.gitmodules` だけ。
public にしたのは、private フォークだと Actions の既定の `GITHUB_TOKEN`
では読めず PAT / deploy key の管理が要るため。LGPL 的にもパッチ済みソースは
公開側に置いてある方が素直。submodule の `origin` はフォーク、`upstream` が
libgme 本家という remote 構成にしてある。

再発防止として、`scripts/release.sh` がリリース前に
`git -C <submodule> branch -r --contains <sha>` を見て、gitlink が origin
から取得できなければ落ちるようにした。SPEC 13 のチェックリストにも足した。

**別クローンで `git clone --recurse-submodules` → `build-host.sh` →
`ctest` 16件全緑になることを確認済み**（これが C1 の完了条件）。

### CI でクロスビルドをしない理由

muOS 実機向けのクロスビルドには `sysroot/`（実機から抜いた `libc.so.6` /
`libSDL2-2.0.so.0.2800.5` と upstream SDL2 のヘッダ）が必要で、これは
`scripts/fetch-sysroot.sh` で実機から SSH/SCP して構成する。P7 で
確立した方法であり、実機の glibc 2.38 が Debian bullseye のクロス
ツールチェインの 2.31 より新しいという事情から来ている（詳細は
「P7準備メモ」参照）。

つまり CI 環境だけでは `.muxapp` を作れない。sysroot をリポジトリや
Release アセットに置けば CI でも作れるようになるが、リポジトリを public に
するかどうかが未定の現状では、実機ファームウェア由来のバイナリを置く判断を
先送りしたい。そこで **CD は「タグを打ったら CI が成果物を作る」形ではなく、
「開発機で `scripts/release.sh` が作ってアップロードし、Actions は整合性の
裏取りだけをする」形**にした。

この判断は後から変えられる。sysroot を配れるようになったら
`ci.yml` にクロスビルドのジョブを足すだけでよく、`package.sh` /
`release.sh` の側は触らずに済む。

### branch protection が使えなかった

PR + CI 必須の運用にしたかったが、

```
$ gh api repos/ka-zuu/gbs-player/rulesets
Upgrade to GitHub Pro or make this repository public to enable this feature. (HTTP 403)
$ gh api repos/ka-zuu/gbs-player/branches/master/protection
Upgrade to GitHub Pro or make this repository public to enable this feature. (HTTP 403)
```

無料プラン + private リポジトリではサーバ側の強制ができない。
そこでリポジトリに追跡される `.githooks/pre-push` で代用した
（`git config core.hooksPath .githooks` をクローンごとに1回）。

これは自衛であって強制ではない（`--no-verify` や
`MUGBS_ALLOW_PUSH_MASTER=1` で抜けられる）。本当の強制が要るように
なったら (a) public 化（ruleset が無料で使える）(b) GitHub Pro、の2択で
あることをフック自身のコメントと README に書いてある。
`refs/tags/*` は素通しする（`release.sh` がタグを push するため）。

### shellcheck を初めて実際に走らせた

`tests/test_package.sh` は P7 の時点から `shellcheck -s sh -S error` を
呼んでいたが、**開発機に shellcheck が入っておらず一度も実行された
ことがなかった**（`command -v shellcheck` が偽なら黙って飛ばす作りだった）。

実際に走らせてみると、皮肉にも唯一の指摘対象が呼び出し元のそのファイル
自身だった。`# shellcheck があれば追加で見る` という日本語コメントが
shellcheck ディレクティブとしてパースされ、SC1073/SC1072（severity=error）に
なっていた。`# ` の直後が `shellcheck` で始まる行はコメントではなく
ディレクティブとして扱われるため。語順を変えて回避した。

併せて次の3点を直した。

1. 検査対象を「一部3ファイル」からリポジトリ内の全シェルスクリプトへ拡張。
   `tests/test_package.sh` 自身は `sh -n` すらされていなかった。対象は
   `SHELL_SCRIPTS` 変数に一元化し、SPEC 12 に「追加したらここに足す」と明記
2. 重大度を `error` から `warning` へ引き上げ。`-s sh` の SC3xxx
   （bashism 検出）は severity=warning なので `-S error` では拾えない。
   実機の busybox ash で動く `mux_launch.sh` にこそ効かせたい検査。
   この状態で指摘は 0 件（残る SC1091 は info で、実機にしか無い
   `/opt/muos/script/var/func.sh` を追えないという話なので拾わなくてよい）
3. `MUGBS_REQUIRE_SHELLCHECK=1` のときだけ「shellcheck が無いこと」自体を
   失敗にする。CI で apt を書き忘れても検査が無言で消えないようにするため。
   CI と `release.sh` がこれを立てる

### UBSan は既定では偽の緑を出す / path_in のリーク

P5 以降 ASan/UBSan ビルドを検証に使ってきたが、2つ問題があった。

**1つ目**: `test_playlist` / `test_archive` / `test_browser` の3つは
LeakSanitizer で必ず失敗していた（PLAN にも既知として書いてあった）。
原因はテストハーネス自身の `path_in()` が `strdup` を返しっぱなしにして
いたこと。`ASAN_OPTIONS=detect_leaks=0` で回避すると本体側のリークも
同時に見逃すことになるので、ハーネスを直す方を選んだ。3箇所に重複していた
`g_tmpdir` / `setup_tmpdir()` / `path_in()` を `tests/test_util.h` へ集約し、
確保した文字列を覚えておいて `atexit` で一括解放する。atexit ハンドラは
LIFO で、LeakSanitizer の検査は main より前に登録されるので、こちらの解放が
必ず先に走る。

`test_util.h` の共有シンボルは `static inline` 関数から参照させている。
素の `static` にすると、`path_in()` を使わない `test_m3u` などで
`-Wunused-function` / `-Wunused-variable` が出るため。

**2つ目**: UBSan は既定で `-fsanitize-recover` が効いており、**診断を
標準エラーへ出しても終了コードは 0 のまま**になる。つまり「ASan/UBSan で
緑」という従来の記録は、未定義動作を見逃していた可能性があった。
`-fno-sanitize-recover=all` と `UBSAN_OPTIONS=halt_on_error=1` を付けて
測り直したところ、**同梱 libgme を含めて新たな指摘は出ず 15件すべて緑**
だった（`test_package` はシェルと zip の構造検査なので除外）。
LeakSanitizer が実際に機能していることは、意図的にリークするだけの
小さなプログラムを同じフラグでビルドして別途確認した。

### CHANGELOG.md を新設した理由

この PLAN.md は 100KB を超える設計ログで、「なぜそう作ったか」を残す文書。
ユーザーが「このバージョンで何が変わったか」を読む文書ではないし、
GitHub Release の本文を書く元も無かった。

見出しを `## vX.Y.Z - YYYY-MM-DD` の1行固定にして、これを
`scripts/release.sh --print-notes` と `release-guard.yml` の契約にした。
`CMakeLists.txt` の `project()` を1行で書く制約（`package.sh` が sed で
読むため。`tests/test_package.sh` が一致を検証している）と同じ思想で、
「機械が読む書式を人間が壊したら機械が気づく」形にしてある。

### release.sh の順序設計

- **取り返しのつかない操作（タグ作成・push・リリース作成）を最後にまとめる。**
  前段の検査・ビルド・パッケージングで落ちても、リモートには何も残らない
- **リリースは既定で下書き。** Release Guard ワークフローが緑になったのを
  確認してから `gh release edit --draft=false` で公開する。成果物が
  開発機由来である以上、「クリーンなチェックアウトでテストが緑か」を
  独立に確認できるのはこのワークフローだけなので、そこを通す
- **`--dry-run` でもクロスビルドと `.muxapp` 生成は実際に行う。** Docker と
  sysroot が絡む一番不確実な部分こそリハーサルしたいので、ここは飛ばさない
- ホストビルドは `build/` ではなく専用の `build-release/` を毎回作り直す。
  古いオブジェクトやコミットし忘れを掴まないようにするのが目的なので、
  使い回しては意味がない
- `--print-notes` は `package.sh --print-version` と同じ「早期 exit する
  問い合わせオプション」。CHANGELOG 切り出しの実装を1箇所に保ち、
  `release-guard.yml` もこれを呼ぶ

**`set -e` の落とし穴**: `git rev-parse -q --verify ... && die "..."` と
書くと、rev-parse が失敗した時点で複合コマンドの終了ステータスが非0になり
`set -e` がスクリプトごと殺す（タグが存在しないという正常系で死ぬ）。
タグの重複確認は `if` で書いてある。既存スクリプトが多用している
`cmd || die` の形は安全。

### P13の実機検証

コードの変更は `tests/` 配下（テストハーネスの一時パス生成）だけで、
`src/` にも `packaging/` にも手を入れていないため、実機固有の検証は不要。
ただし `scripts/release.sh` が生成した `.muxapp` を実機へ入れて起動できる
ことをもって最終確認とする（リリース手順そのものの検証を兼ねる）。

**完了。** `./scripts/release.sh` でタグ `v1.0.0` と GitHub Release
（下書き）を作成し、Release Guard ワークフロー（タグ・
`CMakeLists.txt`・`CHANGELOG.md` の整合性 + クリーンなチェックアウトでの
フル CI）が緑になったことを確認してから `gh release edit v1.0.0
--draft=false` で公開した。`gh release download` で取得した
`muGBS-1.0.0.muxapp`（SHA256:
`b9f2d551ce15005535371229b5504dd991380a339b410d5a55c78fc9ffbc9021`）を
実機へ転送し、Archive Manager から展開・起動して再生できることを
ユーザーが確認済み。
https://github.com/ka-zuu/gbs-player/releases/tag/v1.0.0

## Issue #3: Player画面の文字サイズ

「けっこう文字が小さいところも多い。640:480前提にして、全体のバランスを
整える」という Issue。着手前に 640x480（実機と同じ `scale = 1.0`）で
`--screenshot` を4画面分撮って現状を確認したところ、原因ははっきりしていた。

`UI_TEXT_SMALL` は `8 * 1 * scale` = **8px**、つまり内蔵の 8x8 ビットマップ
フォントの等倍である。そして Player 画面は曲名（TITLE = 24px）以外の
ほぼ全部の情報が、この 8px に載っていた——再生位置・トラック番号・
作者/著作権・`PLAYING repeat:… shuffle:…`・フッタ。他の画面
（Browser / TrackList / Settings）はリスト行が BODY = 16px なので、
実際に「異様に小さい」のは Player の情報ブロックだけだった。

ユーザーに確認して対象を絞った:

- 直すのは Player の Track・再生時間まわり。**フッタは小さいままでよい**
- Browser / TrackList / Settings のリスト行（16px）は現状維持
- ドットが崩れないよう **8 の整数倍**を保つ
- Player で一覧と波形が競合するなら一覧を優先

### サイズ段階の割り当てを変えるだけで済ませた

`ui.h` のサイズ段階は既に 8 / 16 / 24px（1x / 2x / 3x）で、**どれも 8 の
整数倍**になっている。つまり「整数倍を保ったまま大きくする」という要求は、
`ui.c` にも `ui_metrics_compute()` にも手を入れず、`draw_player()` が
どの段階を使うかを差し替えるだけで満たせる。中間サイズ（12px や 20px）を
足す案もあったが、8x8 を 1.5 倍・2.5 倍すると行によって線が 1px と 2px に
分かれて太さがムラになるため採らなかった。`SDL_HINT_RENDER_SCALE_QUALITY`
を `"0"`（最近傍）にしているのと同じ理由である。

結果、変更は `src/app.c` の `draw_player()` だけに収まった。
`tests/test_ui_metrics.c` の期待値（`m.glyph == 16` など）も、
他画面のレイアウトも一切影響を受けない。

| 要素 | 変更前 | 変更後 | 640x480 |
|---|---|---|---|
| 曲名 | TITLE | 変更なし | 24px |
| ゲーム名 | BODY | 変更なし | 16px |
| author / copyright | SMALL | **BODY** | 8 → 16px |
| `Track n/m` | SMALL | **BODY** | 8 → 16px |
| `0:00 / 2:38` | SMALL | **TITLE** | 8 → 24px |
| プログレスバー高 | `pad*2` | **`pad*3`** | 8 → 12px |
| `PLAYING repeat:… shuffle:…` | SMALL | **BODY** | 8 → 16px |
| 一時ステータスメッセージ | SMALL | **BODY** | 8 → 16px |
| フッタ | SMALL | **変更なし** | 8px |

**再生位置だけ TITLE（3x）に上げて曲名と同格にした。** 一番よく目をやる
情報で、指摘の中心がここだったため。`Track n/m` は曲名の索引にすぎないので
BODY（2x）に留め、「時間 > トラック番号」の階層を作っている。
これで Player 画面は TITLE（曲名・再生位置）/ BODY（それ以外）/
SMALL（フッタだけ）の3段階に整理された。

### 一覧の行数も波形の高さも変わらない

情報ブロックは文字が大きくなったぶん縦に伸びるが、**行数自体は増えて
いない**（`Track n/m` と時間はもともと別々の行で、その後も別々のまま）。
640x480 での y の推移:

```
y = pad                                     4
曲名   TITLE   y += glyph(TITLE) + pad  ->  32
ゲーム BODY    y += line_h              ->  52
meta   BODY    y += line_h + pad        ->  76
Track  BODY    y += line_h              ->  96
時間   TITLE   y += glyph(TITLE) + pad  -> 124
バー   pad*3   y += bar.h + pad*2       -> 144
状態   BODY    y += line_h              -> 164
band_h = (480 - footer_h 28) - 164 - pad = 284  ->  14行
  一覧 = min(14 - PLAYER_WAVE_MIN_ROWS, PLAYER_LIST_MAX_ROWS) = 7行(140px)
  波形 = 14 - 7 = 7行(140px)
```

一覧 7 行・波形 140px は変更前と同じ。したがって
`PLAYER_LIST_MAX_ROWS` / `PLAYER_WAVE_MIN_ROWS` は触っていないし、
P10 の「波形をフッタ近くまで大きく」という実機フィードバックへの対応も
そのまま維持されている。ユーザー回答の「一覧を優先」は、低解像度で
足りなくなったときに一覧へ先に配る既存の分配ロジックがそのまま満たす。

### ついでに直した2点

- **時間表示が `ui_text()` だった**。他の行は `ui_text_clipped()` なのに
  ここだけクリップしておらず、長尺の m3u（分が3桁）で画面外へ出うる。
  `trackno` / `status_line` も含めて `ui_text_clipped()` に揃えた
- **フッタの縦センタリングが `ui->metrics.glyph`（= BODY 相当）基準だった**。
  実際に描いているのは SMALL の文字なので、640x480 で 4px 下にずれていた。
  `ui_glyph_size(ui, UI_TEXT_SMALL)` に直した

### 検証

ホストビルドの CTest 16件（`MUGBS_REQUIRE_SHELLCHECK=1`、SKIP なし）と
ASan/UBSan ビルドの 15件がいずれも緑。CTest と同じ6解像度
（320x240 / 480x320 / 640x480 / 720x720 / 1024x768 / 1280x720）で
Player 画面を `--ui-script` + `--screenshot` で撮り、はみ出し・重なりが
無いこと、一覧が7行あること、波形がフッタ直前まで出ていることを目視確認した。
一時ステータスメッセージのオーバーレイは、壊した `.gbs` を置いた
ディレクトリを開いて `Failed to open:` を出させ、320x240 でも行の中に
収まっていることを確認した。

**実機検証は必要**（レイアウトの実寸に関わる変更）。

### 実機検証（完了）

実機（RG35XX PRO相当、muOS 2601.0 JACARANDA、192.168.0.20）へ
`./scripts/build-aarch64.sh` → `./scripts/package.sh` で作った
`.muxapp` を `/opt/muos/script/mux/extract.sh`（Archive Managerが内部で
呼ぶのと同じスクリプト、P7で確立した手順）経由でインストールした。
インストール時に `config.ini` がパッケージ同梱の初期値へ戻る
（既知の挙動）ため、`last_path` だけ以前の値へ手動で戻した。

ユーザーが実機の物理ボタンで muGBS を起動し `.gbs` を開いて Player 画面
まで進め、Claude 側は SSH 越しに `/dev/fb0` をダンプしてスクリーンショット
化した（P5〜P7と同じ役割分担）。`foreground_process` が `mugbs` であること
（`muxfrontend` のオーバーレイが無いこと）、ログにクラッシュが無いことも
確認した。

640x480 実機での見た目:

- `0:30 / 2:38`（再生位置）が曲名と同格の大きさになり、単独ではっきり
  読める。修正前は8pxで「異様に小さい」の中心だった箇所
- `Track 1/18` / `PLAYING repeat:all shuffle:off` もリスト行と同じ
  大きさになり読みやすい
- ファイル一覧は想定通り7行表示、その下に波形ビジュアライザが出ている
  （一覧・波形とも行数/高さは変更前と同じという設計どおりの結果）
- フッタ（`Start+Select:Quit` + パス）は意図通り小さいまま
- ゲーム名の日本語部分が `?` に文字化けするのは basic-latin 以外を
  `?` にフォールバックする既知の仕様（`src/ui.c` のコメント参照）で、
  今回の変更とは無関係

実機でもレイアウトの破綻・文字の重なり・はみ出しは無く、Issue #3 で
指摘された「異様に小さい」状態が解消されたことを確認した。

## 検証手順

```sh
# 前提（初回のみ）
sudo apt update && sudo apt install -y pkg-config libsdl2-dev
# ctest を CI と同じ完全な形で回すなら追加で:
sudo apt install -y zip unzip shellcheck

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

# GUI (P8): EQ・波形
./build/mugbs --config /tmp/test.ini Game.gbs
#   Player で波形が動く
#   Return(START) で Settings -> EQ bass / EQ treble を LEFT/RIGHT で振る
grep -E 'eq_' /tmp/test.ini           # 保存されていることを確認

# レイアウトを目で見る(--screenshot は非公開の開発用オプション)。
# ホストには実機の /dev/fb0 に当たるものが無いのでこれで代用する。
# SDL_VIDEODRIVER=offscreen なので X/Wayland が無くても動く。
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 2 \
  ./build/mugbs --window 320x240 Game.gbs --config /tmp/test.ini \
                --screenshot /tmp/shot.bmp
# --ui-script と組み合わせれば任意の画面まで進めてから撮れる

# ASan/UBSan (P5から使っている検証。CIのsanitizersジョブと同じ内容)
cmake -B build-asan -DTARGET_HOST=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan --output-on-failure -E '^test_package$'
#   P13以前は test_playlist/test_archive/test_browser の3つが、テスト
#   ハーネス自身の意図的なリーク(path_in の strdup)で失敗していた。
#   P13 C3 で path_in を tests/test_util.h へ集約しatexitで解放するよう
#   直したので、LeakSanitizerを有効にしたまま15件すべて緑になる。
#   -fno-sanitize-recover=all も必須(無いとUBSanは診断を出すだけで
#   終了コードが0のままになり、CTestが未定義動作を見逃す)。
#   test_package はシェルと zip の構造検査なのでサニタイザとは無関係
```

### CI とリリース（P13）

```sh
# フックを有効にする(クローンごとに1回)
git config core.hooksPath .githooks
git push --dry-run origin HEAD:master   # 拒否されること

# CIと同じ条件でローカルにテストを回す(SKIPが出ないこと)
MUGBS_REQUIRE_SHELLCHECK=1 ctest --test-dir build --output-on-failure

# リリースノートの切り出しを確認
./scripts/release.sh --print-notes

# リリースのリハーサル(検査とクロスビルドは実行され、変更操作だけ飛ぶ)
./scripts/release.sh --dry-run

# 本番
./scripts/release.sh                                 # タグ + 下書きリリース
gh run list --workflow=release-guard.yml --limit 1   # 緑を確認
gh release edit v1.0.0 --draft=false
gh release download v1.0.0 -D /tmp/rel
sha256sum /tmp/rel/muGBS-1.0.0.muxapp                # 本文のSHA256と一致すること
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
#   -> ./muGBS-1.0.0.muxapp

# 4. 実機へ転送してインストール (正式ルート: Archive Manager)
scp muGBS-1.0.0.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
# 実機で Applications > Archive Manager > muGBS-1.0.0 を選んで展開する
# (SSH越しに /opt/muos/script/mux/extract.sh /mnt/mmc/ARCHIVE/muGBS-1.0.0.muxapp
#  を直接叩いても同じ結果になる。Archive Managerが内部で呼ぶのと同一スクリプト)

# 5. アプリ一覧(Applications)から物理ボタンで起動する
#    ログは実機の /run/muos/storage/application/muGBS/log.txt (=SD上の
#    MUOS/application/muGBS/log.txt) に出る
```

P6以前にあった「SSH直接起動でXDG_RUNTIME_DIR等を手動exportする」手順は、
`mux_launch.sh`経由の正式起動であれば`func.sh`が自動で行うため不要になった。
CLIハーネス(`--cli`)での単発確認だけしたい場合は、以前どおり手動exportが必要
(`mux_launch.sh`を経由しないため)。

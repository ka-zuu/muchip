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

## Issue #8: 画面の文字バランス

Issue #3（上記）の実機検証後に届いたフィードバック3点:

1. 再生位置の表示はもう少し小さくていい
2. 再生位置とシークバーは同じ行に収めてもいいかも
3. 最上部のタイトルは見切れる可能性が高い。文字がスライドしてもいい
   （設定で変えられるように）

1・2は Issue #3 の揺り戻しで、3は新規要求。

### (a) 「もう少し小さく」はBODYへ一段落とすだけ

`ui.h` のサイズ段階は SMALL(8px)/BODY(16px)/TITLE(24px) の3段しか無い
（Issue #3 のとおり、中間段を足すと 8x8 の最近傍拡大でドットの太さが
ムラになるため意図的に増やしていない）。したがって「もう少し小さく」は
選択の余地なく **TITLE → BODY** の一段。曲名との「同格」をやめてトラック
番号などと同じ扱いに戻す形になる。

### (b) 1行化で浮いた縦幅は波形へ回る

再生位置をBODYに落として時間とバーを同じ行に収めると、縦方向に
`glyph(TITLE) + pad`（640x480で28px）ぶん浮く。`draw_player()` の一覧/波形
バンド分割（P9〜P10で作った、残りの高さから `avail_rows` を割り出して
一覧を優先配分し、余りを波形へ回すロジック）は `y` の実値から動的に
高さを計算しているため、ここは一切触っていない。浮いた分はそのまま
波形が2〜3行分厚くなる形で吸収される。

時間+バーの1行化そのものは、時間の幅 `ui_text_width()` を測ってバーの
開始xと残り幅を決めるだけ。バーの残り幅が極端に狭い解像度
（`pad*4` 未満）では2行構成にフォールバックする分岐を入れてあるが、
`test_ui_metrics.c` の不変条件（`glyph>=8`, `pad>=2` 等)の範囲では
128x64 のような極端な解像度でも1行に収まることを `--screenshot` で
確認済み。

### (c) 横スクロールは `ui.c` のプリミティブとして追加した

曲名の見切れは Browser のパス表示や TrackList のヘッダにも起きうる
一般的な問題だが、今回のIssueはPlayerの曲名限定の要求だったため、
`ui_text_scroll()` / `ui_marquee_offset()` を `ui_text_clipped()` の隣に
汎用プリミティブとして追加し、`draw_player()` からだけ呼ぶ形にした
（他画面へ広げるかは別Issue判断とする）。

`ui_marquee_offset()` は `ui_glyph_size_for()` と同じく SDL_Init 不要な
純関数にした。速度・停止時間・折返し間隔をすべて `glyph_px` の倍数で
決めているのは、他のレイアウト量と同じく解像度非依存にするため
（SPEC 6.2）。具体的には: 先頭で1秒静止 → 毎秒4文字分の速度で流す →
文字列の末尾から4文字分の間隔をあけて先頭へ戻る、の周回。

`ui_text_scroll()` の描画は `SDL_RenderSetClipRect()` で対象行の矩形へ
クリップし、同じ文字列を2回（現在位置と1周期先）描くだけ。`ui.c` は
他にクリップ矩形を使っている箇所が無いため、呼び出し前後の状態を
`SDL_RenderIsClipEnabled()`/`SDL_RenderGetClipRect()` で退避・復元する
実装にした（将来 `ui.c` の別の描画がクリップを使うようになっても
壊れないように）。

### (d) 既定をonにした理由

Issue本文の「文字がスライドしてもいいかも」を素直に採用し、
`title_scroll` の既定値は **on**（`config_set_defaults()`）。
offにすると Issue #3 以前と同じ `ui_text_clipped()` の `"..."` 省略に
戻る。Settings画面にも `Scroll title` として出し、他の bool 設定
（`Show all files` 等）と同じ表駆動（`SETTINGS[]`）に乗せてあるので
リセット・保存の経路は既存のものをそのまま使う。

### 検証

ホストビルドの CTest（`ui_marquee_offset()` の純関数テストを
`test_ui_metrics.c` に追加、`title_scroll` の既定値・ラウンドトリップを
`test_config.c` に追加、`ui_smoke.script` に `Scroll title` を1回踏む
操作を追加）。長い曲名を持つ `.m3u` を合成して `--ui-script` +
`--screenshot` で 128x64 / 320x240 / 640x480 / 720x720 / 1280x720 を
確認し、時間+バーが1行に収まること、`title_scroll=on` でクリップされた
曲名が途切れず表示されること（静止画のため動き自体は未確認）、
`title_scroll=off` で従来どおり `"..."` 省略に戻ることを確認した。

### 実機検証（完了）

実機（RG35XX PRO相当、muOS 2601.0 JACARANDA、192.168.0.20）へ
`./scripts/build-aarch64.sh` → `./scripts/package.sh` で作った
`.muxapp` を `/opt/muos/script/mux/extract.sh`（Archive Managerが内部で
呼ぶのと同じスクリプト、P7で確立した手順）経由でインストールした。
インストール直後の `config.ini` に `title_scroll = true`（既定値）が
含まれていることを確認済み。

ユーザーが実機の物理ボタンで `Apps > muGBS プレーヤー` を起動し、
`/mnt/sdcard/ROMS/VGM/GBS/Parodius (EMU).zophar.zip`（P12で使った実データ。
28個の`.m3u`が連結され、どの曲名も `"<曲名> - Akiko Ito, Shigeru
Fukutake, Hidehiro Funauchi - Parodius - ©1991-04-05 Konami"` という
90文字超の長さで実機640x480(TITLE=24px)では確実に画面幅を超える）を開いて
Player画面まで進めた。Claude側はSSH越しに`/dev/fb0`を1秒間隔で連続ダンプし
（`dd if=/dev/fb0` → 32bit `BGRA` として `numpy`/`PIL`でPNG化。実機の
フレームバッファは`fbset -i`で確認した `rgba 8/16,8/8,8/0,8/24`、
640x480、`stride=2560`）、`foreground_process`の値とログを確認した。

**確認できたこと:**

- **1・2（再生位置の1行化）**: `0:12 / 0:42` のような時間表示とシークバーが
  同じ行に収まり、時間が左・バーが残り幅いっぱいに描かれることを実機の
  スクリーンショットで確認した。曲送り（`repeat:all`によるトラック終端の
  自動遷移）でも崩れなかった
- **3（曲名の横スクロール）**: 1秒間隔の連続キャプチャで、長い曲名
  （例:`"Theme of Vic Viper - Akiko Ito, Shigeru Fukutake, Hidehiro
  Funauchi - Parodius - ©1991-04-05 Konami"`）が実際に左へ滑らかに流れ、
  数秒〜十数秒で一周して読める速度であることを目視確認した。文字の
  重なり・ちらつき・クリップ境界からのはみ出しは無かった。ファイル一覧・
  波形ビジュアライザとも正常に描画され続けた
- **起動・終了経路**: `mux_launch.sh`経由の起動で`foreground_process`が
  `mugbs`になり(muxfrontendのオーバーレイなし)、Start+Selectでの終了後は
  `mugbs exited with 0`とログに残り、`foreground_process`が`muxfrontend`
  へ戻って`アプリケーション`一覧に`muGBS プレーヤー`が正常に表示される
  ことを確認した(P7で確立した正式な起動経路の回帰も無いことの確認を兼ねる)
- Settings画面の`Scroll title`をoffにした場合の見た目もユーザーが実機で
  確認し、問題ないことを確認済み

実機でもレイアウトの破綻・文字化け・クラッシュは無く、Issue #8で
指摘された3点がいずれも解消されたことを確認した。

## Issue #7: バッテリー残量の画面表示

Issue本文（要約）:

1. 画面右上（タイトル行）に表示するのが良さそう
2. 常時表示・バッテリー減少時のみ表示・非表示の設定ができる
3. 「減少」の判定は muOS 側の設定を活かせるならそれ、無ければ10%

着手前にユーザーへ確認した4点（見た目・既定値・Player画面への表示・
しきい値探索の要否）は本文末尾の実機検証チェックリストにも反映してある。

### (a) `battery.c` を独立させた理由

`app.c` は既に1300行超で、`app.h` 冒頭のコメントが「画面状態機械と
レイアウト」と自称している。バッテリー残量はプラットフォームの事実
であり、`browser.c`(dirent) や `player.c`(オーディオデバイス) と同じ層に
置くのが筋が良い。加えて、`ui_marquee_offset()`（Issue #8）や
`ui_metrics_compute()` と同じく「SDLをリンクするが `SDL_Init` は呼ばない
純関数」として判定ロジック（`battery_should_poll`/`battery_is_low`/
`battery_should_show`/`battery_low_threshold_from_env`）を切り出せば、
`tests/test_battery.c` で `SDL_GetPowerInfo()` の実行環境依存を避けつつ
境界条件（`Uint32`折り返し・しきい値境界・充電中の扱い）を検証できる。

`SDL_GetPowerInfo()` 自体は Linux バックエンドで `/sys/class/power_supply`
を毎回読み直すため、毎フレーム(60Hz)呼ぶとI/Oが無視できない。
`battery_poll()` が内部で `BATTERY_POLL_INTERVAL_MS`(2秒)ごとにしか
実際には読まないようにし、`app_update_battery()` という1フレーム1回だけの
呼び出し口を`app_update_scope()`(F-14)と同じ場所に置いて、`draw_*()`側は
`app->battery_status`/`app->battery_visible` を読むだけにした
(1フレーム内で4画面が必ず同じ値を見える、という副次効果もある)。

### (b) 充電中でも「低い」を隠さない

`battery_is_low()` は充電中かどうかを見ない(残量そのものだけで判定する)。
充電中は色を緑にして区別するので、「挿した瞬間に赤ゲージが消えて
安心してしまう」よりは「挿しても暫くは赤のまま(緑に変わる)」の方が
実態(まだ空に近い)を隠さない。

### (c) 見た目はゲージのみ(数値は出さない)

8x8埋め込みフォント(`vendor/font8x8`)はASCIIのみで絵文字が無い。
ユーザーに確認した結果、矩形の枠+塗りのゲージのみを描く方針にした
(数値`85%`を出す案もあったが、桁数で幅が動くとPlayerのマーキー
(Issue #8)の可用幅が毎秒揺れてガタつく懸念があった。ゲージのみなら
幅は常に固定でその心配が無い)。

### (d) 4画面への組み込みは「描画スイッチ後のオーバーレイ」にしなかった

`draw_browser()`のcwd表示も`draw_player()`の曲名マーキーも、タイトル行の
幅いっぱいを使って描かれる。スイッチ後に上から重ね描きすると本文と
衝突するため、`draw_battery()`が確保した幅を先に本文側の`max_w`から
差し引いてから本文を描く、という順序を4画面それぞれのタイトル行で
踏む形にした(共有ヘルパは`draw_battery(app, right_x, row_y, row_h)`
1つで、呼び出し側が返り値の幅を引き算する)。Playerだけは曲名の行にのみ
効かせ、以降の行(トラック番号・時間等)は`content_w`のまま(タイトル行より
下から始まるため)。

### (e) しきい値を config.ini のキーにしなかった理由

Issueが求めているのは表示条件(off/low/always)の設定だけで、しきい値
そのものまでconfig.iniに持たせると、SPECのサンプル・Settings画面・
`packaging/muGBS/config.ini`と同期させる箇所が増える。`mux_launch.sh`が
`MUGBS_BATTERY_LOW_PCT`を環境変数でexportする経路(`MUGBS_START_DIR`と
同じ idiom)なら、muOS由来の値もSSHでの実験用上書きも両方まかなえる。
3件目の類似ニーズが出たらconfig.ini化を再検討する。

### (f) `SET_ENUM` の型パニング修正

`repeat_mode_t`に続く2つ目の`SET_ENUM`(`battery_show_t`)を追加するに
あたり、`setting_get`/`setting_set`が`repeat_mode_t *`決め打ちで
読み書きしていたのを`int *`へ直した。両方の enum が `int` と同じ幅・
表現であることを`_Static_assert`で明示し、前提が崩れたらビルドで
気づけるようにしてある。

### (g) muOSのしきい値探索は推測であることを明示する

`mux_launch.sh`が試す `GET_VAR` の候補キー
(`device:battery/low`/`global:settings/general/low_battery`/
`global:settings/power/low_battery`)は、公開ドキュメントから確証を得た
ものではなく推測。全候補の結果を`echo`で`log.txt`に残す設計にしたのは、
初回の実機起動でどれが(あるいはどれも)生きているかを確認し、
候補リストを絞り込む・別Issueに切り出すための材料にするため。
見つからなければ何もexportされず、`battery_low_threshold_from_env(NULL)`
が既定の10%を返す(Issue本文の要求どおりの安全側フォールバック)。

### 検証

ホストビルドの CTest: 新設 `tests/test_battery.c`(`battery_should_poll`の
`Uint32`折り返し・`battery_is_low`/`battery_should_show`の境界値行列・
`battery_low_threshold_from_env`のクランプ・`MUGBS_BATTERY_FAKE`パーサ)、
`tests/test_config.c`の5箇所(`test_defaults`/`test_spec_sample`/
`test_enum_and_bool_forms`/`check_equal`/`test_roundtrip_mutated`)、
`tests/ui_smoke.script`に`Show battery`を1周ぶん踏む操作を追加、
`tests/CMakeLists.txt`の`test_ui_smoke_*`に`MUGBS_BATTERY_FAKE=7`を追加して
CIでもゲージの描画経路(縮退分岐含む)をASan/UBSan下で実行させるようにした。
`ctest --test-dir build`・ASan/UBSanビルドとも全緑(SKIP無し)を確認した。

レイアウト目視確認: 長い曲名(`.m3u`で明示的に長いタイトルを合成)と長い
ディレクトリ名を用意し、`MUGBS_BATTERY_FAKE`で通常(85%)/低下(5%)/充電中
(`+50`)を切り替えつつ、320x240/640x480/1280x720で`--ui-script`+
`--screenshot`を確認。4画面ともゲージが右上に収まり本文と衝突しないこと、
色分岐(グレー/赤/緑)が正しいこと、Playerでは曲名の可用幅がゲージ分だけ
縮んで先が"..."でも切れずに収まることを確認した。

### 実機検証（完了）

実機（RG35XX PRO相当、muOS 2601.0 JACARANDA、192.168.0.20）へSSH(鍵認証)
で接続して確認した。`./scripts/build-aarch64.sh`でクロスビルドした
`mugbs`をscpで転送し、`mux_launch.sh`と同じ環境構築（`func.sh`読み込み→
`SETUP_APP`→`SDL_GAMECONTROLLERCONFIG_FILE`保証）を再現したうえで
`--ui-script`/`--screenshot`/`MUGBS_BATTERY_FAKE`を使い、複数の画面・
残量・状態を自動操作で確認した（P6実機確認と同じ「SSH直接起動」方式。
`foreground_process`をmuxfrontendへ戻す後始末も実施済み）。

**しきい値キーの実在確認（上記(g)の懸念点）**: `mux_launch.sh`が試す3候補
`GET_VAR`のうち、**`global settings/power/low_battery` が実在し `15` を
返した**（他の2候補は空文字）。推測で用意した候補リストが実機で当たって
いたことを確認できた。`mux_launch.sh`の探索処理をそのまま実機で実行し、
`MUGBS_BATTERY_LOW_PCT=15`が正しくexportされることも確認済み。

**`SDL_GetPowerInfo()`の実機動作確認（最大のリスクだった項目）**:
実機の`/sys/class/power_supply/axp2202-battery/`（`type=Battery`,
`status=Charging`, `capacity=64`, `present=1`）を直接読んだうえで、
`mugbs`のログに

```
[INFO] battery: present=1 percent=66 charging=1
```

が実際に出ることを確認した（複数回実行するとcapacityの上昇に追随して
`percent=67`等に変化することも確認、充電中のためcapacityが実際に
増加していた）。危惧していた「実機のSDL2ビルドでは`SDL_GetPowerInfo()`が
`UNKNOWN`を返す」というR2のリスクは杞憂だった。

**4画面での見た目**: Browser/Player/TrackList/Settingsそれぞれで
`--screenshot`を取得し、実機の実解像度(640x480, malifbドライバ、
ソフトウェアレンダラではなく実機の描画パス)でゲージが右上に正しく
収まり、長い曲名・パスと衝突しないことを確認した。Settings画面の
`Show battery`行が`always`と表示され、`X:Reset`の対象一覧にも
（表駆動のため自動的に）含まれていることも画面で確認できた。

**色分岐の3状態**: 実際の充電中バッテリー(67%, 緑)に加え、
`MUGBS_BATTERY_FAKE=90`（非充電、通常=グレー）・`MUGBS_BATTERY_FAKE=8`
（既定の`low`モードで自動表示、残量僅少=赤）を実機で切り替えて描画を
確認した。3色とも実機のフレームバッファ経由のスクリーンショットで
正しく出ることを確認済み。

**config.iniの永続化**: `battery_show = low`（既定）が実機で保存される
ことと、`always`を指定した`config.ini`を渡した場合はそれを読み込んで
起動することを確認した（実機の`config_save`/`config_load`経路そのもの）。

**未確認のまま残った項目**（自動操作の範囲外、または今回は必要性が低いと
判断したもの）:

- 充電ケーブルの物理的な抜き差しによる色のリアルタイム切り替わり
  （SSH越しの自動操作では抜き差し自体ができない。ロジック上は
  `battery_poll()`が2秒ごとに読み直すので反映されるはずだが、目視の
  実地確認はしていない）
- 物理ボタンでの`Show battery`のLEFT/RIGHT操作そのもの（下記
  「v1.1.0リリース時の実機インストール確認」で`mux_launch.sh`経由の
  フルライフサイクル自体は確認したが、ボタン操作はSSH越しでは注入できない
  ため未確認。`SETTINGS[]`の他項目と共通の`adjust_setting()`経路であり、
  Issue #8までに実機で繰り返し確認済みの経路のため、新規リスクは
  小さいと判断）

### v1.1.0リリース時の実機インストール確認（完了）

PR #10マージ後、v1.1.0としてリリースする際に実機への正式インストールを
確認した。`./scripts/release.sh`でクロスビルド・`.muxapp`生成・タグ
push・下書きReleaseまで行った後、`scp muGBS-1.1.0.muxapp
root@192.168.0.20:/mnt/mmc/ARCHIVE/`で転送し、`/opt/muos/script/mux/extract.sh`
（Archive Managerが内部で呼ぶのと同一スクリプト）で展開した。

**確認できたこと:**

- 展開後の`bin/mugbs`が`1.1.0`を報告し、`mux_launch.sh`が更新版
  （しきい値探索ブロック込み、8715バイト）に置き換わっていることを確認
- **`mux_launch.sh`を`muxfrontend`と同じ形（`$1`にAPP_DIRを渡す）で
  直接起動し、本番の起動経路そのものを実行した**。ログに以下が実際に
  出ることを確認した:
  ```
  battery: probe GET_VAR device battery/low -> ''
  battery: probe GET_VAR global settings/general/low_battery -> ''
  battery: probe GET_VAR global settings/power/low_battery -> '15'
  battery: low threshold 15% (muOS)
  ...
  [INFO] battery: present=1 percent=88 charging=1
  ```
  `SETUP_APP`経由で`foreground_process`が`mugbs`へ正しく切り替わることも
  確認した（起動前は`muxfrontend`、起動後は`mugbs`）。
- `/dev/fb0`を直接ダンプしてBrowser画面のスクリーンショットを取得した。
  `battery_show`既定値`low`・残量88%（>しきい値15%）のため、想定どおり
  ゲージは非表示だった（8x8フォントの描画そのものは正常）
- 終了は`kill -TERM $(pidof mugbs)`（muOSの`script/mux/quit.sh`と同じ
  シグナル）で行い、`config.ini`が正常に保存されたうえで`exit 0`で
  終了することを確認した。その後`foreground_process`を`muxfrontend`へ
  手動で戻し（本来は`quit.sh`が行う後処理）、`/tmp`の一時ファイルを
  削除して実機をテスト前の状態に戻した
- **既知の注意点（本Issueとは無関係の既存事象）**: `/dev/fb0`の生ダンプには
  画面左上に小さな緑色のバッジ（`+9`という文字を含む）が写り込む。
  `mugbs`の`--screenshot`機能（SDLレンダラから直接読む）で撮った同じ
  画面には現れないため、`mugbs`自身の描画ではなく、フレームバッファに
  後から重ねて書き込まれる何らかのmuOS側のシステムオーバーレイ
  （CPUガバナ表示等と推測）だと分かる。今回の変更に起因するものではない

これにより、Issue #7節の「未実施」だった`mux_launch.sh`経由の起動確認
（物理ボタン操作を除く）と、リリース物である`.muxapp`のインストール
自体の両方を実機で確認できた。

## Issue #2: NSF再生対応

Issue本文（要約）: 「NSFの再生に対応」。

### (a) 調査で分かったこと: 実質「大きな機能追加」ではなかった

着手前に「バイナリサイズ・CPU負荷・実装コストへのインパクトを知りたい」
という要望があったため、実装より先に調査した。結論は3点:

1. **NSF/NSFEのデコーダは1.0.0の頃から出荷バイナリにリンク済みだった。**
   `CMakeLists.txt`は`USE_GME_GBS=ON`しか明示していなかったが、libgme
   (`vendor/game-music-emu/gme/CMakeLists.txt`)側の`USE_GME_NSF`/
   `USE_GME_NSFE`は既定でONであり、明示的にOFFにしていなかったので
   暗黙にビルドに含まれていた。実際 `nm build-release/mugbs` に
   `gme_nsf_type_`・`Nsf_Emu::*`のシンボルが、`build-aarch64/mugbs`にも
   `strings`で`Nintendo NES`/`NSFE`の文字列が見つかった。つまり
   **今回の変更によるバイナリサイズの増分は無い**（CHANGELOG参照）。
2. **`playlist.c`/`m3u.c`/`player.c`/`archive.c`は元から形式非依存**
   だった。`playlist_open()`は`.zip`/`.m3u`以外の全てを単体ファイル
   （＋任意の同名サイドカーm3u）として扱う経路に流すだけで、
   `gme_open_file()`が拡張子から実際の形式を判別する。`archive.c`の
   `k_music_exts[]`には元から`.nsf`/`.nsfe`が入っていた
   （P4時点で「GBSが主目的だが対応拡張子は広めに持つ」方針で足して
   あったが、Browser側のフィルタだけが追従していなかった）。
3. **P12のGBS用パッチ(`vendor/game-music-emu/gme/Gbs_Emu.cpp`の`flags_
   |= 0x02`)はNSFには適用しない・してはいけない。** P12はzophar.net配布
   GBSパックの10進m3uトラック番号が0始まりだった実例に合わせた特例で、
   GBSヘッダの`first_track`はlibgme内で参照されない。一方NSFはヘッダ
   自体が`first_song`（1始まり）を持ち、実在のNSF用m3uも1始まりで
   書かれる。これはupstreamのデフォルト動作（10進を1始まりとみなし
   -1する）とそのまま一致するため、`gme_nsf_type_`
   (`vendor/game-music-emu/gme/Nsf_Emu.cpp`)には手を入れていない。
   合成NSFヘッダを使った実験(下記(c))で、GBS用パッチの効果
   （`flags_ & 0x02`）が`gme_gbs_type_`側にしか刺さっておらず
   `gme_nsf_type_`は素のupstream動作のままであることを確認した。
   SPEC.md 5.2にこの非対称性を明記した。

したがって実装は「Browserの拡張子フィルタ追加」「`playlist.c`の関数名
整理（`playlist_open_gbs()`→`playlist_open_music_file()`。振る舞いは
不変）」「テスト・ドキュメントの追従」に限定された。対応範囲は
`.nsf`/`.nsfe`のみとし、`.spc`/`.vgm`等は従来どおり
「Show all filesでたまたま動く」扱いのまま据え置いた（ユーザーへの
確認結果）。

### (b) アプリ名は変更しない

「muGBSという名前がNSF等にはそぐわないのでは」という論点をユーザーに
確認したところ、`muGBS`のままで進める判断になった。理由: 実機の
インストール先`/run/muos/storage/application/muGBS/`と`config.ini`の
場所が変わると、既存ユーザーの再インストール・設定移行が必要になる
ため。改名は別Issueとして起票し、影響範囲（`packaging/muGBS/`の
ディレクトリ名・`glyph`/`grid`のPNG・`mux_launch.sh`の`GRID:`/`HELP:`
行・`scripts/package.sh`の`PKG_NAME`・SPEC.md 9章・実機の`config.ini`の
移行手順）を書いておくことにした。

### (c) 合成NSFフィクスチャの作り方（`tests/test_playlist.c`）

`build_synthetic_gbs()`に倣い、ヘッダのみ有効な最小NSFを組み立てる
`build_synthetic_nsf()`を追加した。NSFヘッダ構造
（`vendor/game-music-emu/gme/Nsf_Emu.h`の`header_t`、`header_size = 0x80`）
はGBSより長い(0x80 vs 0x70)。load/init/playアドレスは`rom_begin`
(`enum { rom_begin = 0x8000 }`)に置く必要があり、GBSの`0x400`とは異なる
（`load_addr < rom_begin`だと`Nsf_Emu::load_()`が
"Corrupt file (invalid load/init/play address)"で失敗する）。本体コードは
GBSの`RET`(`0xC9`)2バイトに相当する`RTS`(`0x60`)2バイトで足りる。

この構造は`vendor/game-music-emu/gme/libgme.a`をリンクした最小限のC
プログラムで実際に`gme_open_data()`→`gme_track_info()`→
`gme_start_track()`→`gme_play()`まで通して事前確認してから
`tests/test_playlist.c`へ組み込んだ（合成ヘッダの各フィールドの
意味を誤解したまま`ctest`だけで確認すると、たまたま通っているだけの
可能性を排除しづらいため）。

UIスモークテスト(`tests/gen_fixture_gbs.c`)は**GBSのままにした**。
実際に音が鳴るNSFフィクスチャを作るには6502のAPU初期化コードを
新たに書く必要があり、ビジュアライザの目視確認という目的に対して
コストが見合わないと判断した。

### (d) 実機検証（完了）

実機（muOS 2601.0 JACARANDA、192.168.0.20）で確認した。テスト素材は
著作権上リポジトリには含めないが、開発機のローカルに手持ちであった
実在のNSF3本（`Downtown Special...`(17トラック)・`Super Mario Bros. 3`
(25トラック)・`Tenkaichi Bushi...`(4トラック)。いずれも拡張チップ無し
[`chip_flags`(オフセット0x7B)がいずれも`0x00`]）と、libgme本体が
リポジトリに同梱しているテスト素材`vendor/game-music-emu/test.nsf`+
`test.m3u`（1トラック、m3u付き）を使った。

1. **クロスビルド**: `./scripts/build-aarch64.sh`成功。
2. **`--list`**: 4ファイルすべて期待通りのトラック数
   （17/25/4/1）で列挙された。`test.nsf`は同梱の`test.m3u`
   （`test.nsf,$00,BGM C,...`。16進トラック番号のみでdecimalの
   0始まり/1始まり問題は検証できない）を自動検出し、曲名が
   `Track 01`ではなく`BGM C`に正しく置き換わることを確認した。
3. **`--cli`実再生**: 上記4ファイルそれぞれを実機上で15秒前後
   再生し、クラッシュ・ALSAアンダーラン("underrun occurred")とも
   **無し**。単一プロセスでの計測が前提で、複数の`mugbs`プロセスを
   同時に立てて音声デバイスを取り合わせると（検証作業中の事故で
   一度発生させた）アンダーランと1秒未満での「トラック終端検出」の
   誤検出が多発する。これは無音の合成フィクスチャ(`Game.gbs`)でも
   同じ条件で再現したため、**NSF固有の問題ではなく、複数プロセスが
   同一オーディオデバイスを取り合う既知の状態**だと判断した(通常の
   単一起動運用では起きない)。
4. **CPU負荷**: `/proc/<pid>/stat`のutime+stimeを実再生5秒間サンプル
   したところ`delta_ticks=0`(`CLK_TCK=100`)、つまり**単一コアの
   0.2%未満**。「NSFは6502エミュレーション+最大8ボイスでGBSより重い
   かもしれない」という着手前の懸念は杞憂だった（対象デバイスの
   CPUに対しては両者ともほぼ計測不能なレベルの負荷）。
5. **パッケージング・GUI**: `./scripts/package.sh`で`.muxapp`を作成
   （290,826バイト。v1.1.0比+104バイトで、ほぼ横ばい。CHANGELOG記載の
   「サイズ増分は無い」を裏付ける）。`/opt/muos/script/mux/extract.sh`
   経由でインストールし、`--version`で`muGBS 1.2.0`を確認。
   `--ui-script`+`--screenshot`でBrowser/Player/TrackList画面を実機上で
   撮影し、目視確認した:
   - Browser: `.nsf`が`.gbs`/`.m3u`と同じ一覧に混在し、大小文字無視の
     名前順で正しくソートされていた
   - Player: `.nsf`を開いて`PLAYING`状態になり、波形ビジュアライザが
     静止画ではなく実際の波形を表示していた（＝無音でなく実際に音声が
     生成されている間接証拠）。ゲーム名フィールドはShift-JISの日本語
     文字列だったため`?`にフォールバックした（SPEC/READMEに記載済みの
     既知の仕様どおりで、今回の不具合ではない）
   - TrackList: 25トラックのNSFで`Tracks (25)`〜`25. Track 25`まで
     過不足なく列挙された

**未検証で残った項目**: 実在のNSF用拡張M3U（decimal番号が1始まりである
こと）は、手元に該当する実配布パックが無かったため実機では確認できて
いない。この点はホスト側の`vendor/game-music-emu/libgme.a`を直接リンクした
実験的な検証プログラムで、`Gme_File::remap_track_()`の挙動を直接確認済み
（`gme_nsf_type_`にはGBS用の`flags_ |= 0x02`パッチが入っておらず、
upstreamのデフォルト=1始まりのまま動くこと。上記(c)節参照）であり、
`tests/test_playlist.c`の回帰テストにも反映してある。拡張チップ
（VRC6/VRC7/FDS/Namco163/Sunsoft5B）を使うNSFも手元に無く未検証。
実在のズレた/拡張チップ入りのNSFが手に入り次第、追記する。

## Issue #15/#16: Repeat=oneのエンドレス化・Default lengthの分単位化

実機で使ってみたフィードバックから出た2件。本文はタイトルのみ
（Issue自体に詳細説明は無い）だったため、着手前にユーザーへ以下を
確認した:

1. 分単位の刻みは1分・範囲1〜10分・既定値は150秒→180秒(3分)へ変更
   （半端な既存値は初回操作で分の目盛りへ吸着させる）
2. エンドレス時のPlayer画面は合計時間を`--:--`にし、シークバーは描かない
3. 再生中のRepeat変更（Yコンボ/Settingsどちらも）はいま鳴っている曲へ
   即時反映する
4. v1.3.0へ上げる

### Issue #15の設計判断

- **フェード無効化の実現方法**: `gme_set_fade_msecs()`の開始時刻に負値を
  渡すと、`Music_Emu::play_()`が`fade_start >= 0`のときしか
  `handle_fade()`を呼ばないため、フェードそのものが恒久的に無効化される
  （vendor/game-music-emu/gme/Music_Emu.cpp）。SPEC 5.1に「落とし穴4」
  として明記した。`0`は「即座にフェード開始」であり無効化ではないので
  混同しないよう注意。
- **判定を1関数に集約**: `playlist_fade_start_ms(length_ms, cfg)`を
  playlist.cへ追加（`cfg->repeat_mode==REPEAT_ONE`なら`-1`、それ以外は
  `length_ms`をそのまま返すだけ）。SDLもlibgmeの初期化も要らない純関数
  なので、`tests/test_playlist.c`にホストだけで回る単体テストを追加した
  （`test_fade_start_ms`）。
- **再生中の即時反映**: `player_apply_config()`に、現在のエントリの
  「名目のフェード開始時刻」(`player_t.fade_at_ms`、新設)を
  `playlist_fade_start_ms()`に通し直して`gme_set_fade_msecs()`を
  張り直す処理を追加した。`one`を抜けたとき、名目の開始時刻を既に
  過ぎていれば`player_tell_ms()`の現在位置まで繰り上げる
  （過去の時刻を渡すと`handle_fade()`がゲインを一気に0まで落として
  ブツ切りになるため。「いまからフェードして次へ」にする）。
  `app_step_repeat_mode()`（Yコンボ）は元々`player_apply_config()`を
  呼んでいなかったため、これも呼ぶよう追加した（Settings画面の
  `adjust_setting()`は元々`app_apply_settings()`経由で呼んでいた）。
- **UI**: `player_current_duration_ms()`は、REPEAT_ONEでフェードが
  無効な間は`0`を返すよう変更した（「長さ不定」を表す）。
  `draw_player()`側は`dur_ms<=0`のとき合計時間を`--:--`にし、
  シークバー自体を描かない1行構成にする（元々の「バー幅が狭い解像度は
  2行構成に落とす」分岐とは別に、バーそのものを省く3つ目の分岐を足した）。
  この分岐は「未再生（曲が無い）」と「エンドレス中」の両方を含むが、
  どちらも「合計時間が定まらない」という点で表示上の扱いは同じなので
  区別しなかった。
- **検証**: `playlist_fade_start_ms()`の単体テストに加え、
  `tests/ui_smoke.script`のPlayer画面Yコンボ区間を
  `none→one→all`の3段階へ拡張し、再生中に`one`へ入って抜ける
  （＝`player_apply_config()`の新分岐の両方）を6解像度×ASan/UBSan込みの
  CIで踏むようにした。ただし合成フィクスチャ(`Game.gbs`/`Game2.gbs`)は
  無音（init/playがRET単体）なので、libgmeの無音自動終了
  （`Music_Emu.cpp`の`silence_max=6`秒。フェードとは独立した仕組み）が
  先に効いてしまい、「フェードせず鳴り続ける」こと自体はこの合成
  フィクスチャでは確認できない（実在のGBS/NSFで確認が要る）。

### Issue #16の設計判断

- **表示だけ分単位・保存は秒のまま**: `config.ini`のキー
  (`default_length_sec`)・レンジ(`config.c`の1..3600)・
  `--duration SEC`は変更しない。Settings画面の`setting_kind_t`に
  `SET_MINUTES`を追加し、`SETTINGS[]`のmin/max/stepを秒のまま
  60/600/60（=1〜10分・1分刻み）にして、表示(`settings_item_text()`)と
  操作(`adjust_setting()`)だけを分単位に見せる。
- **目盛りの吸着**: 既定値変更前の`config.ini`(150秒=2.5分)から
  そのまま`←`/`→`を押しても半端な値のまま動いてしまわないよう、
  `adjust_setting()`のSET_MINUTES分岐で「まず60で割って整数分へ
  丸めてから1段動かす」処理を入れた(`iv = before/60 + direction`)。
  一度でも操作すれば以後は必ず整数分になる。
- **既定値変更**: 150秒→180秒(3分ちょうど)。`src/config.c`・
  `packaging/muGBS/config.ini`・`tests/test_config.c`・SPEC.mdの
  config.iniサンプルを揃えて変更した。

### 実機検証（完了）

`./scripts/build-aarch64.sh`→`./scripts/package.sh`→
`scp muGBS-1.3.0.muxapp root@192.168.0.20:/mnt/mmc/ARCHIVE/`→
`/opt/muos/script/mux/extract.sh`（Archive Managerが内部で呼ぶのと
同一スクリプト）で実機へ導入し、`bin/mugbs --version`が`muGBS 1.3.0`を
報告することを確認した。

**Issue #15（`repeat:one`のエンドレス化）の核心部分は、実在のループ曲を
使って`--cli`ハーネスのログだけで機械的に確認できた**（目視ではなく、
「フェード完了→次トラックへ」のログが出るか出ないかという二値の
確認なので、これが最も確実）:

- 対象は実機の実在ライブラリにあった `Tetris (World) (Rev 1) [BGM].gbs`
  トラック1（有名なBGMで、途中で切れず鳴り続ける実物のループ曲）。
- **対照実験（fixが効いていない場合の基準動作）**: `--repeat none
  --duration 4 --fade-ms 1000`で実行したところ、再生開始から**約5.0秒
  後**（=4秒+1秒フェード、狙いどおり）に`トラック終端検出 -> 次トラックへ`
  のログが出て次トラックへ進んだ。これを2回連続で確認し、この曲・この
  ハーネスで「fixが無ければ短時間で終端検出される」ことを裏付けた。
- **本題**: 同じ曲・同じ`--duration 4 --fade-ms 1000`のまま`--repeat
  one`だけを付けて20秒間観察したところ、**一度も**`トラック終端検出`が
  出なかった（対照実験の4倍以上の時間が経過している）。これは
  `playlist_fade_start_ms()`が`REPEAT_ONE`のときフェード開始時刻を
  `-1`にし、`gme_track_ended()`が真にならなくなっている
  （=エンドレス）ことの直接証拠になる。

**UIの見た目（`--:--`・シークバー非表示・Default lengthの並び）は、
実機のSDLレンダラ経由の`--screenshot`で目視確認した**（ソフトウェア
レンダラのホストではなく、実機の実際の描画パスを通した状態）。
同じ`Tetris (World) (Rev 1) [BGM].gbs`を`--window 640x480`のGUIモードで
開き、`--ui-script`でYコンボを注入して`--screenshot`で最終フレームを
書き出した:

- `Y_RIGHT`を2回（既定の`all`→`none`→`one`）: `repeat:one`・
  `0:00 / --:--`・シークバー非表示を確認。波形ビジュアライザが実際に
  波打っており（無音でなく実際に音が鳴っている間接証拠）、上記
  `--cli`でのログ確認と矛盾しないことも裏付けられた。
- `Y_RIGHT`をもう1回押して`one`から`all`へ抜けた状態: `repeat:all`・
  `0:00 / 3:08`（実測曲長）・シークバーが復活していることを確認。
  「`one`を抜けるとその場からフェードして次へ進む」こと自体
  （音が実際に切れずに繋がるか）は、スクリプト実行が一瞬で終わるため
  この方法では確認できていない（フェード開始時刻をまだ過ぎていない
  タイミングでの遷移だったため、`player_apply_config()`の「既に
  過ぎていたら現在位置へ繰り上げる」分岐は未踏。ロジックは
  `player.c`のレビューと上記の基本経路確認で妥当性を確認済み）。
- `START`のみで開いたSettings画面で`Default length`が先頭にあり
  `3 min`と表示されることを確認（既定値180秒が正しく分表示される）。

検証後、実機の`/tmp`の一時ファイル（ui-script・screenshot・config）を
削除し、`.muxapp`は他バージョンと同様`/mnt/mmc/ARCHIVE/`に残した
（インストール済みバイナリ`1.3.0`自体は意図的にそのまま残置）。

## Issue #19: ながさチェンジ機能（v1.4.0）

Issueの本文は条件付きだった: 「Nintendo Musicのながさチェンジ相当を
実装する。Default lengthがm3uの時間で上書きされるなら必要、Default
lengthが生きる（＝m3u優先のまま）なら実質ながさチェンジそのものなので
不要」。着手前にコードを読んで確認したところ、`playlist.c`の
`playlist_effective_length_ms()`（当時の名前。後述のとおり分割した）は
`info->length > 0`（拡張m3uの曲長欄や実測値）→ `intro+loop` → 
`cfg->default_length_sec` の順で判定しており、**Default lengthは
「曲長が全く分からない曲」専用のフォールバックで、m3uに時間が
書いてある曲には一切効かない**ことが分かった。つまり前者（機能が必要な
方）に該当したため、実装した。

ユーザー確認済みの決定事項:

1. 操作はSettings画面の項目のみ（Player画面のYコンボは追加しない。
   4方向は既にRepeat/Shuffleで埋まっているため）
2. 選択肢は `auto` / 5 / 10 / 15 / 20 / 25 / 30 分（5分刻み）

### 設計判断

- **技術的な裏付け**: GBS/NSFは自然にループし続け、曲を終わらせているのは
  `gme_set_fade_msecs()`のフェードだけである（`handle_fade()`が
  `track_ended_`を立てる。`vendor/game-music-emu/gme/Music_Emu.cpp`）。
  したがってフェード開始時刻を後ろへ倒せば延長、前へ倒せば短縮になり、
  Issue #15で作った`playlist_fade_start_ms()`と同じ層（フェード開始時刻の
  決定ロジック）に素直に乗せられた。
- **設定の持ち方**: `mugbs_config_t`に`length_override_sec`を追加
  （0=auto、非0で全トラックへ強制。config.iniのクランプ範囲は
  `default_length_sec`と同じ0..3600秒にしてあるが、Settings画面が
  実際に出す値は0..1800の5分刻みだけ）。
- **実測値を捨てない**: `Length`を`auto`へ戻したときにm3u/実測の曲長へ
  復帰できる必要があるため、`playlist_entry_t`に`natural_ms`
  （上書きを無視した実測曲長。`length_known==0`なら0）を新設した。
  `duration_ms`（従来からある「今使うべき」実効値）とは役割を分けている。
- **純関数の分割**: 旧`playlist_effective_length_ms()`を2つに割った:
  - `playlist_natural_length_ms(info, &known)`: 既存の「乖離#1」判定
    （`length`/`intro+loop`の有無）だけを行う。
  - `playlist_resolve_length_ms(natural_ms, known, cfg)`:
    `cfg->length_override_sec > 0`なら問答無用でそれを返し、`auto`なら
    従来どおり`known ? natural_ms : default_length_sec*1000`。
  - `playlist_effective_length_ms()`はこの2つを合成する薄いラッパとして
    残し、`player.c`の`start_track_at()`の呼び出しは変更していない。
  - `playlist_apply_default_length()`は`playlist_apply_length_config()`へ
    改名し、`length_known`で絞らず**全エントリ**の`duration_ms`を
    `playlist_resolve_length_ms()`で計算し直すようにした（Issue #16までは
    フォールバック中のエントリだけが対象だったが、Issue #19では実測値の
    ある曲も上書き対象になるため）。
- **再生中への即時反映**: Issue #15の「repeat変更は今鳴っている曲へ即座に
  反映する」という決定に合わせた。`app_apply_settings()`の呼び出し順を
  `playlist_apply_length_config()` → `player_apply_config()`の順に変更し
  （逆順だと`duration_ms`の更新前にフェードを張り直してしまう）、
  `player_apply_config()`側は`p->playlist && p->current_entry >= 0`のとき
  `p->fade_at_ms`を`entries[current_entry].duration_ms`から読み直す1行を
  追加した。あとはIssue #15で既に入っていた「開始時刻を過ぎていれば
  `player_tell_ms()`の現在位置へ繰り上げる」ガードがそのまま働くため、
  曲を短くした場合でもブツ切れにならない。`REPEAT_ONE`は従来どおり
  `playlist_fade_start_ms()`が`-1`を返して優先する（＝`one`の間は
  `Length`の値によらずエンドレスのまま）。
- **UI**: `setting_kind_t`に`SET_LENGTH`を追加した。`SET_MINUTES`
  （Issue #16）とほぼ同じ（秒で保持し表示だけ分単位）だが、`0`を特別扱いで
  `auto`と表示する点が異なるため専用の種別にした。目盛りの吸着
  （`adjust_setting()`）は`SET_MINUTES`と同じ式を共有し、stepが300
  （5分）になるだけで成立した。`SETTINGS[]`の先頭（`Default length`より
  前）に置いた——Issue #16で「最も触る項目」として`Default length`を
  先頭に上げた判断と同じ理由で、`Length`はそれよりもさらに触る頻度が
  高いと判断した。
- **Player画面のステータス行**: `auto`のときは今までどおり何も表示せず
  （`repeat:xxx shuffle:on/off`のまま文字数を食わない）、上書き中だけ
  末尾へ`  len:15m`のように追記する。8x8等幅フォントで狭い解像度だと
  他の項目（`repeat`/`shuffle`）と合わせて`ui_text_clipped()`により
  `...`で省略されることがあるが、これは既存の同じ行の他フィールドも
  同じ扱いを受けており新規の問題ではない。

### 検証

- 純関数レベル: `tests/test_playlist.c`に`test_resolve_length_ms()`
  （`test_fade_start_ms()`と同じ位置付けの、SDL/libgme初期化不要な
  ロジックテスト）を追加。auto/上書き双方、および上書きから`auto`へ
  戻したときに渡した`natural_ms`がそのまま返ることを確認。
- プレイリスト単位: `test_length_override_applies_and_reverts()`を追加。
  `test_sidecar_m3u()`と同じ合成フィクスチャ・拡張M3U構文
  （`0:32`/`2:34`/`1:45`の曲長欄。`vendor/game-music-emu/gme/
  M3u_Playlist.cpp`の`parse_time_()`で秒→ms変換されることをソースで確認
  済み）を使い、(1)スキャン直後から上書き値が使われること、(2)
  `natural_ms`にm3u由来の実測値が残っていること、(3)
  `playlist_apply_length_config()`で`auto`に戻すとファイルを開き直さずに
  実測値へ復元されること、(4)再度上書きへ戻しても正しく効くこと、を
  1つのplaylistオブジェクトに対して連続で確認した。
- 設定の永続化: `tests/test_config.c`に既定値(0)・ラウンドトリップ
  比較・`length_override_sec`単体のパース/クランプ（範囲外の`-5`が`0`へ、
  `999999`が`3600`へ丸まること）のテストを追加。
- UIスモーク: `tests/ui_smoke.script`のSettings区間の先頭に、
  再生中（`SELECT`で再開済み）に`Length`を`auto`から`5 min`へ変える
  ステップを追加し、`player_apply_config()`の新しい`fade_at_ms`更新経路
  （`SET_LENGTH`）を6解像度×ASan/UBSanのCIで踏むようにした。
- ヘッドレスGUI（`--window`+`--ui-script`+`--screenshot`、ホスト上の
  offscreenドライバ）で、Settings画面の`Length auto`表示と、`15 min`に
  変えた直後のPlayer画面（`0:00 / 15:08` = 900秒の上書き + 8秒の既定
  フェード長。ステータス行に`len:15m`が追記されようとしていること）を
  目視確認した。
- CLIハーネス（`--cli`）で、合成フィクスチャ（無音・2トラック）に対し
  `--length 4 --fade-ms 500 --repeat none`を実行し、2トラック分
  （4.5秒×2＝9秒）でちょうど`トラック終端検出`が2回出て停止することを
  実測（`real 0m9.5s`）。Issue #15のときと同じ「ログの二値判定」の
  考え方で、m3u/実測の曲長ではなく上書き値でフェードが張られていることの
  直接証拠になる。
### 実機検証（完了）

`./scripts/build-aarch64.sh` → `./scripts/package.sh` →
`scp muGBS-1.4.0.muxapp root@192.168.0.20:/mnt/mmc/ARCHIVE/` →
`/opt/muos/script/mux/extract.sh`（Archive Managerが内部で呼ぶのと同一
スクリプト）で実機へ導入し、`bin/mugbs --version`が`muGBS 1.4.0`を
報告することを確認した。

**「上書きが効く」ことと「`one`が優先する」ことの両方を、実在の
ループ曲（Issue #15と同じ`Tetris (World) (Rev 1) [BGM].gbs`。同名m3uは
無いので全17トラックとも`length_known==0`＝Default lengthフォールバック
対象）に対する`--cli`のログだけで機械的に確認した**（目視ではなく、
「フェード完了→次トラックへ」のログが出るか出ないかという二値の確認。
Issue #15の検証方針を踏襲）:

- `--length 4 --fade-ms 1000 --repeat none --track 1`（曲全体、17トラック
  を通し再生）: 各トラックとも約4.5秒（=4秒+1秒フェード）で
  `トラック終端検出 -> 次トラックへ`が出て次へ進み、17トラック目安通り
  `real 1m18s`（≒17×4.5秒）で全曲を終えて停止した。m3uも実測値も無い
  曲で、かつ実際には長く鳴り続けるはずのBGMトラックが、狙いどおり
  `Length`の上書き値だけで一律にフェードしていることの直接証拠になる。
- `--length 4 --fade-ms 1000 --repeat one --track 1`: 25秒間観察して
  一度も`トラック終端検出`が出なかった。`playlist_fade_start_ms()`が
  `REPEAT_ONE`のとき`length_override_sec`の値によらず常に`-1`
  （フェード無効）を返すというソースレビューの結論と一致する。
  （余談: 初回試行時にSIGTERM経由の`timeout`で直前のプロセスを止めた
  直後に実行したところ数秒おきに終端検出が出る不安定な結果が一度だけ
  観測されたが、SIGKILLで確実に前プロセスを終了させてから撮り直した
  ところ再現しなかった。前セッションのPipeWire/ALSA状態が残っていた
  ことによる環境起因のノイズと判断し、コード側の問題ではないと結論した）。

**UIの見た目**は実機のSDLレンダラ経由の`--screenshot`（640x480、
`--window`指定なのでmuOS本体の全画面表示は奪わない）で目視確認した:
Settings画面で`Length`を`auto`→`5 min`に変えた直後の表示が`5 min`に
なっていること、`15 min`まで進めてPlayer画面へ戻ると合計時間が
`0:00 / 15:08`（900秒の上書き+8秒の既定フェード長）になっていることを
確認した。ステータス行の`len:15m`はホストでの確認時と同様、狭い表示幅
では`...`で省略される（`repeat`/`shuffle`と同じ既存の挙動で新規の問題
ではない）。

検証後、実機の`/tmp`の一時ファイル（ui-script・screenshot・config）を
削除し、`.muxapp`は他バージョンと同様`/mnt/mmc/ARCHIVE/`に残した
（インストール済みバイナリ`1.4.0`自体は意図的にそのまま残置）。

## Issue #22: 既定ブランチを master から main へ

Issue本文は `git branch -m master main` の一行のみ。着手前に調べた前提:

- オープンな PR は0件（GitHub側の改名でリターゲットが必要なものは無い）
- branch protection / ruleset は存在しない（P13の設計判断のとおり無料
  プラン + private リポジトリでは該当APIが403を返すため、そもそも
  サーバ側で強制できていなかった。移行すべき設定も無い）
- `src/`・`tests/` に `master` 参照は無し。C言語側は無変更

`master` を前提にしていた箇所は3系統: `.github/workflows/ci.yml`
（push/pull_requestのブランチフィルタ）、`.githooks/pre-push`
（保護対象ブランチ名の既定値と、抜け道の環境変数
`MUGBS_ALLOW_PUSH_MASTER`）、`scripts/release.sh`
（リリース元ブランチの既定値）。加えてREADME/SPEC/PLAN/CHANGELOGの
運用手順の記述。

**GitHub側を先にリネームした。** `ci.yml` の `pull_request: branches:`
を先に `main` へ変更した状態で `master` 宛にPRを出すと、`pull_request`
イベントはPRのベースブランチ側のワークフロー定義で評価されるため、
その PR では CI が一切走らなくなる（ci.ymlを直す変更を検証するはずの
PR自体が検証できないというデッドロック）。これを避けるため

```sh
gh api -X POST repos/ka-zuu/gbs-player/branches/master/rename -f new_name=main
```

でGitHub側のブランチ実体・既定ブランチ・オープンPRのリターゲットを
まとめて行ってから、ローカルを追随させ (`git branch -m master main` /
`git branch -u origin/main main` / `git remote set-head origin -a`)、
ファイル修正のPRを `main` 宛に出した。

`MUGBS_ALLOW_PUSH_MASTER` は `MUGBS_ALLOW_PUSH_MAIN` へ改名した
（名前が実態とずれるのを避けるため）。上の「CI とリリース（P13）」節や
「P13の設計判断」節に出てくる `master`・`MUGBS_ALLOW_PUSH_MASTER` は
当時の作業ログとして正確さを優先し、あえて書き換えていない
（`## 検証手順`直下の実行コマンド行だけは今の手順を表すので `main` に
更新した）。

既知の穴: GitHub側の改名からこのPRのマージまでの間、`.githooks/pre-push`
は存在しない `master` を保護対象として探すため、その間だけ `main` への
直push が素通しになる（作業はfeatureブランチ経由だったため実害は無い）。

## Issue #21: 短い曲のスキップ機能（v1.5.0）

Issueの本文は「短い再生時間（3秒や5秒）はスキップする機能をつける。
時間は設定できるように」。GBS/NSFのリップには効果音・ジングル・空
トラックなど数秒しかない曲が混ざっていることが多く、全曲リピートで
流しっぱなしにすると数秒ごとに割り込んで聴取体験を壊す、という既知の
不満点への対応。

ユーザー確認済みの決定事項:

1. スキップ対象の曲は再生順から除くだけでなく、TrackListの一覧・曲数
   （`Tracks (N)`）からも消す（「そもそも載せない」方式）
2. しきい値の既定は `off`・0〜30秒・1秒刻み（Settings画面の新項目
   `Skip short`）
3. 判定は実測長（`natural_ms`）で行う。`Length`（ながさチェンジ、
   Issue #19）で全曲を上書き中でも、中身が数秒の効果音は隠す

### 設計判断

- **「全件」と「可視ビュー」の分離**: `pl_scan_source()`の追記時点で
  弾く素直な実装だと、Settingsでしきい値を変えたときに開き直さない限り
  一覧が古いままになってしまう。かといって`playlist_open()`をやり直すと
  再生が中断する。そこで`playlist_t`に`all[]`（スキャン結果全件。title
  の所有権はここにある）と`entries[]`（そのうち可視なものだけの浅い
  コピー。titleは借用）を分け、`entries[]`/`entry_count`を読む既存の
  呼び出し側（`player.c`、`app.c`の`app_prev_source()`/`app_next_source()`
  等、`main.c`の`print_playlist()`）を無改造のまま使えるようにした。
  `natural_ms`/`length_known`はスキャン時に既に保存済みなので、
  ビューの再構築はlibgmeを呼び直さず安価に行える（`playlist_apply_length_
  config()`が安価なのと同じ理屈）。
- **関数の統合**: `playlist_apply_length_config()`を
  `playlist_apply_config(pl, cfg, keep_source, keep_track)`へ改名・拡張
  した。`duration_ms`の再計算（全件）と可視ビューの再構築を1関数にまとめて
  いるのは、これを分けると`entries[]`が古い`duration_ms`を持った浅い
  コピーのまま残ってしまう齟齬が起きうるため。
- **再生中の曲は必ず可視に残す**: しきい値を変えた瞬間に「いま鳴っている
  曲」が消えると`current_entry`が迷子になる。`playlist_apply_config()`に
  `keep_source`/`keep_track`（可視ビューに関わらず必ず残すトラックを
  `source_index`/`track_index`で指定）を持たせ、`app_apply_settings()`が
  再構築前の`current_entry`からこれを控えて渡す。ビュー再構築後は
  `playlist_find_entry()`で新しい添字を引き、新設した
  `player_reanchor_entry()`（範囲チェック→`current_entry`書き換え→
  `sync_shuffle()`）で`player_t`側を追随させる。emuには一切触れないため
  音は途切れない。
- **全滅ガード**: 全トラックがしきい値以下だった場合、可視0件だと
  `playlist_open()`が「エントリなし」エラーになってしまう。可視が0件に
  なる場合はフィルタ自体を諦めて全件可視に戻す（`LOG_WARN`を出す）。
- **判定は常に実測長**: `playlist_is_short(e, cfg)`は
  `e->natural_ms <= cfg->skip_short_sec*1000`（境界は含む=「以下」）を
  `e->length_known`のときだけ見る、独立した純関数として切り出した。
  `length_override_sec`（Length）による見かけの上書きは`duration_ms`にしか
  影響しないため、これを見ずに常に`natural_ms`で判定することで
  「上書き中でも中身が数秒の効果音は隠れる」という決定事項3を自然に
  満たす。曲長不明（`length_known==0`。`default_length_sec`フォールバック
  対象）のトラックは対象外とし、誤って消してしまわないようにした。
- **設定の持ち方**: `mugbs_config_t`に`skip_short_sec`を追加（0=off、
  非0でその秒数）。`length_override_sec`と同じ流儀で、config.iniの
  クランプ範囲は0..600秒と広め、Settings画面が実際に出す値は0..30の
  1秒刻みだけにした。
- **UI**: `setting_kind_t`に`SET_SECONDS`を追加した。刻みが1秒なので
  `SET_MINUTES`/`SET_LENGTH`のような目盛り吸着（`adjust_setting()`）は
  不要で、既存の`else`分岐（`v = before + direction*step`）がそのまま
  使える。`0`は特別扱いで`"off"`、それ以外は分に丸めず`"5s"`のように
  秒のまま表示する（Issueに挙がった「3秒/5秒」という具体値がそのまま
  読める方が分かりやすいと判断した）。`SETTINGS[]`では`Default length`の
  直後に置いた（どちらも「曲長に関する項目」でまとまりが良いため）。
- **TrackList画面のカーソル**: `app_apply_settings()`の最後で
  `tracklist_sel`/`tracklist_scroll`を新しい`entry_count`へクランプする
  防御的コードを入れた。実際にはTrackList画面はPlayer画面からの`X`で
  開くたびに`tracklist_sel = current_entry`へ作り直され（`app.c`）、
  Settings画面はTrackList画面からは開けない（START入力を受け付けない）
  ため実害は無いはずだが、将来の画面遷移変更に備えた保険。

### 検証

- プレイリスト単位: `tests/test_playlist.c`に7本追加。しきい値以下の
  トラックが隠れること・境界（ちょうどしきい値と同じ長さも隠れる側）・
  `off`（既定）では何も隠れないこと（非退行）・曲長不明のトラックは対象外
  なこと・`Length`上書き中でも実測長で判定されること・
  `playlist_apply_config()`でしきい値を上げ下げして件数が正しく往復
  すること・`keep_source`/`keep_track`で指定したトラックがしきい値以下
  でも残ること・全滅ガードで全件可視に戻ること。
  - 「曲長不明のトラック」を`length_known`混在のm3uで作るには、
    そのトラック用のm3u行自体は書きつつ時間フィールドを空にする必要が
    ある（m3uをロードすると`gme_track_count()`は生のトラック数ではなく
    m3uのエントリ数になるため。`vendor/game-music-emu/gme/
    M3u_Playlist.cpp`の`track_count_ = playlist.size()`）。さらに
    空の名前フィールドを区切りのカンマとして認識させるには、その直後が
    カンマ/ダッシュ/数字である必要がある（`parse_name()`の
    「文字列内のカンマは次の1文字が数字/カンマ/ダッシュに見えるときだけ
    区切りとみなす」仕様）ため、`"GBS,1,,,\n"`のようにカンマを1つ余分に
    要求する。最初`"GBS,1,,\n"`（カンマ2つ）で書いたところ名前が`","`に
    化けて失敗し、ソースを読んでこの仕様を確認してから直した。
- 設定の永続化: `tests/test_config.c`に既定値（0）・`test_spec_sample()`
  への追記・ラウンドトリップ・`skip_short_sec`単体のパース/クランプ
  （範囲外の`-5`が`0`へ、`999999`が`600`へ丸まること）のテストを追加。
- UIスモーク: `tests/ui_smoke.script`のSettings区間、`Default length`の
  直後に、再生中（`SELECT`で再開済み）に`Skip short`を`off`から`1s`へ
  変えるステップを追加し、`entries[]`の再構築と`player_reanchor_entry()`
  を6解像度×ASan/UBSanのCIで踏むようにした（スモークで使う合成
  フィクスチャはm3u無し=曲長不明なので、この操作自体で曲が消えることは
  ない。純粋に画面遷移・クラッシュ回帰の確認）。
- ホストでの`ctest --test-dir build`（17件）・
  `cmake -B build-asan ... && ctest --test-dir build-asan`は全緑を確認
  した。

### 実機検証（完了）

`./scripts/build-aarch64.sh` → `./scripts/package.sh` →
`scp muGBS-1.5.0.muxapp root@192.168.0.20:/mnt/mmc/ARCHIVE/` →
`/opt/muos/script/mux/extract.sh`（Archive Managerが内部で呼ぶのと同一
スクリプト）で実機へ導入し、`bin/mugbs --version`が`muGBS 1.5.0`を
報告することを確認した。

**実データでのフィルタ動作**は、実機の`/mnt/sdcard/ROMS/VGM/GBS/
GB_zopher/Super Mario Land 2 - 6-tsu no Kinka (EMU).zophar.zip`
（zophar.net配布形式。曲ごとに個別m3uを持つ実在のリップ、T-14と同じ形式）
を使い、`--list`のログだけで機械的に確認した（Issue #15/#19の検証方針を
踏襲）。このzipには`Golden Coin`（実測7秒）・`Goal`（実測3秒）という、
Issue本文の「3秒や5秒」という例そのままの短いジングルトラックが実在した:

- `--skip-short 0`（off）: 全40トラックが列挙される
- `--skip-short 10`: 29トラックに減り、`Golden Coin`・`Goal`を含む
  11トラックが一覧・曲数の両方から消えている

CLIハーネスは`playlist_open()`→`playlist_apply_config()`というGUIと
共通のコード経路を通るため、これは実機向けにクロスビルドしたバイナリでの
機能面の直接証拠になる。

**GUIの見た目**はユーザー自身が実機で操作して確認した:
同じzipをPlayer画面から開き、Start→SettingsでカーソルをDefault lengthの
次（`Skip short`）まで動かし、右キーで`10s`まで上げてBでPlayer画面へ
戻り（この時点でconfig.iniへ保存される）、XでTrackList画面を開いたところ
見出しが`Tracks (29)`になり、`Golden Coin`・`Goal`等のジングルトラックが
一覧から消えていることを確認した（`off`に戻すと`Tracks (40)`に復活する
ことも確認済み）。

（余談: GUIの目視確認は当初AIが`--ui-script`+`--screenshot`で自動化
しようとしたが、確認時点で実機の画面は`muxfrontend`（Archive Manager）が
フレームバッファを掴んだまま動作中だった。SSH越しにSDLアプリを直接
起動するとこれと画面を奪い合い、最悪フリーズして実機側の手動復旧が
必要になるリスクがあると判断し、自動化を見送ってユーザー自身による
実機操作に切り替えた。CLIハーネス側の`--list`はSDLの映像サブシステムを
初期化しない`SDL_Init(SDL_INIT_AUDIO)`のみで動くため、この問題は起きない）。

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
git push --dry-run origin HEAD:main   # 拒否されること

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

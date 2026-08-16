# 設計ノート

muChip の実装が「なぜ今の形になっているか」をトピック別にまとめたもの。
仕様そのものは [`SPEC.md`](../SPEC.md)、ビルド・リリース手順は
[`development.md`](./development.md)、日々のルールは
[`CLAUDE.md`](../CLAUDE.md) を参照。作業の経緯・実機検証の実施記録・
却下した途中案の詳細は [`history/plan-archive.md`](./history/plan-archive.md)
（凍結・追記禁止）にある。

**このファイルの運用ルール**: 1トピック = 1見出し。新しい判断が出たら
**該当トピックの記述を書き換えて統合**する。日付や Issue 番号で時系列に
追記しない。「検証した」「実機で確認した」という実施記録はここに書かない
（PR 本文と `CHANGELOG.md` の役割）。

## libgme の使い方と既知の乖離

`vendor/game-music-emu`（libgme、GME_VERSION 0.6.6 相当）を基準にすると、
SPEC の記述と食い違う点がいくつかある。

- **`gme_info_t.play_length` は曲長不明時に `-1` ではなく `150000`
  （既定150秒）を返す**（`gme/gme.h` に明記）。曲長が既知かどうかの判定は
  `length` および `intro_length`+`loop_length` の組で行う。`play_length`
  で判定すると既定秒数フォールバックが永久に発火しない
  （`playlist_natural_length_ms()`、`src/playlist.c`）。
- `gme_set_fade(emu, start_msec)` に加えて
  `gme_set_fade_msecs(emu, start_msec, length_msec)` があり、
  `config.ini` の `fade_length_ms` はこちらで反映する。
- `gme_open_data()` / `gme_load_m3u_data()` は**データをコピーする**
  （ヘッダに "Makes copy of data." と明記）。ただし所有権の扱いは安全側に
  倒し、`archive.c` は1ファイル分だけを保持する遅延展開方針を採る。
- CMake オプション名は `BUILD_SHARED_LIBS` ではなく
  `GME_BUILD_SHARED` / `GME_BUILD_STATIC` / `GME_ENABLE_UBSAN`。静的
  ターゲット名は `gme_static`。
- `gme_play()` の第2引数は**ステレオインタリーブされた `short` の個数**
  であり、フレーム数でもバイト数でもない。SDL オーディオコールバックが
  渡す `len`（バイト数）は `len / sizeof(short)` で変換する（`src/audio.c`）。
- `gme_*` API はスレッドセーフでない。オーディオコールバックとメイン
  スレッドの両方から `Music_Emu*` に触るため、`SDL_LockAudioDevice()` /
  `SDL_UnlockAudioDevice()` で保護する。
- フェードを恒久的に無効化するには `gme_set_fade_msecs()` の開始時刻に
  **負値**を渡す（`Music_Emu::play_()` は `fade_start >= 0` のときしか
  `handle_fade()` を呼ばない）。`0` は「即座にフェード開始」であり
  無効化ではないので混同しないこと。
- `Music_Emu.cpp` の無音自動終了（`silence_max = 6` 秒）はフェードとは
  独立した仕組みで、ループ情報もm3uも無い素のGBS/NSFで機能する。

## libgme フォーク運用（m3u トラック番号の0始まり問題）

`vendor/game-music-emu` は upstream ではなく
[ka-zuu/game-music-emu](https://github.com/ka-zuu/game-music-emu) の
`mugbs` ブランチを参照している。理由は m3u の10進トラック番号の扱い。

libgme は m3u の10進トラック番号を「1始まり」とみなし内部で `-1` する
仕様（`gme/Gme_File.cpp` の `remap_track_()`）だが、zophar.net 配布の
GBS パックなど実際の配布物は「0始まり」の慣習で書かれている。GBS
ヘッダの `first_track` フィールドはそもそも `Gbs_Emu.cpp` 内で一切
参照されておらず、「1始まり変換」という前提自体がGBSの実装と整合しない。
これを実データで検証した上で、`gme/Gbs_Emu.cpp` の `gme_gbs_type_` の
`flags_` に KSS と同じ `0x02` ビットを立てるパッチ（1行差分）を当てて
GBSも16進と同様デコーダの生索引をそのまま使うようにした。

このパッチは **NSF には適用していない**。NSF はヘッダ自体が
`first_song`（1始まり）を持ち、実在のNSF用m3uも1始まりで書かれるため、
upstream のデフォルト動作（10進を1始まりとみなし-1する）とそのまま
一致する。`gme_nsf_type_`（`Nsf_Emu.cpp`）には手を入れていない。

submodule は SHA でピン留めされた独自コミットを指すため、**public な
フォーク**に置いている（private フォークだと Actions の既定
`GITHUB_TOKEN` では読めない）。`origin` がフォーク、`upstream` が
libgme 本家という remote 構成。upstream への追従は
`git -C vendor/game-music-emu fetch upstream` から行う。
`scripts/release.sh` はリリース前に gitlink が `origin` から取得できる
ことを検査する（他所での `git clone --recurse-submodules` が失敗する
事故の再発防止）。

## プレイリストと曲長のモデル

**m3u の扱い**: SPEC は「参照ファイルが複数なら自前でプレイリスト
エントリを構築する」としているが、素直に実装するとトラック番号・時間
パースを自前で再実装することになる。そこで `m3u.c` は **m3u をファイル
参照ごとの連続区間（セグメント）に分割し、区間ごとに m3u テキストを
再構成して `gme_load_m3u_data()` に投げ直す**という薄い前処理に徹する。
トラック番号解釈・曲名・曲長の抽出は常に libgme に委譲される。

zip内に**1曲ごとに個別の`.m3u`を同梱する形式**（zophar.net配布パックに
実在）に対応するため、`playlist_open_zip()` は**全`.m3u`のテキストを
名前順に連結し、1回だけ `m3u_split_segments()` に通す**。同じ`.gbs`を
指す複数行は連結すると自然に1セグメント＝1ソースにまとまり、
`Track 1/18` のように正しいトラック数で表示される（個別に解析すると
全曲が`1/1`になってしまう）。`archive_list()` は列挙順を保証しないため、
連結前に `strcasecmp` で名前順ソートしている。

**曲長解決のレイヤー**: `playlist_natural_length_ms(info, &known)` が
既知/不明の乖離判定を行い、`playlist_resolve_length_ms(natural_ms,
known, cfg)` が config の上書き設定（Length・ながさチェンジ）を適用する。
`playlist_effective_length_ms()` はこの2つを合成する薄いラッパ。

**ながさチェンジ（Length、`length_override_sec`）は3区分で判定する**:

| トラックの種類 | 判定 | 理由 |
|---|---|---|
| ループ構造あり（`loop_length > 0`） | 上書きを適用（延長） | 鳴り続けるので指定時間まで確実に持つ |
| 曲長不明（`length_known == 0`） | 上書きを適用（延長） | 判断材料が無い。素のGBS/NSFの現行動作を保つ |
| 曲長既知かつ非ループ | `min(上書き値, natural_ms)` | 実際に途中で終わる曲。延長せず短縮方向だけ効く |

ループ判定は `loop_length > 0` 単独（`playlist_track_loops()`）で行い、
`playlist_natural_length_ms()` が使う「`intro_length > 0 && loop_length
> 0`」という狭い条件は使わない。m3uの`,-`（全体ループ）や`,0:30`
（先頭からループ）のような表現を取りこぼすため。ループ情報を持つのは
GYM/VGM/SPCと拡張m3uのループ欄だけで、**GBS/NSFのヘッダにはループ情報も
曲長も一切無い**ため、これをゲートにすると素のGBS/NSF全部でながさ
チェンジが死ぬ（採らなかった代替案）。

`REPEAT_ONE` はながさチェンジより優先し、`playlist_fade_start_ms()` が
常に `-1`（フェード無効）を返してエンドレス再生になる。設定変更は
`player_apply_config()` が現在のエントリの `fade_at_ms` を張り直すことで
再生中の曲へ即座に反映される（開始時刻を既に過ぎていれば現在位置まで
繰り上げる。過去の時刻を渡すとブツ切りになるため）。

**短い曲のスキップ（Skip short、`skip_short_sec`）** は常に**実測長
（`natural_ms`）** で判定し、Length による見かけの上書きは無視する
（`playlist_is_short()`）。曲長不明のトラックは対象外（誤って隠さない
ため）。`playlist_t` は `all[]`（スキャン結果全件）と `entries[]`
（可視ビューの浅いコピー）を分けて持ち、しきい値変更のたびに
`playlist_open()` をやり直さず安価にビューだけ再構築する
（`playlist_apply_config()`）。全トラックがしきい値以下になる場合は
フィルタ自体を諦めて全件可視に戻す（全滅ガード）。再生中の曲は
`keep_source`/`keep_track` で必ず可視に残し、`player_reanchor_entry()`
で `player_t` 側の `current_entry` を追随させる。

**シャッフル（`src/shuffle.c`）**: 「nextのたびにランダムな1件を選ぶ」
ではなく、`[0, entry_count)` の順列を Fisher-Yates で1回作り、`pos` と
いうカーソルで前後する設計。これにより「prevはnextをちょうど巻き戻す」
が自然に成り立ち、1周すれば全エントリが1回ずつ再生されることも保証
される。TrackListジャンプ・ソース切替などnext/prevを経由しない曲変更が
あるため、`player_play_entry()`（あらゆる曲変更が必ず通る関所）の最後で
`sync_shuffle()` を呼び、`shuffle.order` を現在の `entry_count` と
`current_entry` に同期させる。末尾から先頭への回り込みは再シャッフル
するが、先頭から末尾への回り込み（prev方向）はしない（直前まで見えて
いた並びを壊さないため）。`REPEAT_ONE` はシャッフルより優先する。

## UI・レイアウト

**解像度非依存化**: 画面座標・文字サイズは `src/ui.c` の `ui_metrics_t`
（`scale = min(w/640, h/480)` から導出）経由でのみ扱い、直接ハードコード
しない。文字サイズは SMALL(8px)/BODY(16px)/TITLE(24px) の3段のみ
（8x8フォントの最近傍拡大でドットの太さがムラにならないよう、意図的に
中間段を足していない）。

**文字サイズの使い分け**（Issue #41）: 3段は「情報の重要度」ではなく
「画面上の役割」で固定的に割り当てる。TITLEは各画面の主題（Browserの
フォルダ名、TrackListのゲーム名、Playerの曲名など）だけに使い、複数行を
TITLEにしない。BODYはリスト行と本文（曲名以外のPlayer情報ブロック等）。
SMALLはヘッダのサブタイトル・カウンタとフッタの操作ヒントに限る。
ヘッダ／フッタ帯の高さ（`ui_metrics_compute()`の`header_h`/`footer_h`）は
この役割分担を前提に、載せる段の実ピクセル高の合計から直接組む
（`line_h`からの間接的な導出はしない。ヘッダ=タイトル1行+サブ1行、
フッタ=SMALL2行）。`ui_draw_header()`/`ui_draw_footer()`（`ui.c`）が
この2段組/2行フッタの描画を共通化しており、文字列と色は呼び出し側
(`app.c`)が渡す(Browser/TrackList/Settings/Theme Editorの4画面が共有。
Playerはヘッダ帯を持たず曲名をTITLEでそのまま出すため対象外だが、
フッタは同じ2行構成を使う)。リスト系画面の選択行は塗りつぶしに加えて
左端へ`THEME_ROLE_ACCENT`の縦バーを添える(`ui_draw_list()`内)。

**Player画面の構成**（Issue #3/#8、P9〜P10 で確定）: 曲名とゲーム名の下に
「時間+シークバー」を1行で（時間はBODY、バーの残り幅を計算）、その下に
`Track n/m` とステータス行（`repeat:xxx shuffle:on/off`、ながさチェンジ
中は `len:15m` を追記）、その下に現在ディレクトリのファイル一覧
（`PLAYER_LIST_MAX_ROWS=7` 行まで）、残りの高さを波形ビジュアライザに
割り当てる。一覧を優先し、両立できない極端な低解像度では波形→一覧の順に
省く（フッタへはみ出させない）。一時ステータスメッセージは専用行を
確保せず、フッタ帯の直上にオーバーレイとして描く。

曲名は `ui_text_scroll()`（`ui.c` の汎用プリミティブ、`title_scroll`
設定・既定on）で横スクロール表示できる。速度・停止時間はすべて
`glyph_px` の倍数で決め、解像度非依存を保つ。

**Player画面のファイル一覧**: `app_t` に専用の2つ目の `browser_t`
（`player_list`）を持つ（`app->browser` と共有すると、Player中に
Browserのカーソル位置が壊れる）。一覧の元にするパスは
`app_open_path()` に渡されたパスそのもの（zip内ソースでも一貫して
使える値がこれだけ）。ディレクトリは一覧に出さない（階層を辿るのは
Browserの役目）。UP/DOWNで一覧のカーソルを動かし、`A`で確定して開く
（即切替ではなく確定ボタン方式。D-pad長押しリピートで `playlist_open()`
が連打されるのを防ぐため）。

**リスト系画面のカーソル折り返し**: Browser/TrackList/Settings/Player
ファイル一覧のUP/DOWNカーソルは端で反対側へ折り返す
（`browser_move_wrap()`、既存の `browser_move()` とは別関数。
`browser_page()` が内部で `browser_move()` を呼ぶため、既存関数を
折り返すよう変えるとページ送りまで巻き添えになる）。**ページ送り
（Browserの`←`/`→`）は対象外**（複数件ジャンプするページ送りを
折り返すと着地位置が押した回数で変わり分かりにくくなるため）。

**ビジュアライザ(F-14)**: libgme の公開C APIでチャンネル別PCMを取り出す
手段は `gme_new_emu_multi_channel()` のみで、`gme_open_file()`/
`gme_open_data()` と両立しない（emu生成経路の全面書き換えが必要になる）
ため、既存のオーディオ経路に手を入れずに済む**混合出力からの簡易
オシロスコープ**を採った。トリガ（立ち上がりゼロ交差）を入れて時間窓を
安定させ、表示ゲイン3倍（GBSの出力は素直に写すと中央に潰れる）で描く。

## カラーテーマ

**9スロット＋派生色**: 画面の色は `src/theme.h` の `theme_slot_t`（背景・
パネル・本文・副文・アクセント・選択行・再生中マーク・警告・充電中の9個）
に集約している。波形背景・Player中央一覧の背景・シークバー背景・確認
ダイアログ背景はスロットにせず、`theme_role_color()` がそのつど
`theme_mix()`/`theme_recede()` で計算する派生ロールにした。プリセット
追加時に決める色数を絞るためと、`Edit theme` サブ画面（Theme Editor）で
編集する項目を9個に抑えるため。派生色の配合率は
`midnight`（Issue #27以前のリテラル値そのもの）を基準に「見た目が
ほぼ変わらない」値へ逆算して決めた（`raised`=4%, `gutter`=15%,
`sunken`=33%, `box_bg`=10%。`tests/test_theme.c` が許容誤差つきで固定）。

**`theme_recede()` のヘッドルームフォールバック**: 波形背景（`sunken`）は
背景色を、本文色とは逆方向（本文が明るければ黒、暗ければ白）へ寄せて
作る。`custom` パレットで背景を純黒/純白にされると、寄せる方向の余地が
無くなり `sunken == bg` になって波形パネルが消える事故が起きる。
各chの余地の最大値が閾値未満のときは `theme_mix(bg, fg, pct/3)` へ
逃がすことで、極端な色を選ばれても何かしらの窪み感は残るようにしてある。

**`theme_best_on()` によるコントラスト保証は限定的に使う**: 全画面の色に
自動補正をかけると「テーマを選ぶ」機能の意義が薄れるため、事故が起きうる
2箇所だけに絞ってある。(1) `ui_draw_list()` の選択行の文字色は `sel` と
`fg`/`bg` のうち輝度差が大きい方を選ぶ（`custom` で `sel` を `fg` に近い
色にされても選択行の文字が消えない）。(2) `Edit theme` サブ画面
（`draw_theme_edit()`）は行の文字とフッタだけ、画面の実効 `bg` に対して
黒/白のうちコントラストが高い方（`theme_best_on(bg, 黒, 白)`）を使う。
この画面は自分自身が編集している最中の `fg`/`bg` の値をそのまま信用
できない唯一の画面（`fg` や `bg` そのものを今まさに書き換えている
可能性がある）なので、テーマ由来の `fg` ではなく黒/白の2択に倒す。
これにより、どんな極端な値を作っても行の文字とフッタ（`B`/`X` の案内）
は必ず読め、かならず抜けられる。スウォッチの枠も同じ関数
（`theme_best_on(スロット色, 黒, 白)`）で選び、`bg` スロット自身の
スウォッチが背景に溶けて見えなくなるのを防いでいる。

**`theme_t` は `ui_t` に値で持つ**: `theme_color_t` は3バイト×9=27バイトと
小さく、ポインタで持つとテーマ切り替え時のライフタイム管理
（`mugbs_config_t.theme_custom` を指すのか、一時的な `theme_resolve()` の
結果を指すのか）が煩雑になる。`app_apply_theme()` が
`app_update_scope()`/`app_update_battery()` と同じく毎フレーム1回、
`theme_resolve()` の結果を `ui_set_theme()` で値コピーする方式にして、
「テーマ変更をどこかで反映し忘れる」事故を構造的に無くした。

**`config.ini` の `[theme]` は常に9キー書く**: `theme` が `custom` 以外の
ときも `[theme]` セクションは省略せず、`theme_custom` の現在値をそのまま
書く。`config.c` の「`KEYS[]` が読み書き両方を駆動し、読めるが書けない
ズレが起きない」という不変条件（同ファイル冒頭コメント）を保つための
判断で、`section_comment()` に「`custom` のときだけ効く」ことを明記して
補っている。

**`Edit theme` は copy-on-first-edit**: サブ画面を開いた時点
（`app_enter_theme_edit()`）では `cfg->theme_id`/`cfg->theme_custom` に
一切触れず、実効テーマを `app_t.theme_edit_working` へコピーして表示する
だけにしてある。「眺めるだけで設定が変わる」副作用を避けるため（プリセット
選択中に一覧を見るためだけに開いて `B` で抜けても `Theme` が変わっていない
のが自然）。最初に値を変えた瞬間（`theme_edit_commit()`）だけ
`cfg->theme_id` を `THEME_CUSTOM` へ切り替え、以後は毎回 `theme_edit_working`
を `cfg->theme_custom` へ書き込む。`X`（確認ダイアログ無しのundo）は
`cfg->theme_id`/`cfg->theme_custom` の両方を入室時の値へ戻す。ここで
「入室時の実効色」ではなく「入室時の生の `theme_id`/`theme_custom`」を
保持しているのが肝: 入室時に `theme_id` が `custom` でなかった場合、
`cfg->theme_custom` には無関係な過去のパレット（以前 `custom` として
使っていた色）が残っている可能性があり、`X` で戻すときにそれを実効色
（＝プリセットの色）で上書きして消してしまうと、後で `Theme` を再び
`custom` へ回したときに過去のパレットが失われてしまう。生の値を
そのまま退避しておくことでこの事故を避けている。

**プリセットはすべてダーク系**: 実機（muOS機）の多くは反射型/低輝度
パネルで、明るい背景色は書字灯下での視認性が落ちる（ユーザー判断）。
ライト系の配色が欲しい場合は `custom` で自分で作る前提とし、`gameboy`
（DMGの4階調緑を基調）・`mono`（白黒ハイコントラスト）・`amber`
（琥珀色CRT風）・`synthwave`（紫＋ピンク）の4種を `midnight`（既定、
旧来の配色）に加えた。

## 入力

**GameController対応は自前実装しない**。muOSは
`/usr/lib/gamecontrollerdb.txt` を実機に同梱しており、`mux_launch.sh`が
`SDL_GAMECONTROLLERCONFIG_FILE` 等をexportするだけで物理ボタンが
`SDL_GameController` として認識される（公開アプリ XMPlayer の
`.muxapp` を実際に展開して確認済み）。`src/input.c` は生Joystick
イベントを自前解釈せず、`config.ini` の `[input] gamecontroller_db`/
`controller_mapping` から `SDL_GameControllerAddMappingsFromFile()`/
`AddMapping()` を呼ぶ経路のみを持つ（`mux_launch.sh` を経由しない開発時
や、DBに載っていない機種向けの上書き手段）。GameControllerのボタン
判定は `SDL_CONTROLLER_BUTTON_*` の論理名のみを使い、生のボタン番号を
決め打ちしない。

**D-pad長押しリピート**: `SDL_CONTROLLERBUTTONDOWN` はキーボードと違い
OSレベルのキーリピートを持たない。`input_t` に `dpad_held[4]`/
`dpad_next_repeat_at[4]` を追加し、`input_poll()` がイベント無しの
アイドルなポーリング時にリピートを合成する（初回350ms後から70ms間隔）。
対象はUP/DOWN/LEFT/RIGHTのみ（A/B等の単発操作は対象外）。
GameController切断時は `dpad_held[]` をクリアする。

**Yコンボ（Player画面での Repeat/Shuffle 直接切替）**: Player画面の全
ボタンが使用済みのため、新ボタンを増やさず `Y` を「押しながらD-Padの
意味を変えるモディファイア」に転用した。`input_t.y_held` を状態として
持ち、`apply_y_modifier()` が押下イベント・D-pad長押しリピート合成の
両方から呼ばれることで、押す順序に関わらず正しく判定する。`Y+Left`/
`Y+Right` はRepeatモードの循環（3値なので連射で行き過ぎても実害が
小さい）、`Y+Up`/`Y+Down` はShuffleの**明示的なon/off**（トグルだと
D-pad長押しリピートでちらつくため）。`Y`単体は何もしない。

## 設定（config.ini）

**Settings画面はBrowser/Playerどちらからも開ける**: SPEC 6.3の入力表は
Startをplayer画面専用としているが、ファイルを開くまで設定に入れないのは
初回体験として悪いため、Browser画面からもStartで開けるようにした
（意図的な逸脱）。

**単一所有権**: `config.ini` の設定は実行中プログラム全体で `main()` が
持つ唯一のインスタンスだけが権威を持つ。`app_t`/`player_t` はポインタで
参照するだけでコピーを持たない（かつては3箇所に値コピーされ食い違う
問題があった）。`audio_callback` は `player_t` を一切参照しないため、
ポインタ化しても新たなスレッド間共有は生じない。

**Settings画面の項目追加パターン**: `setting_kind_t` に種別を足し
（`SET_MINUTES`＝秒で保持し分単位で見せる、`SET_LENGTH`＝`SET_MINUTES`
に「0はauto」を加えたもの、`SET_SECONDS`＝1秒刻みでそのまま表示）、
`SETTINGS[]` の表駆動に1行足すだけで保存・表示・リセット対象への算入が
自動的に揃う。目盛りの吸着（半端な既存値を一段動かしたときに整数へ
丸める）は `adjust_setting()` に集約している。

**設定リセット**: `X` で確認ダイアログを開き、`A` で `SETTINGS[]` に
載っている項目だけを既定値に戻す。`last_path`・`gamecontroller_db`・
`controller_mapping`・`sample_rate` など Settings画面に出てこない値は
対象外（ユーザーの意図せず消えると実機での体験を損なうため）。
`config_set_defaults()` で作った一時的な `config_t` から
`SETTINGS[]` に載っている分だけコピーする実装のため、対象範囲の限定が
自然に実現できている。確認ダイアログは新しい画面を作らず、1つのbool
フラグ（`app_t.settings_confirm_reset`）で表現する。
唯一の例外が `theme_custom`（カラーテーマの`custom`パレット、下記
「カラーテーマ」参照）: `SETTINGS[]` には載らない（Settings画面の一覧
行ではなく `Edit theme` サブ画面からしか触れない）が、`Theme` 自体は
リセット対象であり、リセット後に `Theme` を `custom` へ戻すと編集済み
パレットが復活するのは驚きになるため、`app_reset_settings()` で
明示的に既定（`midnight`と同じ色）へ戻している。

**旧バージョンのconfig.iniへの配慮**: 廃止したキー（`[audio] volume`、
`[voices] mute_mask`）は未知のキー/セクションとして黙って飛ばされ、
前後の有効なキーは通常どおり効く。

**ロケール非依存**: `strtod()`/`printf("%f")` はロケール依存のため、
`config.c` は `setlocale()` を一切呼ばない前提（常に `"C"` ロケール）で
小数値を読み書きする。

**削除した機能**: 音量調整（ハードウェア音量と非連動で紛らわしいとの
ユーザー判断により完全削除、常に最大出力）とチャンネルミュート
（`gme_mute_voices()` まで一度実装したが、実機投入直前にユーザー判断で
不要と削除）。

## バッテリー表示

`src/battery.c` として独立させた（プラットフォームの事実であり、
`browser.c`/`player.c` と同じ層）。判定ロジック
（`battery_should_poll`/`battery_is_low`/`battery_should_show`/
`battery_low_threshold_from_env`）はSDL_Init不要な純関数にして
単体テストできるようにした。`SDL_GetPowerInfo()` はLinuxバックエンドで
`/sys/class/power_supply` を毎回読み直すため**毎フレーム呼んではならず**、
`battery_poll()` が `BATTERY_POLL_INTERVAL_MS`（2秒）でスロットルする。

充電中でも残量そのものだけで「低い」を判定する（色を緑にして区別する
ので、挿した瞬間に赤ゲージが消えて安心してしまうことを避ける）。見た目は
矩形の枠+塗りのゲージのみ（8x8フォントはASCIIのみで絵文字が無いことと、
数値表示だと桁数で幅が変わりマーキーの可用幅がガタつくため）。しきい値は
config.ini のキーにせず、`mux_launch.sh` が `MUCHIP_BATTERY_LOW_PCT`を
環境変数でexportする経路にした（muOS側の設定探索は `GET_VAR` の複数候補を
試す推測ベースで、見つからなければ既定10%にフォールバックする）。

## 文字コードとフォント

文字描画は外部フォントライブラリを使わず内蔵ビットマップフォント
（`vendor/font8x8`、ASCIIのみ、パブリックドメイン）を使う。実機の
`sysroot/` にはSDL2の`.so`しか無く、SDL2_ttfが実機に存在するか
未確認のため、新規の実行時依存を増やさない選択をしている。

NSF/GBSヘッダやM3Uの拡張コメントがShift_JIS(CP932)で書かれている場合
（実例で確認済み）に文字化けする問題（Issue #29）へは、取り込み境界
（`src/text.c`）とグリフ描画（`src/ui.c`）を疎結合な2層に分けて対応した。

- **`text_dup_utf8()`**: 判定順「①全バイトASCII→そのまま ②妥当なUTF-8
  →そのまま ③妥当なCP932→UTF-8変換 ④どちらでもない→ASCIIは通し'?'へ
  丸め」。**②を③より先に試すのが肝**（日本語UTF-8はCP932としても妥当な
  バイト列になり得るため、順序を逆にすると誤判定する）。CP932変換表は
  `tools/make_cp932_table.py`でビルド前に事前生成（ビルド時にPython
  不要）。`playlist.c` の `dup_meta()` がこれをラップし、m3uの曲名も
  同じ経路を通るため追加コード無しで対象になる。
- **美咲フォント（`vendor/misaki`、JIS第1・第2水準相当）**をASCIIとは
  別の第2テクスチャアトラス（`ui->cjk_atlas`）に展開し、コードポイント
  128以上をここから引く。全角も1セル8px幅のまま描く（美咲フォントの
  全角グリフがちょうど8x8ドットに収まるため、等幅前提のレイアウト計算を
  一切変更せずに済んだ）。アトラス構築失敗時も `ui_init()` 自体は失敗
  させず `?` フォールバックへ委ねる。

**スコープ外**（意図的に対応しないもの）: Browserのファイル名表示
（ファイルアクセスにも使う文字列のため変換すると開けなくなる）、
元ファイル自体の書き換え。

## クロスビルドと muOS パッケージング

muOSの独自ビデオドライバ（`mali`、Allwinner H700系のMali GPU直結
フレームバッファドライバ）は標準ディストリのSDL2と別物で、
**Debianの`libsdl2-dev:arm64`でビルドしたバイナリは実機で動かない**
（X11/Wayland/PulseAudio等への直接リンクがロードに失敗する）。実機の
glibc(2.38)はDebian bullseyeのクロスツールチェインが持つglibc(2.31)より
**新しく**、素朴にリンクすると実機SDL2が要求する新しいシンボルが
解決できない。**Debianの`crossbuild-essential-arm64`は`--sysroot`
フラグを無視し常に`/usr/aarch64-linux-gnu`をsysrootとして使う**ため、
`CMAKE_SYSROOT`の指定だけでは機能しない。

対処: `scripts/fetch-sysroot.sh` が実機からSSH/SCPで `sysroot/` を構成し
（SDL2バージョンを実機の`.so`から自動検出）、`docker/Dockerfile` が
その内容を `/usr/aarch64-linux-gnu/{lib,include/SDL2}` へ上書きコピー
する。`cmake/toolchain-aarch64.cmake` はコンパイラ指定のみ
（`CMAKE_SYSROOT`は使わない）。期待する動的リンク先（`NEEDED`）は
`libSDL2-2.0.so.0` / `libm.so.6` / `libc.so.6` の3つだけ（libstdc++が
紛れ込むと実機で`GLIBCXX_3.4.xx not found`になる回帰の早期発見に使う）。

**muxapp パッケージング**（SPEC 9章の記述より実際の`.muxapp`と
`extract.sh`のソースを優先した箇所）:

- zip内トップは`<AppName>/`のみ（`extract.sh`が`application/`直下へ
  展開するため。SPEC通りに`mnt/mmc/MUOS/application/<name>/`にすると
  二重ネストして動かない）
- `lib/`は同梱しない（SDL2だけが実機の動的ライブラリ、他は静的リンク）
- `SETUP_APP "$APP_BIN" ""`（第2引数は空。ユーザーがmuOS全体で選んで
  いるretro/modernレイアウトを尊重するため。論理ボタン名で解釈するので
  影響を受けない）
- `glyph/`と`grid/`の両方を同梱（テーマによってどちらが使われるか
  変わる）

`MUCHIP_START_DIR` 環境変数（`mux_launch.sh`が音楽ディレクトリを自動
検出してexport）は `--start-dir > last_path > MUCHIP_START_DIR > "."`の
優先順位で、F-13（前回開いた場所の復元）を潰さないよう最低優先度にして
ある。

ホストで実機を模した確認をする際は、`mux_launch.sh` をSSH経由で直接
起動しない（`muxfrontend`のフォアグラウンド受け渡しを経由せず、終了後に
画面が固まる既知の問題がある。実機検証手順は [`development.md`](./development.md)
参照）。

## 開発基盤（CI・リリース・サニタイザ）

**CIでクロスビルドはしない**。`sysroot/`は実機から抜いたバイナリで
リポジトリに含めない方針のため、CI環境だけでは`.muxapp`を作れない。
CDは「タグを打ったらCIが成果物を作る」形ではなく、「開発機で
`scripts/release.sh`が作ってアップロードし、Actionsは整合性の裏取り
（タグ・バージョン・CHANGELOGの整合性＋クリーンなチェックアウトでの
フルCI）だけをする」形にしている（Release Guardワークフロー）。

**`main`直push禁止**は`.githooks/pre-push`で自衛している。GitHub側の
branch protection/rulesetと併用する多重防御で、ruleset はサーバ側の
設定でありクローン直後には効かないためフックで補っている。
`git config core.hooksPath .githooks` をクローンごとに1回実行する必要が
ある。

**shellcheckは`-s sh -S warning`**（`error`ではなく）。実機の busybox
ashで動く`mux_launch.sh`のbashism検出（SC3xxx）はseverity=warningなので
`-S error`では拾えない。`MUCHIP_REQUIRE_SHELLCHECK=1`（CIとreleaseが
立てる）は「shellcheckが無いこと」自体を失敗にし、apt書き忘れで静的
解析が無言で消えるのを防ぐ。

**UBSanは`-fno-sanitize-recover=all`が必須**。無いと診断を出すだけで
終了コードが0のままになり、CTestが未定義動作を見逃して偽の緑になる。
テストハーネス自身の意図的なリーク（`path_in()`の`strdup`）は
`tests/test_util.h`へ集約し`atexit`で解放することでLeakSanitizerを
有効にしたまま全件緑にしている。

**CHANGELOG.mdの書式は`scripts/release.sh`/`release-guard.yml`の契約**。
見出しを`## vX.Y.Z - YYYY-MM-DD`の1行固定にし、これをそのままリリース
本文として切り出す。`CMakeLists.txt`の`project()`を1行で書く制約と
同じ「機械が読む書式を人間が壊したら機械が気づく」思想。

**release.shの順序設計**: 取り返しのつかない操作（タグ作成・push・
リリース作成）を最後にまとめ、前段の検査・ビルド・パッケージングで
落ちてもリモートには何も残らないようにしている。リリースは既定で
下書きとして作り、Release Guardが緑になってから公開する。

## 削除した機能とその理由

- **チャンネルミュート(F-10)**: SPEC 6.3に従い一度実装（`gme_mute_voices()`
  の配線・Player画面SELECTでのミュートパネル・専用テスト・4chすべて
  鳴る合成GBSフィクスチャまで揃えてホストで動作確認済みだった）が、
  実機投入直前にユーザーから「不要」との判断があり関連コード一式を
  削除した。合成GBSフィクスチャの「音が出る」実装だけはビジュアライザの
  目視確認に有用なため残した。
- **音量調整**: 実機のハードウェア音量と本アプリのソフトウェア音量が
  別々に効き紛らわしいとのフィードバックで、ユーザー判断により機能
  そのものを削除（Settingsに「100固定で表示だけ残す」案は不採用）。
  常に最大出力(100)で`gme_play()`する。

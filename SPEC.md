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
- GBS/NSF 以外の形式の**積極的な**サポート（ただし libgme が対応する
  SPC/VGM 等は「たまたま動く」状態で構わない。UI上で排除しない）。
  `.nsf`/`.nsfe`（NSF, Nintendo Sound Format）は GBS と同格の一級市民として
  正式サポートする（F-27, Issue #2）。GBS 用に組んだ「単体ファイル＋同名
  サイドカーm3u／m3u直接／zip」の枠組みがそのまま形式非依存で動くこと、
  および libgme の NSF/NSFE デコーダが元々静的リンクされていたことから、
  対応コストが低い（詳細は PLAN.md「Issue #2」参照）
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
| F-07 | 曲長が判明している場合、終端でフェードアウトし自動的に次トラックへ進む。ただしリピートが1曲リピート（`one`）のときはフェードせずエンドレスに再生し続ける（Issue #15） |
| F-08 | 曲長が不明な場合、設定した既定時間（デフォルト180秒 = 3分。Settings画面では分単位で編集する。Issue #16）で次へ進む（リピートが `one` のときはF-07と同様エンドレス）。`Length`（F-28）が `auto` 以外のときはこのフォールバック自体が使われない（F-28が優先する） |

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
| F-25 | シャッフル再生（エントリの再生順をランダム化する。P10で実装） |
| F-26 | バッテリー残量の画面表示（常時 / 残量低下時のみ / 非表示。Issue #7で実装）。しきい値は muOS 側の設定があればそれ、無ければ10% |
| F-27 | `.nsf` / `.nsfe`（NSF, Nintendo Sound Format）を `.gbs` と同格に扱える。単体ファイル＋同名サイドカーm3u・m3u直接・zip同梱のいずれの経路も共通（Issue #2で実装）。10進の拡張M3Uトラック番号はNSFでは1始まり（GBSは0始まり。5.2節参照）で、これはlibgme本家の既定動作そのものであり追加パッチは不要 |
| F-28 | ながさチェンジ: Settings画面の `Length` を `auto` 以外（5〜30分・5分刻み）にすると、m3uの曲長・実測値の有無を問わず全トラックの曲長をその値へ強制する（Issue #19）。`auto`（既定）では従来どおりF-07/F-08の判定に戻る。`Repeat: one`（F-07のエンドレス化）はF-28より優先する。変更はいま鳴っている曲へ即座に反映される |

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
/* リピートが one のときは開始時刻を負にしてフェード自体を無効化する
   (エンドレス再生。Issue #15。落とし穴4参照): gme_set_fade(emu, -1); */

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

#### ★ 落とし穴 4: フェードを無効化するには開始時刻を負にする（Issue #15）

`gme_set_fade`/`gme_set_fade_msecs` に「フェードを掛けない」ための専用APIは
無い。`start_msec` に**負値**を渡すとフェードが恒久的に無効化される
（`Music_Emu::play_()` が `fade_start >= 0` のときだけ `handle_fade()` を
呼ぶため。実体は `vendor/game-music-emu/gme/Music_Emu.cpp`）。
リピートが `one` のときはこれを使い、エンドレス再生（F-07/F-08 の
ただし書き）を実現している（`src/playlist.c` の `playlist_fade_start_ms()`）。
`0` はフェードが「即座に開始する」設定であり無効化ではないので注意。

---

### 5.2 拡張M3U の扱い（F-03）

#### 形式

GBS/NSF系の拡張M3Uは以下の形式を取る（1行1トラック）：

```
# コメント行
Game.gbs::GBS,0,Title Screen,0:32,,0:05
Game.gbs::GBS,$02,Overworld,2:34,2:34,0:08
Game.gbs::GBS,2,Battle,1:45
```

フィールド（カンマ区切り、空欄あり）:

```
<file>::<TYPE>,<track>,<title>,<time>,<loop>,<fade>,<artist>,<amp>
```

- `<track>` は10進または `$` 始まりの16進。**16進は常に0始まり**
  （生のsubtrack索引そのもの）。**10進の始まりは形式(TYPE)ごとに違う**
  （下記参照）
- `<time>` は `m:ss.mmm` 形式。`-` はループ扱い
- 空欄は省略可

> **GBSの10進トラック番号は0始まり（P12）、NSFは1始まり（Issue #2）**:
> `<track>` フィールドの形式(`::GBS,...` / `::NSF,...`)ごとに、10進表記の
> 意味が異なる。同梱の libgme (`game-music-emu`) はデフォルトで、10進の
> m3uトラック番号を「1始まり」とみなし内部で-1する仕様(`Gme_File::remap_track_()`。
> `$`始まりの16進はこの対象外)。実機で実際に使う zophar.net配布パックの
> **GBS用**m3uは10進トラック番号が0始まり(`GBS,0,...`が1曲目)であり、
> このデフォルトの前提と食い違って全曲が1つズレて再生される不具合が
> あった。KSS形式が既に持っていた `flags_ |= 0x02`(「10進もそのまま使う」)を
> `vendor/game-music-emu/gme/Gbs_Emu.cpp` の **GBS形式(`gme_gbs_type_`)にのみ**
> 適用して修正した(GBSヘッダの`first_track`フィールドはlibgme内で一切
> 参照されておらず、native表現が0始まりであることとも整合する)。詳細は
> PLAN.mdの「P12」を参照。
>
> 一方、実機で使う **NSF用**m3u・NSFファイル自体のヘッダ(`first_song`)は
> いずれも10進のsong番号が1始まり(`NSF,1,...`が1曲目)であり、これは
> upstreamのデフォルト動作(1始まりとみなして-1する)とそのまま一致する。
> そのため `gme_nsf_type_`(`vendor/game-music-emu/gme/Nsf_Emu.cpp`)には
> GBSのような `flags_` パッチを**当てていない**。GBS用の0始まりパッチを
> 誤ってNSFにも適用すると、逆に全曲が1つズレる新たな不具合になるので
> 注意（Issue #2で確認済み。`tests/test_playlist.c` の
> `test_nsf_sidecar_m3u_is_one_based()` 参照）。

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

4. `.gbs`/`.nsf`/`.nsfe` 等の単体音楽ファイルを直接開いた場合、**同ディレクトリに
   同名の `.m3u` があれば自動で読み込む**。（例: `Game.gbs` → `Game.m3u`、
   `Game.nsf` → `Game.m3u`）この経路は形式によらず共通
   (`playlist.c` の `playlist_open_music_file()` 参照)。

---

### 5.3 zip 対応（F-04）

`archive.c` に miniz ベースで実装する。

```
zip を開く
 ├─ 中央ディレクトリを列挙
 ├─ 拡張子で分類: .gbs/.gb/.nsf/.spc → 音楽, .m3u → プレイリスト
 ├─ .m3u が1つ以上ある場合
 │    └─ **全ての .m3u をメモリ展開してマージする**（最初の1つだけを
 │       採用しない）→ 参照される .gbs もメモリ展開
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
- **zip内に `.m3u` が複数ある場合は、全てを処理してマージした1つの
  プレイリストにする（最初の1つだけを採用して残りを捨ててはならない）**。
  zophar.net配布パック等、1曲ごとに個別の `.m3u` ファイル
  （`01 BGM #01.m3u`, `02 BGM #02.m3u`, ...）を同梱する形式が実在するため。
  この形式では「最初の1つだけ採用」だと1曲しか再生できなくなる
  （実機検証で発見。P9参照）

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
  - `REPEAT_ONE`: 同一トラックを再開（シャッフルより優先する）。フェードも
    無効化してエンドレスに再生する（Issue #15。詳細は5.1の落とし穴4）
  - `REPEAT_ALL`: 先頭に戻る
- 曲送りの際は必ず `SDL_LockAudioDevice()` で保護
- フェード中（`gme_track_ended()` 直前）にユーザーが next を押した場合、即座に切り替える
- シャッフル再生（F-25, P10）が有効なとき、`next_track()`/`prev_track()` の
  「次/前」はシャッフル順（`shuffle.c` が管理する `[0, entry_count)` の
  順列）に従う。`REPEAT_ALL` での周回時は次の周回のぶんだけ並びを作り直す
  （同じ順番を繰り返さないため）。`REPEAT_ONE` はシャッフルより優先し、
  常に現在のトラックを再開する。TrackListからのジャンプやL2/R2でのソース
  切替のように next/prev を経由しない曲変更も、`player_play_entry()` の
  中でシャッフル順の現在位置を同期し直すため、以降の next/prev はそこを
  起点に進む。

---

## 6. UI仕様

### 6.1 画面

| 画面 | 内容 |
|---|---|
| **Browser** | ファイル一覧。ディレクトリ階層を辿る。`.gbs` `.gb` `.nsf` `.nsfe` `.m3u` `.zip` のみ表示（設定で全表示可。`.nsf`/`.nsfe`はIssue #2で追加） |
| **Player** | 曲名（見切れる場合、`[ui] title_scroll` が既定onなら横スクロール表示。Issue #8）・ゲーム名・作者・著作権・トラック `n/N`・経過/全体時間とシークバー（同一行。リピートが `one` でフェード無効(エンドレス)のときは全体時間を `--:--` にしシークバーを描かない。Issue #15）・**現在のファイルが属するディレクトリのファイル一覧（中央。Browserと同じ拡張子フィルタ。ディレクトリは出さない。カーソルを青、再生中のファイルを黄でハイライト）**・波形ビジュアライザ（下部） |
| **TrackList** | 現在のファイルの全トラック一覧。直接ジャンプ可能 |
| **Settings** | Length（先頭。ながさチェンジ。auto/5〜30分・5分刻み。F-28, Issue #19）・デフォルト曲長（分単位・1分刻み。Issue #16）・リピート・シャッフル（F-25, P10）・ステレオ深度・EQ・Fade・Show all files・Scroll title（Issue #8）・Show battery（F-26, Issue #7）。`X`で全項目を既定値に戻す確認ダイアログを開ける（P10） |

> **バッテリー残量表示（F-26, Issue #7）**: 4画面すべてのタイトル行右端に
> 残量ゲージ（矩形の枠＋残量ぶんの塗り。8x8フォントはASCIIのみで絵文字が
> 無いため数値は出さない）を表示できる。`[ui] battery_show` で
> `off`/`low`/`always` を選ぶ（既定 `low`＝残量が少ないときだけ）。色は
> 通常=グレー、残量が少ない=赤、充電中=緑。Playerでは曲名の横スクロール
> （Issue #8）の可用幅がゲージ分だけ縮む。読み取れない値（デスクトップ等
> バッテリーの無い環境）は常に非表示。

> P8で実装したチャンネルミュート(F-10)はユーザー判断により削除済み。
> 音量調整も同様の理由（本体ハードウェア音量と非連動で紛らわしい）で
> 廃止し、常に最大出力で `gme_play()` する（P9。詳細はPLAN.md）。

### 6.2 レイアウト規則（解像度非依存）

- 起動時に `SDL_GetCurrentDisplayMode()` で解像度を取得
- 640x480 を基準とし、`scale = min(w/640.0, h/480.0)` でスケーリング
- フォントサイズ・余白・行高すべてを `scale` 倍する
- **座標のハードコード禁止**
- バッテリーゲージ（F-26）のような右詰め要素は `screen_w` からの右詰めで
  幅を確保し、本文（パス・曲名等）の描画幅をその分だけ縮めてから描く
  （逆順にすると重なる）。ゲージのみで数値を出さないため、残量の桁数で
  可用幅が揺れる心配は無い

### 6.3 入力（SDL GameController 準拠）

| ボタン | Browser | Player |
|---|---|---|
| D-Pad Up/Down | カーソル移動 | 中央のファイル一覧のカーソル移動（再生は変わらない） |
| D-Pad Left/Right | ページ送り | 前/次トラック |
| A | 決定・開く | 決定：カーソルが指すファイルを開いて再生 |
| B | 上の階層へ | Browserへ戻る |
| X | — | TrackList を開く |
| Y | — | （単体では未使用。下記「Yコンボ」参照） |
| L1 / R1 | — | シーク -5s / +5s |
| L2 / R2 | — | 前/次ソース（開いている m3u/zip 内でファイルを跨ぐ） |
| Start | — | Settings |
| Select | — | 再生／一時停止 |
| Menu長押し | 終了 | 終了 |

> Player で `A` を「決定」に割り当てたのは、Browser・TrackList と同じく
> `A`＝決定で揃えるため（P9）。玉突きで再生／一時停止は `Select` へ移した。
> カーソルが動いた瞬間に即再生する案もあったが、確定ボタンを挟む方式を
> 選んだ（D-Pad の長押しリピートでファイルを開き続けてしまわない、という
> 副次的な利点もある）。

> muOS はデバイスごとにボタン配置が異なる。`SDL_GameControllerOpen()` を使い、
> 生のボタン番号を決め打ちしないこと。GameController として認識されない場合は
> `SDL_Joystick` にフォールバックし、`config.ini` でマッピングを上書き可能にする。

> **カーソルの折り返し（P9）**: リスト系画面（Browser・TrackList・Settings・
> Playerのファイル一覧）のD-Pad Up/Downによる単純なカーソル移動は、
> 最下段でDownを押すと先頭へ、先頭でUpを押すと最下段へ折り返す。
> ページ送り（Browserの D-Pad Left/Right）は対象外（クランプのまま）。

> **Yコンボ（P11）**: Settings画面まで入らずにRepeat/Shuffleを変えられる
> よう、Player画面で `Y` を押しながら D-Pad を押すと意味が変わる。
> `Y+Left`/`Y+Right` でRepeatモードを1段ずつ進める・戻す（Settings画面の
> 並びと同じ none→one→all の順で循環）。`Y+Up`/`Y+Down` でShuffleを
> 明示的にon/off（トグルではない。D-Padの長押しリピートで連射されても
> 同じ値を再代入するだけになるようにするため）。`Y`単体（押して離すだけ）
> は何もしない。Playerのステータス行（`repeat:xxx shuffle:on/off`）で
> 変更後の値をすぐ確認できる。

> **D-Pad長押しリピート（P8実機検証で追加済み）**: GameControllerの
> ボタンはキーボードと違いOSレベルのキーリピートを持たないため、
> `input.c` が押下状態と経過時間を自前で追跡し、長押し中は
> UP/DOWN/LEFT/RIGHTを一定間隔で再送する（初回350ms後、以降70ms間隔）。

> **Settings画面の `X`（P10）**: 上表はBrowser/Player限定だが、Settings画面
> でも `X` を使う。全項目を既定値に戻す確認ダイアログを開き、`A`で確定
> （`show_all_files`等も含めSETTINGS[]に載っている項目だけを戻す。
> `last_path`やコントローラ設定は対象外）、`B`でキャンセルする。

---

## 7. 設定ファイル

`/run/muos/storage/application/muGBS/config.ini`

```ini
[playback]
default_length_sec  = 180  ; 曲長不明時の再生秒数(Settings画面では分単位で編集する。Issue #16)
length_override_sec = 0    ; ながさチェンジ。0=auto、非0で全トラックの曲長を強制 (F-28, Issue #19)
fade_length_ms     = 8000
repeat_mode        = all   ; none | one | all
shuffle            = false ; シャッフル再生 (F-25, P10)
sample_rate        = 44100

[audio]
stereo_depth = 0.15
eq_bass      = 0
eq_treble    = 0

[ui]
show_all_files = false
title_scroll   = true   ; 曲名が見切れるとき横スクロールさせる (Issue #8)
battery_show   = low    ; off | low | always。バッテリー残量ゲージ (F-26, Issue #7)
last_path      = /mnt/mmc/MUSIC
```

> 音量調整機能はP9で廃止した（常に最大出力。PLAN.md参照）ため
> `[audio] volume` キーは存在しない。チャンネルミュート(F-10)はP8で
> 実装後に削除したため `[voices]` セクションも存在しない。

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
不要なエミュレータを削って軽量化してもよい（`USE_GME_GBS=ON` /
`USE_GME_NSF=ON` / `USE_GME_NSFE=ON` は必須。F-27, Issue #2）。

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

> **バッテリー低下しきい値の探索（F-26, Issue #7）**: `mux_launch.sh` は
> `GET_VAR` でしきい値の候補キーを順に試し、`1..99` の整数が返った
> 最初のものを `MUGBS_BATTERY_LOW_PCT` として export する
> （`packaging/muGBS/mux_launch.sh` 参照。絶対パスは書かず `GET_VAR` 経由
> なのでSPEC 12/13に反しない）。候補キーはどれが実際に生きているか
> 実機でしか分からないため推測で、全候補の結果を `log.txt` に残す。
> 何も見つからなければ export されず、`src/battery.c` の
> `battery_low_threshold_from_env(NULL)` が既定の10%を返す。

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

### 9.5 リリース（P13）

- タグは `vX.Y.Z`。版番号の唯一の情報源は `CMakeLists.txt` の
  `project(mugbs VERSION ...)`
- ユーザー向けの変更履歴は `CHANGELOG.md`。見出しは
  `## vX.Y.Z - YYYY-MM-DD` の1行固定で、これが GitHub Release 本文の生成元
- リリース作業は `scripts/release.sh` に集約する。検査 → ホストビルド +
  CTest → クロスビルド → `.muxapp` 生成 → タグ → 下書きリリース、の順で、
  取り返しのつかない操作は最後にまとめる
- **`.muxapp` は CI では作らない。** クロスビルドには実機から抜いた
  `sysroot/` が要り、これは実機由来のバイナリなのでリポジトリに置かない
  （8.3 参照）。GitHub Actions 側（`release-guard.yml`）が行うのは、
  タグ・`CMakeLists.txt`・`CHANGELOG.md` の整合性と、クリーンな
  チェックアウトでのフル CI だけ
- リリースは**下書きとして作り**、Release Guard が緑になってから公開する

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
| ~~T-10~~ | ~~4chミュート~~ | F-10ごとP8で削除済み（欠番） |
| T-11 | 再生中に next 連打 | クラッシュせず、音が途切れず切り替わる |
| T-12 | 壊れた `.gbs` | エラーメッセージを表示し、クラッシュしない |
| T-13 | 存在しないファイルを参照するm3u | 該当エントリをスキップし警告表示 |
| T-14 | `.m3u` が複数(曲ごとに1ファイル)入った `.zip` | 全ての `.m3u` がマージされ、全曲が再生できる（P9） |
| T-15 | バッテリー残量表示（F-26, Issue #7） | 4画面右上に残量ゲージが出る。`Show battery`(off/low/always)がconfig.iniへ保存・復元される。`low`では10%(既定)を境に表示/非表示が切り替わる |
| T-16 | ながさチェンジ（F-28, Issue #19） | Settingsで`Length`を`auto`以外にすると、m3uの曲長・実測値があるトラックも含め全トラックの曲長がその値になる。`auto`へ戻すと元の値(m3u/実測)へ復元される。変更は再生中の曲へ即時反映される |

### 10.3 テスト用素材

著作権上の理由からリポジトリに `.gbs` は含めない。
`tests/fixtures/` には **合成した最小GBSファイル**（ヘッダのみ有効な擬似ファイル）と、
各種パターンの `.m3u` テキストのみを置く。m3uパーサのユニットテストはこれで行う。

### 10.4 CI（GitHub Actions、P13）

`.github/workflows/ci.yml` が PR と main への push で次を回す。

| ジョブ | 内容 |
|---|---|
| ホストビルド + CTest | `scripts/build-host.sh` → `ctest`。`MUGBS_REQUIRE_SHELLCHECK=1` を立て、SKIP が1件でもあれば失敗させる |
| ASan/UBSan | `-fsanitize=address,undefined -fno-sanitize-recover=all`、`ASAN_OPTIONS=detect_leaks=1` |

- ヘッドレスUIスモークは `SDL_VIDEODRIVER=dummy` / `SDL_AUDIODRIVER=dummy`
  で走るのでランナーに X も音声デバイスも要らない
- **CI でクロスビルドはしない**（9.5 参照）。実機検証は人手で行い、結果は
  `PLAN.md` に記録する
- シェルスクリプトの静的解析は専用ジョブを作らず `tests/test_package.sh`
  に集約する（検査対象リストを二重管理しないため）
- `-fno-sanitize-recover=all` は必須。これが無いと UBSan は診断を出すだけで
  終了コードが 0 のままになり、CTest が未定義動作を見逃して緑になる

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
| **P8** | ビジュアライザ、EQ（チャンネルミュートは実装後ユーザー判断で削除） | SHOULD/NICE要件 |
| **P9** | 実機フィードバック対応（zip複数m3uマージ、音量調整の廃止、カーソル折り返し、Player入力再割当、Playerへのファイル一覧追加） | T-14が通り、各項目が実機で確認できる |
| **P10** | 実機フィードバック対応・第2弾（シャッフル再生 F-25 ほか） | 各項目が実機で確認できる |
| **P11** | Player画面から Repeat/Shuffle を直接変える Yコンボ | 実機で物理ボタンだけで切り替えられる |
| **P12** | m3uの10進トラック番号が0始まりであることに対応 | zophar.net 配布パックで宣言通りのトラックが鳴る |
| **P13** | GitHub の PR 運用・CI・リリース自動化 | PR で CI が回り、`scripts/release.sh` でタグと Release が作れる |

---

## 12. コーディング規約

- 言語: **C11**（libgme連携部のみ C++ でも可だが、極力Cで完結させる）
- 命名: `snake_case`。モジュール名をプレフィックスに（`player_next_track()`）
- エラー処理: 戻り値でエラーを返す。`assert` に頼らない
- **メモリ**: 全ての `malloc` に対応する `free` を明示。zip展開バッファの所有権を
  コメントで明記する
- **絶対パスのハードコード禁止**（`/mnt/mmc` 等）
- ログ: `LOG_INFO` / `LOG_WARN` / `LOG_ERR` マクロを用意し、stderr に出す
- 依存追加は事前に相談すること（バイナリサイズと実機の glibc 互換性に直結するため）。
  ただし **shellcheck のような開発ツールはこの制限の対象外**（成果物に入らないため）。
  無い環境でもテストは通ること
- シェルスクリプトは **POSIX sh**。`shellcheck -s sh -S warning` が通ること
  （`mux_launch.sh` は実機の busybox ash で動くので bashism = SC3xxx は致命的）
- シェルスクリプトを追加したら `tests/test_package.sh` の `SHELL_SCRIPTS` に
  必ず加える（`sh -n` と shellcheck の対象がそこで一元管理されている）
- **main へは直接コミット・push しない。** ブランチを切って PR を出す
  （P13。`git config core.hooksPath .githooks` で `.githooks/pre-push` を
  有効にしておくこと）

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
- [ ] zip内に `.m3u` が複数ある場合、最初の1つだけでなく**全て**処理して
      マージしたか（曲ごとに1ファイルの配布形式が実在する。5.3参照）
- [ ] submodule の gitlink が**公開リモートから取得できる**コミットを
      指しているか（P13 で実際に壊れていた。upstream に無い独自パッチを
      当てたまま upstream の url を指していると、開発機以外では
      `git clone --recurse-submodules` が失敗し CI が一切動かない。
      `scripts/release.sh` がリリース前にこれを検査する）
- [ ] UBSan を掛けたビルドで `-fno-sanitize-recover=all` を付けたか
      （既定では診断を出しても終了コードが 0 のままで、テストが偽の緑になる）
- [ ] シェルスクリプトを追加したとき `tests/test_package.sh` の
      `SHELL_SCRIPTS` に加えたか
- [ ] `SDL_GetPowerInfo()` を毎フレーム呼んでいないか（Linuxバックエンドは
      `/sys/class/power_supply` を毎回読み直すため、`battery_poll()` の
      throttle（`BATTERY_POLL_INTERVAL_MS`）を経由すること。F-26, Issue #7）

### public 化するときの TODO

現在このリポジトリは private。public にする場合は追加で以下が要る。

- [ ] `LICENSE` を置く。`.muxapp` は libgme（LGPL-2.1）を静的リンクして
      いるので、再リンク可能な形の提供かライセンス選択で条件を満たすこと
- [ ] libgme の独自パッチは既に public フォーク
      （ka-zuu/game-music-emu の `mugbs` ブランチ）にあるので、
      リリース本文からそこへリンクする
- [ ] branch protection / ruleset を有効にする（public なら無料で使える。
      private + 無料プランでは API が 403 を返すため `.githooks/pre-push`
      で代用している）
- [ ] README に CI バッジを付ける（private では未認証だと表示されない）

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


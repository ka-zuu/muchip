# SPEC.md — muChip: muOS向け chiptune (GBS/NSF/SPC) プレーヤー

> このドキュメントは muChip の仕様書です。実装は完了済み（v1.0.0〜）。
> 「なぜこの仕様になっているか」は [`docs/design-notes.md`](./docs/design-notes.md)、
> 作業規約は [`CLAUDE.md`](./CLAUDE.md) を参照してください。

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
- GBS/NSF/SPC 以外の形式の**積極的な**サポート（ただし libgme が対応する
  VGM 等は「たまたま動く」状態で構わない。UI上で排除しない）。
  `.nsf`/`.nsfe`（NSF, Nintendo Sound Format）と `.spc`（SPC, SNES SPC700
  Sound File）は GBS と同格の一級市民として正式サポートする（F-27, F-32,
  Issue #2, Issue #43）。GBS 用に組んだ「単体ファイル＋同名サイドカーm3u／
  m3u直接／zip」の枠組みがそのまま形式非依存で動くこと、および libgme の
  NSF/NSFE/SPC デコーダが元々静的リンクされていたことから、対応コストが
  低い（詳細は `docs/design-notes.md`「libgmeフォーク運用」参照）
- `.rsn`（RAR書庫のSPCアルバム）対応。中身の展開にminizでは扱えないRARが
  必要で、unrar等の新規依存はバイナリサイズと実機のglibc互換性に直結する
  （CLAUDE.md「事前相談なしの依存追加禁止」）。是非は別Issueで扱う
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
| F-08 | 曲長が不明な場合、設定した既定時間（デフォルト180秒 = 3分。Settings画面では分単位で編集する。Issue #16）で次へ進む（リピートが `one` のときはF-07と同様エンドレス）。`Length`（F-28）が `auto` 以外のときはこのフォールバック自体が使われない（F-28が優先する。判断材料が無いトラックなのでF-28は常にこのケースを上書きする。Issue #24） |

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
| F-28 | ながさチェンジ: Settings画面の `Length` を `auto` 以外（5〜30分・5分刻み）にすると、原則として全トラックの曲長をその値へ強制する（Issue #19）。ただし Issue #24: 曲長が既知（m3uの曲長欄や実測値がある）なのにループ構造を持たない（=延長しても実際には鳴り続けず、libgmeの無音自動終了で早く終わってしまう）トラックは、`min(Length指定値, 実測値)` にとどめる（延長はしないが、実測値の方が長い場合の短縮方向は効く）。曲長不明のトラック（判断材料が無い）とループ構造を持つトラック（延長しても実際に鳴り続けられる）は、既知/不明を問わず従来どおり強制する。`auto`（既定）では従来どおりF-07/F-08の判定に戻る。`Repeat: one`（F-07のエンドレス化）はF-28より優先する。変更はいま鳴っている曲へ即座に反映される |
| F-29 | 短い曲のスキップ: Settings画面の `Skip short` を `off` 以外（0〜30秒・1秒刻み）にすると、実測曲長がその秒数以下のトラックをTrackList一覧・再生順の両方から隠す（Issue #21）。判定は常に実測長で行い、`Length`（F-28）による見かけの曲長上書きの影響は受けない。曲長不明のトラック（F-08のフォールバック対象）は対象外。いま再生中のトラックはしきい値変更で消えない。全トラックが対象になる場合はフィルタ自体を無視して全曲を表示する。変更はTrackList・再生順へ即座に反映される |
| F-30 | メタデータ（曲名・ゲーム名・作者・著作権）がShift_JIS(CP932)で書かれたNSF/GBS/SPC/M3Uでも日本語を正しく表示できる（Issue #29。SPCのID666タグはIssue #43でF-32と同時に対応）。取り込み時に文字コードを自動判定してUTF-8へ正規化し（判定順はASCII→UTF-8→CP932→'?'フォールバック。5.5節）、美咲フォント（8x8、JIS第1・第2水準相当）で描画する。判定は自動のみで、明示的な文字コード指定の設定項目は無い |
| F-31 | カラーテーマ: Settings画面の `Theme` で5つのプリセット（`midnight`〈既定〉/`gameboy`/`mono`/`amber`/`synthwave`。すべてダーク系。Issue #27）と `custom` を切り替えられる。画面の色は9つの意味的スロット（背景・パネル・本文・副文・アクセント・選択行・再生中マーク・警告・充電中）から導出され、波形背景やシークバー背景等はそこからの計算値（6.4節）。`custom` は `Theme` の次の行 `Edit theme` から開く専用サブ画面（Theme Editor）で9色を個別編集できる。編集は即座に4画面へライブプレビューされる |
| F-32 | `.spc`（SPC, SNES SPC700 Sound File）を `.gbs`/`.nsf` と同格に扱える。単体ファイル＋同名サイドカーm3u・m3u直接・zip同梱のいずれの経路も共通（Issue #43で実装）。以下の3点でGBS/NSFと挙動が異なる: (1) 1ファイル=1トラック固定（libgmeの`gme_spc_type_`がfixed_track_count=1を宣言するため、フォルダ内の複数`.spc`をまたぐ自動連続再生は行わない。アルバムはzip/m3uでまとめる運用に委ねる）。(2) 10進の拡張M3Uトラック番号はNSFと同じく1始まり（5.2節参照。追加パッチ不要）。(3) ID666タグに曲長（秒）を持つため`length_known`が立ち、F-28（ながさチェンジ）・F-29（Skip short）がGBS/NSFの素のヘッダ（曲長情報を一切持たない）と異なる経路を通る。加えて、libgmeの実装上（`Spc_Emu`が`Classic_Emu`を継承しないため）EQ（F-20）とステレオ深度（F-21）がSPC再生中は一切効かない。値の編集自体は禁止しないが、Settings画面は該当行をグレーアウト（`THEME_ROLE_DIM`）して示す（選択中も維持する。6.1節） |

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
> `docs/design-notes.md`「libgmeフォーク運用」を参照。
>
> 一方、実機で使う **NSF用**m3u・NSFファイル自体のヘッダ(`first_song`)は
> いずれも10進のsong番号が1始まり(`NSF,1,...`が1曲目)であり、これは
> upstreamのデフォルト動作(1始まりとみなして-1する)とそのまま一致する。
> そのため `gme_nsf_type_`(`vendor/game-music-emu/gme/Nsf_Emu.cpp`)には
> GBSのような `flags_` パッチを**当てていない**。GBS用の0始まりパッチを
> 誤ってNSFにも適用すると、逆に全曲が1つズレる新たな不具合になるので
> 注意（Issue #2で確認済み。`tests/test_playlist.c` の
> `test_nsf_sidecar_m3u_is_one_based()` 参照）。
>
> **SPCも10進トラック番号は1始まり**（NSFと同じ、Issue #43）:
> `gme_spc_type_`(`vendor/game-music-emu/gme/Spc_Emu.cpp`)も `flags_==0` の
> ままで、upstreamのデフォルト動作(1始まりとみなして-1する)がそのまま
> 適用される。SPCは1ファイル=1トラック固定なので実用上m3uで意味を持つのは
> `SPC,1,...`のみだが、GBSのようなフォークパッチは不要（
> `tests/test_playlist.c` の `test_spc_sidecar_m3u_is_one_based()` 参照）。

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

### 5.5 メタデータの文字コード正規化（F-30, Issue #29）

`gme_track_info()` が返す `game`/`song`/`author`/`copyright`（5.1節）は、
NESM(NSF)仕様上ASCII前提だが、実在のファイルでShift_JIS(CP932)の日本語が
そのまま書き込まれている例が確認された。muChip内部の文字列表現は常に
UTF-8で統一しているため、`playlist.c` が上記フィールドを複製する際、
`text.c`（`text_dup_utf8()`）を通してUTF-8へ正規化してから保持する。
判定順序は固定:

1. 全バイトASCII → そのまま
2. 厳密に妥当なUTF-8 → そのまま（CP932より先に試す。日本語UTF-8のバイト列は
   CP932としても妥当になり得るため、順序を逆にすると誤判定する）
3. 厳密に妥当なCP932 → UTF-8へ変換
4. どれでもない → ASCIIバイトは通し、それ以外は `?` 1文字に丸める

拡張M3U（5.2節）の曲名も `gme_load_m3u_data()` の結果が同じ `info->song`
経路を通るため、追加コードなしでこの正規化の対象になる。

描画側（6.2節）は美咲フォント（8x8、JIS第1・第2水準相当。フリー
ソフトウェア）を非ASCII用に追加で持ち、UTF-8化されたメタデータの
日本語を表示できる。Browserのファイル名一覧・元ファイル自体のヘッダは
対象外（ファイルアクセスに使う文字列を書き換えると開けなくなるため）。

---

## 6. UI仕様

### 6.1 画面

| 画面 | 内容 |
|---|---|
| **Browser** | ファイル一覧。ディレクトリ階層を辿る。`.gbs` `.gb` `.nsf` `.nsfe` `.spc` `.m3u` `.zip` のみ表示（設定で全表示可。`.nsf`/`.nsfe`はIssue #2、`.spc`はIssue #43で追加） |
| **Player** | 曲名（見切れる場合、`[ui] title_scroll` が既定onなら横スクロール表示。Issue #8）・ゲーム名・作者・著作権・トラック `n/N`・経過/全体時間とシークバー（同一行。リピートが `one` でフェード無効(エンドレス)のときは全体時間を `--:--` にしシークバーを描かない。Issue #15）・**現在のファイルが属するディレクトリのファイル一覧（中央。Browserと同じ拡張子フィルタ。ディレクトリは出さない。カーソルを青、再生中のファイルを黄でハイライト）**・波形ビジュアライザ（下部） |
| **TrackList** | 現在のファイルの全トラック一覧。直接ジャンプ可能 |
| **Settings** | Length（先頭。ながさチェンジ。auto/5〜30分・5分刻み。F-28, Issue #19）・デフォルト曲長（分単位・1分刻み。Issue #16）・Skip short（短い曲のスキップ。off/0〜30秒・1秒刻み。F-29, Issue #21）・リピート・シャッフル（F-25, P10）・ステレオ深度・EQ・Fade・Show all files・Scroll title（Issue #8）・Show battery（F-26, Issue #7）・Theme・Edit theme（末尾2つ。カラーテーマ。F-31, Issue #27）。`X`で全項目を既定値に戻す確認ダイアログを開ける（P10）。現在再生中のソースがSPCのときは、libgmeの実装上効かないステレオ深度・EQ bass・EQ trebleの3行をグレーアウトする（カーソルを合わせた選択中も維持する。F-32, Issue #43。値の編集自体は禁止しない） |
| **Theme Editor** | `Settings`の`Edit theme`（`A`）で開くサブ画面。9つの色スロット（背景・パネル・本文・副文・アクセント・選択行・再生中マーク・警告・充電中）をラベル・R/G/B値・色見本の一覧で並べる。カーソル行のR/G/Bのうち選択中のチャンネルにマーカー（`>`）が付く。編集した値は即座に4画面すべてへライブプレビューされる（F-31, Issue #27） |

> **ヘッダ／フッタの文字階層（Issue #41）**: Browser・TrackList・Settings・
> Theme Editorの4画面は、ヘッダをタイトル（大）＋サブタイトル・右端カウンタ
> （小、同じ行）の2段組で描く。タイトルは画面の主題（Browserはフォルダ名、
> TrackListはゲーム名、Settingsは`Settings`、Theme Editorは`Edit theme`）、
> サブタイトルは補足（フルパス・バージョン・`custom palette`）、カウンタは
> `選択位置 / 総数`。フッタは操作ヒントを2行で描く（1行目=主要操作・
> 明るい文字色、2行目=補助操作・アクセント色）。Playerはヘッダ帯を持たない
> （曲名をタイトルサイズでそのまま出すため）が、フッタは同じ2行構成で
> `Start+Select:Quit`と再生中ファイルのパスを2行目に出す。リスト系画面の
> 選択行は、塗りつぶしに加えて左端へアクセント色の縦バーを添える。

> **バッテリー残量表示（F-26, Issue #7）**: 4画面すべてのタイトル行右端に
> 残量ゲージ（矩形の枠＋残量ぶんの塗り。8x8フォントはASCIIのみで絵文字が
> 無いため数値は出さない）を表示できる。`[ui] battery_show` で
> `off`/`low`/`always` を選ぶ（既定 `low`＝残量が少ないときだけ）。色は
> 通常=グレー、残量が少ない=赤、充電中=緑。Playerでは曲名の横スクロール
> （Issue #8）の可用幅がゲージ分だけ縮む。読み取れない値（デスクトップ等
> バッテリーの無い環境）は常に非表示。

> チャンネルミュート(F-10)はユーザー判断により削除済み。
> 音量調整も同様の理由（本体ハードウェア音量と非連動で紛らわしい）で
> 廃止し、常に最大出力で `gme_play()` する（詳細は
> `docs/design-notes.md`「削除した機能とその理由」）。

### 6.2 レイアウト規則（解像度非依存）

- 起動時に `SDL_GetCurrentDisplayMode()` で解像度を取得
- 640x480 を基準とし、`scale = min(w/640.0, h/480.0)` でスケーリング
- フォントサイズ・余白・行高すべてを `scale` 倍する
- **座標のハードコード禁止**
- フォントは内蔵ビットマップ(8x8)を2枚のテクスチャアトラスへ展開して使う。
  ASCII(U+0000-U+007F)は `vendor/font8x8`、非ASCIIは美咲フォント
  (JIS第1・第2水準相当、Issue #29)。全角も1セル8px幅で描き、
  等幅前提のレイアウト計算(`ui_text_width()`等)を崩さない。どちらの
  アトラスにも無いコードポイントは `?` にフォールバックする(5.5節)
- バッテリーゲージ（F-26）のような右詰め要素は `screen_w` からの右詰めで
  幅を確保し、本文（パス・曲名等）の描画幅をその分だけ縮めてから描く
  （逆順にすると重なる）。ゲージのみで数値を出さないため、残量の桁数で
  可用幅が揺れる心配は無い
- ヘッダ／フッタ帯の高さ（Issue #41）は `line_h` からではなく、実際に
  載せる文字サイズ段階の合計から直接導出する（ヘッダ=タイトル文字高+
  サブ文字高+余白3個分、フッタ=補助文字高×2+余白3個分）。段組を増やす
  ときは、この導出も一緒に見直すこと

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
> Issue #27: `custom` テーマのパレット（`[theme]`）はSETTINGS[]には
> 無い値だが、`Theme` 自体がリセット対象である以上ここでも既定
> （`midnight`と同じ色）へ戻す。

> **Settings画面の `A`（Issue #27）**: `Edit theme` 行のように値を持たない
> 項目（SET_ACTION）では、`A` は値を変えず Theme Editor サブ画面を開く
> （それ以外の行では従来どおり `RIGHT` と同じ）。

> **Theme Editor画面の入力（Issue #27）**: `UP`/`DOWN` でスロット行を移動
> （端で折り返す。他のリスト系画面と同じ規約）。`L1`/`R1` で編集する
> チャンネル（R→G→B、またはその逆）を切り替える。`LEFT`/`RIGHT` で選択中
> チャンネルの値を8刻みで増減する（255は33段目として個別に扱う。値が
> 梯子に乗っていない場合はまず動く方向側へ吸着する）。値を1回でも変えると
> `Theme` は自動的に `custom` へ切り替わる。`X` は確認ダイアログ無しで
> 入室時の状態（`Theme`と`[theme]`の両方）へ戻す（パレットは1枚だけなので
> これがそのままundoとして機能する）。`B`/`START` でSettings画面へ戻り、
> ここで保存する。

### 6.4 カラーテーマ（F-31, Issue #27）

画面の色は9つの意味的スロットから成る：背景・パネル（ヘッダ/フッタ帯）・
本文・副文・アクセント（シークバー塗り・波形線）・選択行背景・再生中
マーク・警告（エラー・バッテリー低下・確認ダイアログ枠）・充電中。
波形背景・Player中央一覧の背景・シークバー背景・確認ダイアログ背景は
このスロットからの計算値であり、個別に編集はできない（導出規則は
`docs/design-notes.md`「カラーテーマ」参照）。

`Theme` は5つのプリセット（`midnight`〈既定〉/`gameboy`/`mono`/`amber`/
`synthwave`。実機の反射型/低輝度パネル向けにすべてダーク系）と `custom`
を循環選択する。`custom` のパレットは `Edit theme` サブ画面（Theme Editor）
から画面上で編集するか、`config.ini` の `[theme]` セクション（9キー、
`RRGGBB`の16進）を手編集することで変更できる。

---

## 7. 設定ファイル

`/run/muos/storage/application/muChip/config.ini`

```ini
[playback]
default_length_sec  = 180  ; 曲長不明時の再生秒数(Settings画面では分単位で編集する。Issue #16)
length_override_sec = 0    ; ながさチェンジ。0=auto、非0で全トラックの曲長を強制 (F-28, Issue #19)
skip_short_sec     = 0     ; 短い曲のスキップ。0=off、非0でこの秒数以下の実測曲長を隠す (F-29, Issue #21)
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
theme          = midnight ; midnight | gameboy | mono | amber | synthwave | custom (F-31, Issue #27)
last_path      = /mnt/mmc/MUSIC

; theme が "custom" のときだけ実効値になる9色 (RRGGBB、先頭#無し。F-31)
[theme]
bg     = 12121a
panel  = 1e1e2a
fg     = e6e6e6
dim    = 9696a0
accent = 78b4ff
sel    = 3c5aa0
mark   = ffd25a
warn   = ff785a
ok     = 78dc8c
```

> 音量調整機能は廃止した（常に最大出力。`docs/design-notes.md`参照）ため
> `[audio] volume` キーは存在しない。チャンネルミュート(F-10)も
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
target_link_options(muchip PRIVATE
    -static-libstdc++
    -static-libgcc
)
target_link_libraries(muchip PRIVATE gme_static SDL2 m)
```

libgme は `BUILD_SHARED_LIBS=OFF`, `ENABLE_UBSAN=OFF` でビルドし、
不要なエミュレータを削って軽量化してもよい（`USE_GME_GBS=ON` /
`USE_GME_NSF=ON` / `USE_GME_NSFE=ON` / `USE_GME_SPC=ON` は必須。
F-27, F-32, Issue #2, Issue #43）。

---

## 9. muOS パッケージング

### 9.1 ディレクトリ構成（`.muxapp` の中身）

`.muxapp` は拡張子を変えた **zip** であり、SDカードのルートを基準とした構造を持つ。

```
mnt/mmc/MUOS/application/muChip/
├── mux_launch.sh
├── mux_lang.ini
├── bin/
│   └── muchip              (実行ファイル / chmod +x)
├── lib/                   (静的リンクできなかった依存があれば)
├── assets/
│   └── font.ttf
├── glyph/
│   └── muchip.png
└── config.ini
```

### 9.2 `mux_launch.sh`

現行の muOS（JACARANDA / ANDROMEDA）の作法に厳密に従う。

```sh
#!/bin/sh

# HELP: A chiptune (GBS/NSF) music player with full sub-track and M3U support.
# ICON: muchip
# GRID: muChip

. /opt/muos/script/var/func.sh

APP_BIN="muchip"
SETUP_APP "$APP_BIN" "retro"

# -----------------------------------------------------------------------------

APP_DIR="/run/muos/storage/application/muChip"

cd "$APP_DIR" || exit 1

export LD_LIBRARY_PATH="$APP_DIR/lib:$LD_LIBRARY_PATH"

./bin/muchip > "$APP_DIR/log.txt" 2>&1
```

**厳守事項:**

- `. /opt/muos/script/var/func.sh` の行は**絶対に削除しない**
  （CPUガバナ設定・SDL環境変数・HOME設定・SD1/SD2判定を行っている）
- `SETUP_APP "$APP_BIN" ""` を呼ぶ。第2引数は `"modern"` / `"retro"` /
  空文字（ボタンレイアウトの強制指定）
- **`/mnt/mmc` や `/mnt/sdcard` をハードコードしない。**
  必ず bind mount された `/run/muos/storage/application/muChip` を使う。
  ハードコードすると SD2 搭載機やストレージ構成の異なる機種で壊れる
- 標準出力を `log.txt` に落としておくとデバッグが劇的に楽になる

> **バッテリー低下しきい値の探索（F-26, Issue #7）**: `mux_launch.sh` は
> `GET_VAR` でしきい値の候補キーを順に試し、`1..99` の整数が返った
> 最初のものを `MUCHIP_BATTERY_LOW_PCT` として export する
> （`packaging/muChip/mux_launch.sh` 参照。絶対パスは書かず `GET_VAR` 経由
> なのでSPEC 12/13に反しない）。候補キーはどれが実際に生きているか
> 実機でしか分からないため推測で、全候補の結果を `log.txt` に残す。
> 何も見つからなければ export されず、`src/battery.c` の
> `battery_low_threshold_from_env(NULL)` が既定の10%を返す。

### 9.3 `mux_lang.ini`

```ini
[full]
English=muChip Player
Japanese=muChip プレーヤー

[grid]
English=muChip
Japanese=muChip

[help]
English=A chiptune (GBS/NSF) player with full sub-track and extended M3U support.
Japanese=GBS/NSF対応、サブトラックと拡張M3Uに正しく対応したchiptuneプレーヤーです。
```

### 9.4 パッケージ生成スクリプト

`scripts/package.sh` を用意し、`build/` の成果物から
`muChip-<version>.muxapp` を生成する。

```sh
cd package_root && zip -r ../muChip-1.0.0.muxapp . -x '.*' -x '__MACOSX/*'
```

インストールは実機の `Applications > Archive Manager` から行う。

### 9.5 リリース（P13）

- タグは `vX.Y.Z`。版番号の唯一の情報源は `CMakeLists.txt` の
  `project(muchip VERSION ...)`
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
| T-16 | ながさチェンジ（F-28, Issue #19/#24） | Settingsで`Length`を`auto`以外にすると、曲長不明のトラックとループ構造を持つトラックは指定値まで曲長が延びる。m3uの曲長・実測値はあるがループ構造を持たないトラックは、指定値より短ければそのまま(延長されない)、長ければ指定値でキャップされる。`auto`へ戻すと元の値(m3u/実測)へ復元される。変更は再生中の曲へ即時反映される |
| T-17 | 短い曲のスキップ（F-29, Issue #21） | Settingsで`Skip short`を`off`以外にすると、実測曲長がその秒数以下のトラックがTrackList一覧・再生順から消え、曲数(`Tracks (N)`)も減る。曲長不明のトラックは消えない。`Length`で上書き中でも実測長で判定される。いま再生中のトラックはしきい値変更で消えない。`off`へ戻すと消えたトラックが復活する |
| T-18 | カラーテーマ（F-31, Issue #27） | Settingsの`Theme`で5プリセット+`custom`を循環すると4画面すべての配色が切り替わり、`[ui] theme`へ保存・復元される。`Edit theme`(Theme Editorサブ画面)で9スロットをL1/R1でチャンネル切替・LEFT/RIGHTで値変更すると即座にライブプレビューされ、初回編集で`Theme`が自動的に`custom`へ切り替わる。`X`は入室時の状態(`Theme`と`[theme]`の両方)へ確認ダイアログ無しで戻す。`config.ini`の`[theme]`セクション(9キー、RRGGBB)を手編集しても反映される。Settings画面の`X`での全リセットは`Theme`を`midnight`へ、`[theme]`の9色もmidnightと同じ値へ戻す |
| T-19 | m3u なしの単体 `.spc` を開く（F-32, Issue #43） | 1トラックだけが `Track 01` で列挙される（SPCは1ファイル=1トラック固定） |
| T-20 | 同名 `.m3u` がある `.spc` を開く（F-32, Issue #43） | 曲名がm3u通りに反映される。10進トラック番号は1始まり（NSFと同じ。5.2節参照） |
| T-21 | SPC再生中に Settings 画面を開く（F-32, Issue #43） | `Stereo depth`・`EQ bass`・`EQ treble` の3行がグレーアウトされる（選択中も維持）。値そのものはLEFT/RIGHTで編集できる。GBS/NSF再生中・停止中はグレーアウトされない |
| T-22 | ID666タグに曲長を持つ `.spc`（F-32, Issue #43） | GBS/NSFの素のヘッダと異なり `length_known` が立ち、F-28（ながさチェンジ）・F-29（Skip short）の判定対象になる |

### 10.3 テスト用素材

著作権上の理由からリポジトリに `.gbs`/`.nsf`/`.spc` は含めない。
`tests/fixtures/` には **合成した最小GBS/NSF/SPCファイル**（ヘッダのみ有効な
擬似ファイル。SPCは加えてDSPレジスタ初期値とBRRサンプル1ブロックを持つ）と、
各種パターンの `.m3u` テキストのみを置く。m3uパーサのユニットテストはこれで行う。

### 10.4 CI（GitHub Actions、P13）

`.github/workflows/ci.yml` が PR と main への push で次を回す。

| ジョブ | 内容 |
|---|---|
| ホストビルド + CTest | `scripts/build-host.sh` → `ctest`。`MUCHIP_REQUIRE_SHELLCHECK=1` を立て、SKIP が1件でもあれば失敗させる |
| ASan/UBSan | `-fsanitize=address,undefined -fno-sanitize-recover=all`、`ASAN_OPTIONS=detect_leaks=1` |

- ヘッドレスUIスモークは `SDL_VIDEODRIVER=dummy` / `SDL_AUDIODRIVER=dummy`
  で走るのでランナーに X も音声デバイスも要らない
- **CI でクロスビルドはしない**（9.5 参照）。実機検証は人手で行い、結果は
  PR 本文に記録する
- シェルスクリプトの静的解析は専用ジョブを作らず `tests/test_package.sh`
  に集約する（検査対象リストを二重管理しないため）
- `-fno-sanitize-recover=all` は必須。これが無いと UBSan は診断を出すだけで
  終了コードが 0 のままになり、CTest が未定義動作を見逃して緑になる

---

## 11. 実装フェーズ

実装は完了済み（P0〜P13、v1.0.0〜v1.7.0）。当時のフェーズ計画・進捗は
[`docs/history/plan-archive.md`](./docs/history/plan-archive.md)（凍結・
参考資料）を参照。

---

## 12. コーディング規約

[`CLAUDE.md`](./CLAUDE.md)「コーディング規約」節へ移設した。

---

## 13. 既知の落とし穴チェックリスト

[`CLAUDE.md`](./CLAUDE.md)「既知の落とし穴チェックリスト」節へ移設した。

---

## 14. 参考

- game-music-emu: https://github.com/libgme/game-music-emu
- muOS Application Runner: https://community.muos.dev/t/application-runner/1282
- miniz: https://github.com/richgel999/miniz
- GBS形式仕様: https://www.tauwasser.eu/wiki/GBS
- SPC/ID666形式仕様: https://ocremix.org/info/SPC_and_RSN_File_Format_Specification


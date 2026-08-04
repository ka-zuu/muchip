# PLAN.md — muGBS 実装計画

このドキュメントは `SPEC.md` に基づく実装計画と進捗を記録する。
詳細な設計判断・SPECとの乖離点は各フェーズのコミットログおよび
`docs/` (追加され次第) を参照。

現在のスコープ: **P0〜P4（コア再生エンジン）**。
UI (P5)・入力抽象化と設定ファイル (P6)・クロスコンパイルとmuxappパッケージング (P7)・
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
- [ ] **P3** — m3u対応（`gme_load_m3u` + 自前プリパーサ）
      完了条件: T-02〜T-05
- [ ] **P4** — zip対応（miniz）
      完了条件: T-06, T-07
- [ ] P5 — UI（Browser / Player / TrackList）… 別プランで着手
- [ ] P6 — 入力抽象化、解像度非依存化、設定ファイル … 別プランで着手
- [ ] P7 — クロスコンパイル、muxappパッケージング … 別プランで着手
- [ ] P8 — チャンネルミュート、ビジュアライザ、EQ … 別プランで着手

## m3u の設計方針（SPEC 5.2 の解釈）

SPEC 5.2 は「参照ファイルが複数なら自前でプレイリストエントリを構築する」としているが、
素直に実装するとトラック番号（10進/16進）・時間パースを自前で再実装することになり、
同節の「自前パーサで再実装しないこと」という原則と衝突する。

そこで `m3u.c` は **m3u をファイル参照ごとの連続区間（セグメント）に分割し、
区間ごとに m3u テキストを再構成して `gme_load_m3u_data()` に投げ直す**という
薄い前処理に徹する。トラック番号解釈・曲名・曲長の抽出は常に libgme に委譲される。
参照ファイルが1種類だけの場合はセグメントが1つになるため、特別扱いの分岐は不要。

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
```

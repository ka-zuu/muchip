# 変更履歴

muGBS のユーザー向け変更履歴。実装の設計判断・実機検証の詳細は
[`PLAN.md`](./PLAN.md)、仕様は [`SPEC.md`](./SPEC.md) を参照。

## 書式について

見出しは `## vX.Y.Z - YYYY-MM-DD` の1行固定。`scripts/release.sh` が
この見出しの次の行から次の `## ` の直前までを切り出して GitHub Release の
本文にする（`./scripts/release.sh --print-notes` で確認できる）。
`.github/workflows/release-guard.yml` も同じ経路を通して「打たれたタグに
対応する節が存在し、中身が空でないこと」を検査する。書式を崩すと
リリースが作れなくなる（`CMakeLists.txt` の `project(...)` を1行で書く
制約と同じ思想）。

未リリースの変更は `## Unreleased` 節に書き溜め、リリース時に
`## vX.Y.Z - YYYY-MM-DD` へ書き換える。`CMakeLists.txt` の
`project(mugbs VERSION ...)` の更新と同じコミットで行うこと。

## v1.0.1 - 2026-08-07

### 画面

- Player 画面の文字を大きくした（Issue #3）。再生位置（`0:00 / 2:38`）は
  曲名と同じ大きさに、トラック番号・作者／著作権表示・再生状態
  （`PLAYING repeat:… shuffle:…`）・一時的なメッセージはリストの行と同じ
  大きさになった。640x480 で最小の 8px 表示に載っていて読みにくかったのを
  解消したもの。フッタの操作ヒントは従来どおり小さいまま
- あわせて Player 画面のプログレスバーを少し太くし、フッタの文字の
  縦位置のずれを直した。ファイル一覧の行数と波形ビジュアライザの
  大きさは変わらない

### 開発基盤

- GitHub Actions の CI を追加（PR と master への push でホストビルド・
  CTest・ASan/UBSan・シェルスクリプトの静的解析）
- `scripts/release.sh` を追加。バージョン整合性・テスト・クロスビルド・
  `.muxapp` 生成・タグ・GitHub Release の下書き作成を1本にまとめた
- master への直接 push を止める `.githooks/pre-push` を追加
- `vendor/game-music-emu` の参照先を、独自パッチを載せた public フォークへ
  変更（従来は upstream に存在しないコミットを指しており、開発機以外では
  `git clone --recurse-submodules` が失敗していた）

## v1.0.0 - 2026-08-06

muOS 実機で動作確認済みの最初のリリース。

### 再生

- `.gbs` の再生と、内部の全サブトラックの列挙・選択（F-01 / F-02）
- 拡張 `.m3u` の曲名・トラック番号・演奏時間・ループ・イントロ指定の反映（F-03）
- `.zip` の中の `.gbs` / `.m3u` を展開せずメモリ上で扱う（F-04）。
  zip 内に `.m3u` が複数ある配布形式もマージして全曲を扱える
- 10進のトラック番号を持つ `.m3u` を 0 始まりとして解釈する
  （同梱 libgme へのパッチ。zophar.net 配布の GBS パックで「2曲目を選ぶと
  1曲目が鳴る」ずれが起きていた）
- 再生 / 一時停止 / 次・前トラック / シーク（F-06）
- 曲長が判明していれば終端でフェードアウトして自動的に次へ（F-07）。
  不明なら既定秒数（初期値 150 秒）で次へ（F-08）
- リピート（なし / 1曲 / 全曲）とシャッフル再生（F-11 / F-25）

### 画面

- Browser / Player / TrackList / Settings の4画面（F-05 / F-12）
- 解像度非依存レイアウト（320x240 〜 1280x720 で検証。640x480 / 720x720 /
  1024x768 など機種ごとの解像度をハードコードしない）
- 波形ビジュアライザ（F-14）
- Player 画面で `Y` + 十字キーによるリピート／シャッフルの直接切り替え

### 音質・設定・入力

- イコライザ bass / treble（F-20）
- GB APU 4ch（Pulse1 / Pulse2 / Wave / Noise）の個別ミュート（F-10）
- `config.ini` の読み書き。終了時オートセーブと直近に開いた場所の復元（F-13）
- 入力は muOS の `gamecontrollerdb.txt` に委譲し、ボタン番号を決め打ちしない
- 終了は GUIDE ボタン単体、または Start + Select 同時押し

### パッケージング

- muOS の Archive Manager からインストールできる `.muxapp` を生成する
  `scripts/package.sh`
- 起動時に SD カード上の音楽ディレクトリ（`MUSIC` / `Music` / `ROMS/GBS` 等）を
  自動検出する `mux_launch.sh`

### 既知の制限

- GBS 専用。ogg / mp3 等の一般的な音楽ファイルは再生できない
- ステレオ深度（F-21）、テンポ調整（F-22）、スリープタイマー（F-23）、
  画面消灯状態でのバックグラウンド再生（F-24）は未実装

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
- muxappパッケージング（`mux_launch.sh`, `lib/`構成, `.muxapp`化）
- 複数デバイス（RG35XX H/SP, RG40XX, RG CubeXX等）での動作差異
- `sysroot/` の内容の版管理方針（バイナリはgit管理しない。再現性は
  `fetch-sysroot.sh` の再実行に依存する）

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

### クロスビルド・実機検証（実証済みの手順）

```sh
# 1. 実機からsysrootを構成 (初回のみ。実機のIP/ユーザーに合わせる)
./scripts/fetch-sysroot.sh root@<実機のIP>

# 2. クロスビルド用Dockerイメージを作成 (sysroot/を上書き取り込みする)
docker build -f docker/Dockerfile -t mugbs-crossbuild .

# 3. クロスビルド
docker run --rm -v "$(pwd):/work" -w /work mugbs-crossbuild bash -c '
  cmake -B build-aarch64 -DTARGET_HOST=OFF -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
  cmake --build build-aarch64 -j$(nproc)
'

# 4. 実機へ転送して実行 (mux_launch.sh経由でない場合はXDG_RUNTIME_DIRの手動exportが必要)
scp build-aarch64/mugbs root@<実機のIP>:/root/
ssh root@<実機のIP> 'export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; /root/mugbs --cli Game.gbs'
```

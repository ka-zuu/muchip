# muGBS

muOS 向けの GBS (Game Boy Sound System) プレーヤー。サブトラック構造と拡張M3U
（曲名・曲長・ループ指定）を正しく扱う。詳細仕様は [`SPEC.md`](./SPEC.md)、
実装進捗は [`PLAN.md`](./PLAN.md) を参照。

現在のスコープは **コア再生エンジン（P0〜P4）** で、SDL によるホスト上の
CLI ハーネスから動作確認できる。GUI（Browser/Player/TrackList）と実機向けの
クロスビルド・パッケージングは別プランで扱う。

## ビルド（ホスト / 開発機）

前提パッケージ（Ubuntu/Debian系）:

```sh
sudo apt update && sudo apt install -y \
    pkg-config libsdl2-dev libsdl2-ttf-dev cmake build-essential git
```

初回のみ submodule を取得:

```sh
git submodule update --init --recursive
```

ビルド:

```sh
./scripts/build-host.sh
# または直接:
cmake -B build -DTARGET_HOST=ON
cmake --build build -j
```

テスト:

```sh
ctest --test-dir build --output-on-failure
```

実行:

```sh
./build/mugbs --list Game.gbs      # プレイリストを列挙するだけ（無音）
./build/mugbs --cli Game.gbs       # 1トラック目を再生
```

## 実機（muOS）向けクロスビルドについて【実機で検証済み】

muOS 2601.0 (JACARANDA) 実機（Cortex-A53 aarch64）で、クロスビルドした
`mugbs` が実際に映像・音声とも正常動作することを確認済み。詳細な調査経緯は
[`PLAN.md`](./PLAN.md) の「P7準備メモ: SDL2の扱いに関する調査」節を参照。

muOS の SDL2 は独自のビデオドライバ（Allwinner H700系デバイス共通のMali
GPU直結フレームバッファドライバ `mali`）を内蔵しており、**Debian の
`libsdl2-dev:arm64` でビルドしたバイナリは実機で正しく動かない**
（依存する X11/Wayland/PulseAudio 等が実機に存在しないため、そもそも
ロードに失敗する）。

さらに実機の glibc (2.38) は Debian bullseye のクロスツールチェインが
持つ glibc (2.31) より新しく、素朴にリンクすると実機SDL2が要求する
新しいシンボルが解決できない。**Debian の `crossbuild-essential-arm64`
は `--sysroot` フラグを無視し、常に `/usr/aarch64-linux-gnu` を
sysrootとして使う**ため、`CMAKE_SYSROOT` の指定だけでは機能しない。

手順:

1. 実機からSSH/SCPで `sysroot/` を構成する（SDL2のバージョンを実機の
   `.so` から自動検出し、対応する upstream SDL2 のヘッダも取得する）
   ```sh
   ./scripts/fetch-sysroot.sh root@<実機のIP>
   ```
2. `docker/Dockerfile` がビルド時に `sysroot/` の内容を
   `/usr/aarch64-linux-gnu/{lib,include/SDL2}` へ上書きコピーする
   ```sh
   docker build -f docker/Dockerfile -t mugbs-crossbuild .
   ```
3. クロスビルド（`cmake/toolchain-aarch64.cmake` はコンパイラ指定のみ。
   SDL2は `/usr/aarch64-linux-gnu/include/SDL2` を直接参照する）
   ```sh
   docker run --rm -v "$(pwd):/work" -w /work mugbs-crossbuild bash -c '
     cmake -B build-aarch64 -DTARGET_HOST=OFF -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
     cmake --build build-aarch64 -j$(nproc)
   '
   ```
4. 実機へ転送して実行する。`mux_launch.sh` 経由なら `func.sh` が
   `XDG_RUNTIME_DIR`/`PIPEWIRE_RUNTIME_DIR` を自動でexportするが、
   SSH生シェルから直接実行する場合は手動でexportが必要
   （実機のオーディオはPipeWire経由のため）
   ```sh
   scp build-aarch64/mugbs root@<実機のIP>:/root/
   ssh root@<実機のIP> 'export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; /root/mugbs --cli Game.gbs'
   ```

`sysroot/` はバイナリを含むため git 管理しない（`.gitignore` 済み）。
再現性は `scripts/fetch-sysroot.sh` の再実行に依存する。

muxappパッケージング（`mux_launch.sh` の実配置、`.muxapp` 化）自体は
まだ未着手（P7本格着手時に対応）。

## ライセンス / 同梱ソースについて

- `vendor/game-music-emu`（libgme）: git submodule。LGPL/GPL（同梱の
  `license.txt` / `license.gpl2.txt` を参照）。GBS デコードと拡張M3U解析を
  委譲している。自前で GB APU は実装していない。
- `vendor/miniz`: MIT ライセンス。zip 展開に使用（P4 以降）。
  https://github.com/richgel999/miniz より split-file ソースを vendoring。

## 依存関係とアーキテクチャ上の注意

- `gme_play()` の第2引数は **ステレオインタリーブされた `short` の個数**
  であり、フレーム数でもバイト数でもない。SDL オーディオコールバックが
  渡す `len`（バイト数）は `len / sizeof(short)` で変換する。
  `len / 4` を渡すと倍速再生になる（`src/audio.c` 参照）。
- `gme_*` API はスレッドセーフでない。オーディオコールバックとメインスレッド
  の両方から `Music_Emu*` に触るため、`SDL_LockAudioDevice()` /
  `SDL_UnlockAudioDevice()` で保護する。

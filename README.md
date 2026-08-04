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

## 実機（muOS）向けクロスビルドについて【現時点では未検証】

SPEC.md 8.3 節が定める重要な注意点をここに転記する。**実装が進み P7
（クロスコンパイル・パッケージング）に着手する際は、必ずこの手順を実機で
再確認してから進めること。**

muOS の SDL2 はデバイス固有のバックエンド（KMS/DRM, fbdev, 回転処理）を
含むため、**Debian の `libsdl2-dev:arm64` でビルドしたバイナリは実機で
正しく動かない可能性が高い。**

推奨手順（P7 で実施予定）:

1. 実機に SSH で入り、以下を取得してホスト側 `sysroot/` に配置する
   ```
   /usr/lib/libSDL2-2.0.so*
   /usr/lib/libSDL2_ttf*.so*      (使う場合)
   /usr/include/SDL2/             (無ければ同バージョンのヘッダをGitHubから取得)
   ```
2. `cmake/toolchain-aarch64.cmake` の `CMAKE_SYSROOT` がこの `sysroot/` を
   指すようにする（既にそう書いてある）
3. リンクは動的（実機の SDL2 をそのまま使う）
4. `docker/Dockerfile` のクロスビルド環境で
   `cmake -B build-aarch64 -DTARGET_HOST=OFF -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake`
   してビルドし、実機で起動確認する

現時点（P0〜P4）ではこの経路は**まだ一度もビルドしていない**。
`docker/Dockerfile` と `cmake/toolchain-aarch64.cmake` は SPEC の骨子を
そのまま置いてあるだけの未検証状態である。

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
  `len / 4` を渡すと倍速再生になる（`src/audio.c` 参照、P1 で追加予定）。
- `gme_*` API はスレッドセーフでない。オーディオコールバックとメインスレッド
  の両方から `Music_Emu*` に触るため、`SDL_LockAudioDevice()` /
  `SDL_UnlockAudioDevice()` で保護する。

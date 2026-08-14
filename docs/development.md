# 開発ガイド

ビルド・クロスビルド・パッケージング・CI・リリース・PR運用の手順。
「なぜこの手順になっているか」は [`design-notes.md`](./design-notes.md)、
日々のルール・コーディング規約は [`CLAUDE.md`](../CLAUDE.md) を参照。
アプリの使い方は [`README.md`](../README.md) を参照。

## ホストビルド

前提パッケージ（Ubuntu/Debian系）:

```sh
sudo apt update && sudo apt install -y \
    pkg-config libsdl2-dev cmake build-essential git
```

`ctest` を完全な形で回すには追加で `zip` `unzip` `shellcheck` があるとよい
（無い場合、`.muxapp` の構造検証はスキップされ、シェルスクリプトの静的
解析は飛ばされる。CI は3つとも入れる）。

初回のみ submodule を取得:

```sh
git submodule update --init --recursive
```

ビルドとテスト:

```sh
./scripts/build-host.sh
ctest --test-dir build --output-on-failure
```

ASan/UBSan を掛けたい場合（CI が毎回回しているのと同じ内容）:

```sh
cmake -B build-asan -DTARGET_HOST=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure -E '^test_package$'
```

`-fno-sanitize-recover=all` は必ず付けること。無いと UBSan は診断を出す
だけで終了コードが 0 のままになり、CTest が未定義動作を見逃して緑になる。

レイアウトを目で見る（`--screenshot` は非公開の開発用オプション。ホストに
実機の `/dev/fb0` に当たるものが無いのでこれで代用する）:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 2 \
  ./build/muchip --window 320x240 Game.gbs --screenshot /tmp/shot.bmp
# --ui-script と組み合わせれば任意の画面まで進めてから撮れる
```

## 実機（muOS）向けクロスビルド

muOS の SDL2 は独自のビデオドライバ（Allwinner H700系デバイス共通の
Mali GPU直結フレームバッファドライバ `mali`）を内蔵しており、Debian の
`libsdl2-dev:arm64` でビルドしたバイナリは実機で動かない。詳細な理由は
[`design-notes.md`「クロスビルドとmuOSパッケージング」](./design-notes.md#クロスビルドと-muos-パッケージング)
を参照。

手順:

1. 実機からSSH/SCPで `sysroot/` を構成する（初回のみ。実機のSDL2や
   libcが変わったときだけ再実行する）
   ```sh
   ./scripts/fetch-sysroot.sh root@<実機のIP>
   ```
2. クロスビルドする
   ```sh
   ./scripts/build-aarch64.sh                  # -> build-aarch64/muchip
   ./scripts/build-aarch64.sh --rebuild-image  # sysroot/ を更新したとき
   ```
   Docker イメージが無ければ自動で
   `docker build -f docker/Dockerfile -t muchip-crossbuild .` を実行する。
   ビルド後に `file` と `readelf -d` の `NEEDED` を表示するので、
   libstdc++ が動的リンクに紛れ込む回帰（実機で
   `GLIBCXX_3.4.xx not found` になる）にすぐ気づける。期待する
   `NEEDED` は `libSDL2-2.0.so.0` / `libm.so.6` / `libc.so.6` の3つだけ
3. `.muxapp` を作る（strip・バージョン付けまで自動）
   ```sh
   ./scripts/package.sh    # -> ./muChip-<version>.muxapp
   ```
   バージョンは `CMakeLists.txt` の `project(muchip VERSION x.y.z ...)`
   が唯一の情報源（`./build/muchip --version` でも確認できる）。
   `scripts/package.sh --bin PATH` で任意のバイナリを、`--no-strip` で
   strip無し生成も指定できる（`ctest -R test_package` が構造検証に使う）。

`sysroot/` はバイナリを含むため git 管理しない（`.gitignore` 済み）。
再現性は `scripts/fetch-sysroot.sh` の再実行に依存する。

SDLウィンドウを開かないCLIハーネス（`--cli`）だけを単発で試したい場合は、
`mux_launch.sh` を経由せず直接転送して実行してもよい（実機のオーディオは
PipeWire経由のため `XDG_RUNTIME_DIR`/`PIPEWIRE_RUNTIME_DIR` の手動exportが
必要）:

```sh
scp build-aarch64/muchip root@<実機のIP>:/root/
ssh root@<実機のIP> 'export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; /root/muchip --cli Game.gbs'
```

## muOS へのインストールと実機検証

```sh
scp muChip-<version>.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
ssh root@<実機のIP> /opt/muos/script/mux/extract.sh /mnt/mmc/ARCHIVE/muChip-<version>.muxapp
```

これは実機の **Applications > Archive Manager** から展開するのと同じ
スクリプトを直接叩いている。アプリは
`/run/muos/storage/application/muChip/` に配置される。

**画面・挙動の確認はSSH越しに `bin/muchip` を直接実行する**
（`mux_launch.sh` をSSHから直接起動すると `muxfrontend` のフォアグラウンド
受け渡しを経由せず、終了後に画面が固まる既知の問題がある）:

```sh
ssh root@<実機のIP> 'export XDG_RUNTIME_DIR=/run PIPEWIRE_RUNTIME_DIR=/run; \
  /run/muos/storage/application/muChip/bin/muchip \
  --ui-script <script> --screenshot /tmp/shot.bmp'
scp root@<実機のIP>:/tmp/shot.bmp .    # BMP→PNG変換して確認
```

物理ボタン操作そのものが必要な検証（起動→操作→終了の一連の流れ、
GameControllerの押下感など）はユーザーに実機で操作してもらい、Claudeは
その間SSH側でスクリーンショット取得・ログ確認を並行する。muOSの内部
制御ファイル（`/tmp/app_go` 等）を外部から書き換えてUI遷移を代替
トリガーする手法は使わない（`frontend.sh` のループを壊し実機が
フリーズした前例がある）。

ログは `/run/muos/storage/application/muChip/log.txt` に出る。

## `.muxapp` を自分でビルドする

`packaging/muChip/` にアプリ本体以外の資材（`mux_launch.sh`・
`mux_lang.ini`・`config.ini`・アイコン）が入っている。
`scripts/package.sh` がこれとクロスビルド済みバイナリを合わせて
`.muxapp`（実体はzip）を作る。アイコン（`packaging/muChip/{glyph,grid}/
muchip.png`）は `tools/make_glyph.py`（Pillow使用）で生成したものを
コミット済み。図案を変えたいときだけ再実行する。

## 開発フロー（PR ベース）

`main` へは直接コミット・push しない。ブランチを切って PR を出す。

```sh
git switch -c <ブランチ名>
# ... 作業 ...
git push -u origin <ブランチ名>
gh pr create --fill     # .github/pull_request_template.md が展開される
gh pr checks --watch    # CI が緑になるのを待つ
```

**クローンごとに1回、フックを有効にすること:**

```sh
git config core.hooksPath .githooks
```

`.githooks/pre-push` が `main` への直接 push を拒否する。`core.hooksPath` は
`.git/config` に入る設定なので、リポジトリに置いてあるだけでは有効にならない。

本来これは GitHub 側の branch protection / ruleset でやりたいところだが、
ruleset はサーバ側の設定でありクローン直後には効かないため、フックを
多重防御として併用している。フックはあくまで自衛で、`git push --no-verify`
や `MUCHIP_ALLOW_PUSH_MAIN=1` で抜けられる。本当の強制が要るときは
GitHub 側の branch protection / ruleset を有効にすること。

ドキュメントだけの変更で CI を回したくないときは、コミットメッセージに
`[skip ci]` を入れる。

## 機能追加のフロー（Issue駆動）

思いついたことは気軽に GitHub Issue へ。**テンプレートも必須項目もない**
（`.github/ISSUE_TEMPLATE/` は意図的に置いていない）。SPEC.md への影響
（新規 F-xx が要るか）や実機検証の要否を Issue 作成時点で書く必要は無い。

着手するときは、ターミナルで `claude` を起動して Issue を渡す
（例:「Issue #N をやって」）。SPEC/design-notes への影響判断・実機検証
要否・`CHANGELOG.md` への追記は、その場で Claude Code が Issue の内容を
読んで判断し、実装後の PR 本文（`.github/pull_request_template.md` の
各チェック項目）に自分で書き込む。

PR は `Closes #N` で対応する Issue を閉じるようにする。

## CI

`.github/workflows/ci.yml` が PR と `main` への push で2つのジョブを回す。

| ジョブ | 内容 |
|---|---|
| ホストビルド + CTest | `scripts/build-host.sh` → `ctest`。SKIP が1件でもあれば失敗 |
| ASan/UBSan | サニタイザ付きビルドで `test_package` 以外の全件 |

- ヘッドレスUIスモークは `SDL_VIDEODRIVER=dummy` / `SDL_AUDIODRIVER=dummy` で
  走るので、ランナーに X も音声デバイスも要らない
- CI は `MUCHIP_REQUIRE_SHELLCHECK=1` を立てる（「shellcheck が無いこと」
  自体を失敗にするフラグ。apt の書き忘れで静的解析が無言で消えるのを防ぐ）
- **実機（aarch64）向けのクロスビルドは CI ではしない。** `sysroot/` が
  実機から抜いたバイナリで、リポジトリに含めない方針のため。実機検証は
  人手で行い、結果は PR 本文に記録する

## リリース手順

`.muxapp` は必ず開発機で作る（CI では作れない）。
`scripts/release.sh` は `git` `docker` `zip` `unzip` `sha256sum` `cmake` `gh` に加えて
**`shellcheck` も必須**（リリース時のテストは静的解析込みで回すため）。
無ければ冒頭の前提チェックで止まる。

1. `CMakeLists.txt` の `project(muchip VERSION x.y.z ...)` を上げる
2. `CHANGELOG.md` の `## Unreleased` を `## vx.y.z - YYYY-MM-DD` に書き換える
   （1 と同じコミットで）
3. PR 経由で `main` へマージし、ローカルの `main` を最新にする
4. リハーサル → 本番

```sh
./scripts/release.sh --dry-run   # 検査とクロスビルドは実行し、変更操作はしない
./scripts/release.sh             # タグ + .muxapp を添付した下書きリリース
```

5. Release Guard ワークフロー（タグ・バージョン・CHANGELOG の整合性 +
   クリーンなチェックアウトでのフル CI）が緑になるのを確認する

```sh
gh run list --workflow=release-guard.yml --limit 1
```

6. 下書きを公開し、実機で最終確認する

```sh
gh release edit vx.y.z --draft=false
scp muChip-x.y.z.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
```

`scripts/release.sh` は取り返しのつかない操作（タグ作成・push・リリース作成）を
最後にまとめてあるので、途中で落ちてもリモートには何も残らない。
`--no-github` / `--keep-binary` / `--notes FILE` / `--publish` も参照
（`./scripts/release.sh --help` は無い。スクリプト冒頭のコメントに一覧がある）。

## 依存関係とアーキテクチャ上の注意

- `gme_play()` の第2引数は **ステレオインタリーブされた `short` の個数**
  であり、フレーム数でもバイト数でもない。SDL オーディオコールバックが
  渡す `len`（バイト数）は `len / sizeof(short)` で変換する。
  `len / 4` を渡すと倍速再生になる（`src/audio.c` 参照）。
- `gme_*` API はスレッドセーフでない。オーディオコールバックとメインスレッド
  の両方から `Music_Emu*` に触るため、`SDL_LockAudioDevice()` /
  `SDL_UnlockAudioDevice()` で保護する。
- 画面座標・文字サイズは `src/ui.c` の `ui_metrics_t`（`scale = min(w/640,
  h/480)` から導出）経由でのみ扱い、直接ハードコードしない。
- GameController のボタン判定は `SDL_CONTROLLER_BUTTON_*` の論理名のみを
  使い、生のボタン番号を決め打ちしない（`src/input.c`）。
- `config.ini` の設定は実行中プログラム全体で `main()` が持つ唯一の
  インスタンスだけが権威を持つ（`app_t`/`player_t` はポインタで参照する
  だけでコピーを持たない）。
- `strtod()`/`printf("%f")` はロケール依存のため、`config.c` は
  `setlocale()` を一切呼ばない前提（常に `"C"` ロケール）で
  `stereo_depth` 等の小数値を読み書きする。

これらの「なぜ」は [`design-notes.md`](./design-notes.md) を参照。

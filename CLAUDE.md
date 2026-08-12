# CLAUDE.md

muChip（muOS向け chiptune プレーヤー、GBS/NSF対応）のリポジトリで
作業するときの規約。C言語の型名・ヘッダガードは歴史的経緯で
`mugbs_*`/`MUGBS_*` のまま（Issue #13、[`docs/design-notes.md`](./docs/design-notes.md)
参照）。

## ドキュメントの役割分担

情報は書いた場所で腐る。追記する前に、まずどこに書くべきかをこの表で
決めること。

| 書く内容 | 置き場所 |
|---|---|
| 何のアプリか・使い方・インストール | [`README.md`](./README.md) |
| 仕様（F-xx要件・UI規則・config.iniキー・T-xxテストケース） | [`SPEC.md`](./SPEC.md) |
| ビルド・クロスビルド・リリース・CI・PR運用の手順 | [`docs/development.md`](./docs/development.md) |
| 「なぜこうなっているか」＝いま有効な設計判断 | [`docs/design-notes.md`](./docs/design-notes.md) |
| 利用者から見た変更 | `CHANGELOG.md` の `## Unreleased` |
| 作業の経緯・調査ログ・検証の実施記録 | **PR本文**（リポジトリの`.md`には書かない） |
| 過去の経緯（P0〜P13 / v1.0.0〜v1.7.0） | [`docs/history/plan-archive.md`](./docs/history/plan-archive.md)（**追記禁止**） |

`docs/design-notes.md` は**トピック別**（1トピック=1見出し）。新しい
判断が出たら該当トピックの記述を書き換えて統合する。日付やIssue番号での
時系列追記はしない。「検証した」「実機で確認した」という実施記録は
書かない（それはPR本文とCHANGELOG.mdの役割）。

## ビルドと検証

```sh
./scripts/build-host.sh
ctest --test-dir build --output-on-failure          # 全緑・SKIPなしが必須
```

ASan/UBSan（サニタイザ検証は毎回省略しないこと）:

```sh
cmake -B build-asan -DTARGET_HOST=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure -E '^test_package$'
```

`-fno-sanitize-recover=all` は必須。無いとUBSanは診断を出すだけで
終了コードが0のままになり、CTestが未定義動作を見逃して偽の緑になる。

画面に関わる変更は複数解像度で `--screenshot`（非公開オプション）を
確認する。詳細コマンドは [`docs/development.md`](./docs/development.md)。

## 実機検証

音声出力・入力・パッケージング・SDL2の使い方・レイアウトの実寸に
関わる変更は実機（muOS）検証が要る。純粋な backend/logic 変更で
見た目・挙動が変わらないなら不要と判断してよい。

```sh
./scripts/build-aarch64.sh && ./scripts/package.sh
scp muChip-<version>.muxapp root@<実機のIP>:/mnt/mmc/ARCHIVE/
ssh root@<実機のIP> /opt/muos/script/mux/extract.sh /mnt/mmc/ARCHIVE/muChip-<version>.muxapp
```

スクリーンショットはSSH越しに `bin/muchip --ui-script <script>
--screenshot <path>` を**直接**実行して撮る（`XDG_RUNTIME_DIR=/run
PIPEWIRE_RUNTIME_DIR=/run` を付与）。これは実際のディスプレイ/GPU/
オーディオデバイスを使う。**`mux_launch.sh` をSSHから直接起動しては
いけない**——`muxfrontend`のフォアグラウンド受け渡しを経由せず、
終了後に画面が固まる既知の問題がある。物理ボタン操作そのものが必要な
検証はユーザーに実機で操作してもらい、Claudeはその間SSH側でスクリー
ンショット取得・ログ確認を並行する。詳細は
[`docs/development.md`](./docs/development.md)。

## コーディング規約

- 言語: **C11**（libgme連携部のみC++でも可。極力Cで完結させる）
- 命名: `snake_case`。モジュール名をプレフィックスに（`player_next_track()`）
- エラー処理: 戻り値でエラーを返す。`assert` に頼らない
- **メモリ**: 全ての`malloc`に対応する`free`を明示。zip展開バッファの
  所有権をコメントで明記する
- **絶対パスのハードコード禁止**（`/mnt/mmc` 等）
- ログ: `LOG_INFO` / `LOG_WARN` / `LOG_ERR` マクロを使い、stderrへ出す
- 依存追加は事前に相談すること（バイナリサイズと実機のglibc互換性に
  直結するため）。ただし shellcheck のような開発ツールは対象外
  （成果物に入らないため。無い環境でもテストは通ること）
- シェルスクリプトは**POSIX sh**。`shellcheck -s sh -S warning`が通る
  こと（`mux_launch.sh`は実機のbusybox ashで動くのでbashism=SC3xxxは
  致命的）
- シェルスクリプトを追加したら `tests/test_package.sh` の
  `SHELL_SCRIPTS` に必ず加える

## 既知の落とし穴チェックリスト

- [ ] `gme_play()` の count はバイト数でもフレーム数でもなく**int16の
      個数**（偶数）
- [ ] `-static-libstdc++ -static-libgcc` を付けたか
- [ ] SDL2は**実機から抜いたもの**に対してリンクしたか
- [ ] `emu` へのアクセスを `SDL_LockAudioDevice()` で保護したか
- [ ] `gme_open_data()` に渡したバッファを `gme_delete()` 前に free
      していないか
- [ ] `gme_free_info()` を呼んでいるか（`gme_track_info` はヒープを返す）
- [ ] `mux_launch.sh` の `func.sh` 読み込みを消していないか
- [ ] `/mnt/mmc` をハードコードしていないか
- [ ] 実行ファイルに実行権限が付いた状態でzip化しているか
- [ ] 画面座標を640x480決め打ちしていないか
- [ ] ボタン番号を決め打ちしていないか
- [ ] zip内に`.m3u`が複数ある場合、最初の1つだけでなく**全て**処理して
      マージしたか
- [ ] submoduleのgitlinkが**公開リモートから取得できる**コミットを
      指しているか
- [ ] UBSanを掛けたビルドで`-fno-sanitize-recover=all`を付けたか
- [ ] シェルスクリプトを追加したとき`tests/test_package.sh`の
      `SHELL_SCRIPTS`に加えたか
- [ ] `SDL_GetPowerInfo()`を毎フレーム呼んでいないか（`battery_poll()`
      のthrottleを経由すること）
- [ ] ドキュメントにバージョン番号をハードコードしていないか
      （`<version>`プレースホルダか「`CMakeLists.txt`の`project()`が
      唯一の情報源」という記述にする）

## 変更を出す手順

```sh
git switch -c <ブランチ名>
# 実装 → 上記のビルド/検証 → 必要なら実機検証
git commit -m "Issue #NN: ..."
git push -u origin <ブランチ名>
gh pr create --fill        # Closes #N を本文に含める
gh pr checks --watch
```

マージはユーザーが行う（自動化しない）。マージ後はローカルを追随:

```sh
git fetch && git switch main && git pull
git branch -d <ブランチ名>
```

## やってはいけないこと

- `main` へ直push（`.githooks/pre-push`が拒否する。クローンごとに
  `git config core.hooksPath .githooks` が必要）
- 事前相談なしの依存追加
- `/mnt/mmc` 等の絶対パスをコードへハードコード
- ドキュメントへのバージョン番号のハードコード（ドリフトする）
- [`docs/history/plan-archive.md`](./docs/history/plan-archive.md) への追記
- [`docs/design-notes.md`](./docs/design-notes.md) への時系列追記
  （該当トピックへの統合に書き換える）
- `muOS`の内部制御ファイル（`/tmp/app_go`等）を外部から書き換えて
  UI遷移をトリガーすること（実機をフリーズさせた前例がある。物理操作は
  ユーザーに依頼する）

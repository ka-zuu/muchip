## 概要

<!-- 何を、なぜ。SPEC の要件番号 (F-xx) や PLAN のフェーズ (Pxx) があれば書く -->

## 変更点

-

## 検証

- [ ] `./scripts/build-host.sh` が通る
- [ ] `ctest --test-dir build --output-on-failure` が全緑（SKIP を含まない）
- [ ] ASan/UBSan ビルドでも緑（手順は `PLAN.md` の「検証手順」）
- [ ] 画面に関わる変更なら複数解像度でレイアウトを目視確認した
      （確認した解像度: ）

### 実機検証（muOS / aarch64）

いずれか1つにチェックする。

- [ ] 不要 — ホスト側だけで完結する変更（理由: ）
- [ ] 必要 — 実施済み（機種 / muOS バージョン / 確認した内容: ）
- [ ] 必要 — 未実施。マージ後に実施して `PLAN.md` へ結果を追記する

<!-- 実機検証が要るものの目安:
     音声出力、入力 (SDL GameController)、
     パッケージング (packaging/muGBS/mux_launch.sh, scripts/package.sh)、
     クロスビルド設定 (docker/, cmake/toolchain-aarch64.cmake)、
     SDL2 の使い方の変更、レイアウトの実寸に関わる変更 -->

## ドキュメント

- [ ] `SPEC.md` を更新した / 更新不要
- [ ] `PLAN.md` に設計判断と検証結果を記録した / 記録不要
- [ ] `README.md` を更新した / 更新不要
- [ ] `CHANGELOG.md` の `## Unreleased` に追記した / 追記不要
      （ユーザーから見た挙動が変わるなら必須）

## 依存関係

- [ ] 新しい外部依存を追加していない
      （SPEC 12: 依存追加は事前に相談。バイナリサイズと実機の glibc 互換性に直結する）
- [ ] シェルスクリプトを追加した場合、`tests/test_package.sh` の
      `SHELL_SCRIPTS` に加えた / 追加していない

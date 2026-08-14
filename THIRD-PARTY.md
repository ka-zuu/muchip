# サードパーティ・同梱ソースについて

muChip 自体のライセンスは [`LICENSE`](./LICENSE)（MIT）。このファイルは
muChip が同梱・リンクしているサードパーティ製ソフトウェアの一覧と、
それぞれのライセンス条件を満たすために必要な情報をまとめる。

## 一覧

| コンポーネント | 場所 | ライセンス | 形態 |
|---|---|---|---|
| game-music-emu (libgme) | `vendor/game-music-emu`（submodule） | LGPL-2.1-or-later | 静的リンク |
| miniz | `vendor/miniz/` | MIT | ソース同梱（vendoring） |
| font8x8 | `vendor/font8x8/font8x8_basic.h` | パブリックドメイン | ソース同梱（vendoring） |
| 美咲フォント（misaki） | `vendor/misaki/misaki_gothic.h` | フリーソフトウェア（無保証・改変可） | 生成ソース同梱（vendoring） |
| cp932→Unicode 変換テーブル | `vendor/cp932/cp932_to_ucs.h` | ― | 自前生成（下記参照） |
| SDL2 | （非同梱） | zlib License | 実機の動的ライブラリを実行時リンク |

## game-music-emu（libgme）— LGPL-2.1-or-later

GBS/NSF デコードと拡張M3U解析を丸ごと委譲している。自前で GB APU は
実装していない。ライセンス全文は submodule 内 `vendor/game-music-emu/license.txt`
（および逐語コピーを [`licenses/LGPL-2.1.txt`](./licenses/LGPL-2.1.txt) にも
同梱）。

**upstream ではなくフォーク https://github.com/ka-zuu/game-music-emu の
`mugbs` ブランチを参照している。** GBS の m3u トラック番号を0始まりとして
扱う独自パッチを当てているため（詳細は
[`docs/design-notes.md`](./docs/design-notes.md)「libgmeフォーク運用」参照）。
このフォークは恒久的に公開を維持する。upstream への追従は
`git -C vendor/game-music-emu fetch upstream` から行う。

YM2612 エミュレータは `Nuked`（LGPLv2.1+）に固定している
（`CMakeLists.txt` の `GME_YM2612_EMU`）。選択肢のうち `MAME` は
GPLv2+ になるため、明示的に `Nuked` を指定して黙って GPL 化しないように
している。

**静的リンクと LGPL-2.1 §6（再リンク条件）について。** `.muxapp` は
libgme を静的リンクした実行ファイルを配布している。LGPL-2.1 §6 は
「ユーザーがライブラリの改変版に差し替えて再リンクできること」を求めて
おり、muChip では以下の2点で満たす:

1. muChip 自身の完全なソースコードが MIT ライセンスで公開されている
   （このリポジトリそのもの）。
2. リンクしている libgme（改変版）のソースも上記フォークで公開されている。

**再リンク手順**（改変版 libgme に差し替えて自分でビルドする場合）:

```sh
# vendor/game-music-emu を任意の libgme ソースツリーに差し替える
# (例: 自分のフォークをクローンし直す、あるいはローカルの改変版に置き換える)
rm -rf vendor/game-music-emu
git clone <差し替えたいlibgmeのURL> vendor/game-music-emu

# ホスト向けに再ビルドして動作確認
./scripts/build-host.sh

# 実機向けに再ビルド・再パッケージ
./scripts/build-aarch64.sh
./scripts/package.sh
```

`CMakeLists.txt` は `vendor/game-music-emu` を `add_subdirectory()` して
`gme_static` を静的リンクするだけなので、ソースツリーを差し替えれば
そのまま新しいライブラリでリンクし直される。

## miniz — MIT

zip 展開に使用。https://github.com/richgel999/miniz より split-file
ソースを vendoring。ライセンス全文は `vendor/miniz/LICENSE`。

## font8x8 — パブリックドメイン

UI の文字描画（ASCII）に使用。https://github.com/dhepper/font8x8 より
`font8x8_basic.h`（basic latin, U+0000-U+007F）のみを vendoring。
オリジナルは Marcel Sondaar / IBM の public domain VGA フォントを
Daniel Hepper が整理したもの。ライセンス表記はヘッダ内コメントにある。

## 美咲フォント（misaki）— フリーソフトウェア

UI の文字描画（非ASCII）に使用。https://littlelimit.net/misaki.htm の
美咲ゴシック BDF版から `tools/make_misaki_font.py` で `misaki_gothic.h`
を生成して vendoring。Num Kadoma 氏によるフォント。ライセンス条件の
逐語引用は `vendor/misaki/README.md` を参照（改変・商用利用・再配布可、
無保証）。

## cp932→Unicode 変換テーブル — 自前生成

`vendor/cp932/cp932_to_ucs.h` は第三者のソースやデータファイルではなく、
Python 標準ライブラリの `cp932` コーデックのみを使って
`tools/make_cp932_table.py` が生成したテーブル。`vendor/` 配下に置いて
いるのは他の同梱物と生成物の置き場所を揃えるためで、外部依存は無い。

## SDL2 — zlib License（非同梱）

入出力・描画に使用。実機（muOS）の `/usr/lib` にある動的ライブラリを
実行時にリンクするのみで、`.muxapp` にはバイナリを同梱していない
（`scripts/package.sh` 参照）。ホストビルドでは `apt install libsdl2-dev`
等、システムの SDL2 をリンクする。zlib License はソース・バイナリいずれの
再配布義務も課さないため、同梱義務は発生しない。

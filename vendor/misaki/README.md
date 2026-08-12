# 美咲フォント (misaki font) — 取り込みメモ

`misaki_gothic.h` は 美咲フォント BDF版(美咲ゴシック, `misaki_gothic.bdf`)
から `tools/make_misaki_font.py` で自動生成した、8x8ドットの非ASCII
(日本語)グリフ表。 出典: https://littlelimit.net/misaki.htm

- 制作者: Num Kadoma 氏
- 収録範囲: JIS X 0208 (第1・第2水準) 相当 + 各種記号。BDFのCHARSET_REGISTRY
  が `ISO10646` のため、コードポイントは変換不要でそのままUnicodeとして
  扱える。
- ASCII域(U+0000-U+007F)は含まない。muChip側では `vendor/font8x8` の
  ビットマップをASCII用に使い続け、美咲は非ASCIIのみを補う形にしている
  (半角文字の字形をUI全体で統一するため)。

## ライセンス

配布アーカイブ同梱の `misaki.txt` 「ライセンス」節より原文のまま引用する:

> These fonts are free softwares.
> Unlimited permission is granted to use, copy, and distribute it, with or
> without modification, either commercially and noncommercially.
> THESE FONTS ARE PROVIDED "AS IS" WITHOUT WARRANTY.
>
> これらのフォントはフリー（自由な）ソフトウエアです。
> あらゆる改変の有無に関わらず、また商業的な利用であっても、自由にご利用、
> 複製、再配布することができますが、全て無保証とさせていただきます。

Copyright (C) 2002-2021 Num Kadoma。上記のとおり改変・商用利用・再配布は
自由であり、無保証で提供される。

## 再生成方法

`tools/make_misaki_font.py` の冒頭コメント参照。要点:

1. https://littlelimit.net/misaki.htm から `misaki_bdf_*.zip` を取得し展開
   (TTF/PNG版ではなくBDF版の `misaki_gothic.bdf` を使う)。
2. `python3 tools/make_misaki_font.py <path>/misaki_gothic.bdf vendor/misaki/misaki_gothic.h`

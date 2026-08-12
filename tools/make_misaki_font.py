#!/usr/bin/env python3
"""美咲フォント(BDF, ISO10646エンコード)から muChip 用の8x8日本語グリフ
テーブル(vendor/misaki/misaki_gothic.h)を生成する。

生成物はコミットするので、通常のビルド(scripts/build-host.sh 等)では
このスクリプトも入力のBDFもPillowも一切不要。フォントを更新したいときだけ、
以下の手順で再生成する:

    1. https://littlelimit.net/misaki.htm から
       misaki_bdf_YYYY-MM-DD.zip をダウンロードして展開する
       (X11 BDF形式。TTF/PNG版ではなくBDF版を使うこと)。
    2. 展開した misaki_gothic.bdf をこのスクリプトと同じ引数で渡す:
           python3 tools/make_misaki_font.py path/to/misaki_gothic.bdf \\
               vendor/misaki/misaki_gothic.h

ライセンス: 美咲フォントは Num Kadoma 氏によるフリーソフトウェアで、
「あらゆる改変の有無に関わらず、また商業的な利用であっても、自由にご利用、
複製、再配布することができ」る(無保証)。原文は同梱の misaki.txt の
「ライセンス」節、日本語訳は vendor/misaki/README.md を参照。

## BDFからの変換方法

misaki_gothic.bdf のヘッダは
    FONTBOUNDINGBOX 8 8 0 -2
    CHARSET_REGISTRY "ISO10646"
    CHARSET_ENCODING "1"
であり、各グリフの ENCODING の値がそのまま Unicode コードポイントになっている
(コードページ変換は不要)。グリフごとの BBX(bbw, bbh, bboffx, bboffy)は
フォント全体のバウンディングボックス(FBB)を基準にした位置とサイズなので、
以下の式で 8x8 のキャンバスへ配置する(ベースライン基準のy座標を
「上端=0行目」の行インデックスへ変換している):

    canvas_row = (FBBY + FBBH) - bboffy - bbh + r   (r: BITMAPの行番号, 0起点)

x方向は FBBX=0 なので canvas_col = bboffx + (グリフ内のビット位置x) をそのまま
使う。出力ビット順は vendor/font8x8/font8x8_basic.h に合わせて
「ビット0=左端」(BDFのMSB-first from-the-leftとは逆順)にしてある
(src/ui.c の build_font_atlas()/build_cjk_atlas() がこの前提で走査するため)。

ASCII域(コードポイント < 0x80)は font8x8_basic.h 側で描画するので、
このスクリプトは出力しない(半角文字の字形をUI全体で統一するため)。
"""
import sys
from pathlib import Path


def parse_bdf(path):
    """BDFを読み、(fbbw, fbbh, fbbx, fbby, {codepoint: (bbw,bbh,bboffx,bboffy,bitmap_rows)}) を返す。"""
    fbbw = fbbh = fbbx = fbby = None
    glyphs = {}
    with open(path, encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f]

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        if line.startswith("FONTBOUNDINGBOX"):
            parts = line.split()
            fbbw, fbbh, fbbx, fbby = (int(x) for x in parts[1:5])
        elif line.startswith("STARTCHAR"):
            enc = None
            bbw = bbh = bboffx = bboffy = None
            bitmap_rows = []
            i += 1
            while not lines[i].startswith("ENDCHAR"):
                l = lines[i]
                if l.startswith("ENCODING"):
                    enc = int(l.split()[1])
                elif l.startswith("BBX"):
                    parts = l.split()
                    bbw, bbh, bboffx, bboffy = (int(x) for x in parts[1:5])
                elif l == "BITMAP":
                    i += 1
                    for _ in range(bbh):
                        bitmap_rows.append(lines[i])
                        i += 1
                    continue
                i += 1
            # ENCODING -1 (未割り当て枠)や、BBXが無い異常な定義は無視する。
            if enc is not None and enc >= 0 and bbw is not None:
                glyphs[enc] = (bbw, bbh, bboffx, bboffy, bitmap_rows)
        i += 1

    if fbbw is None:
        raise ValueError("FONTBOUNDINGBOX が見つかりません: " + str(path))
    return fbbw, fbbh, fbbx, fbby, glyphs


def glyph_to_bits(fbbh, fbby, bbw, bbh, bboffx, bboffy, bitmap_rows):
    """1グリフ分のBDFデータを8バイトの8x8キャンバス(各バイトはビット0=左端)へ変換する。"""
    canvas = [0] * 8
    nbytes = (bbw + 7) // 8
    for r, hexrow in enumerate(bitmap_rows):
        val = int(hexrow, 16) if hexrow else 0
        canvas_row = (fbby + fbbh) - bboffy - bbh + r
        if not (0 <= canvas_row < 8):
            # FBB(8x8)からはみ出す行は切り捨てる(美咲では実質発生しない)。
            continue
        for x in range(bbw):
            bit = (val >> (nbytes * 8 - 1 - x)) & 1
            if not bit:
                continue
            cx = bboffx + x
            if 0 <= cx < 8:
                canvas[canvas_row] |= 1 << cx
    return canvas


def main():
    if len(sys.argv) != 3:
        print(f"使い方: {sys.argv[0]} <misaki_gothic.bdf> <出力先.h>", file=sys.stderr)
        return 1

    src_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    fbbw, fbbh, fbbx, fbby, glyphs = parse_bdf(src_path)

    # ASCII域(font8x8_basic.hが担当)を除いた非ASCIIのみ、コードポイント昇順で出力する。
    # 昇順は src/ui.c のbsearch()前提。
    entries = []
    for cp in sorted(glyphs):
        if cp < 0x80:
            continue
        bbw, bbh, bboffx, bboffy, bitmap_rows = glyphs[cp]
        canvas = glyph_to_bits(fbbh, fbby, bbw, bbh, bboffx, bboffy, bitmap_rows)
        entries.append((cp, canvas))

    lines = []
    lines.append("/* misaki_gothic.h - 美咲ゴシック(8x8, ISO10646版)由来の非ASCIIグリフ表。")
    lines.append(" *")
    lines.append(" * tools/make_misaki_font.py で misaki_gothic.bdf から自動生成した。")
    lines.append(" * 手で編集しないこと(再生成手順は同スクリプトの冒頭コメント参照)。")
    lines.append(" *")
    lines.append(" * ライセンス: vendor/misaki/README.md 参照(改変・商用利用・再配布可、無保証)。")
    lines.append(" *")
    lines.append(" * 各グリフは8バイト(8行分)。font8x8_basic.h と同じく")
    lines.append(" * 「ビット0=左端の桁」。コードポイント昇順(src/ui.c の bsearch() 前提)。")
    lines.append(" * ASCII域(U+0000-U+007F)は font8x8_basic.h 側が担当するため含まない。")
    lines.append(" */")
    lines.append("#ifndef MUCHIP_MISAKI_GOTHIC_H")
    lines.append("#define MUCHIP_MISAKI_GOTHIC_H")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    unsigned short cp;")
    lines.append("    unsigned char bits[8];")
    lines.append("} misaki_glyph_t;")
    lines.append("")
    lines.append(f"#define MISAKI_GLYPH_COUNT {len(entries)}")
    lines.append("")
    lines.append("static const misaki_glyph_t misaki_glyphs[MISAKI_GLYPH_COUNT] = {")
    for cp, canvas in entries:
        bits_str = ", ".join(f"0x{b:02x}" for b in canvas)
        lines.append(f"    {{ 0x{cp:04x}, {{ {bits_str} }} }},")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* MUCHIP_MISAKI_GOTHIC_H */")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(entries)} グリフを {out_path} へ書き出しました")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

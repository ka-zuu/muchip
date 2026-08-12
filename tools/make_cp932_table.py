#!/usr/bin/env python3
"""CP932(Shift_JIS拡張)の2バイト文字→Unicode変換表(vendor/cp932/cp932_to_ucs.h)
を生成する。

NSF/GBSのヘッダやM3Uの拡張コメントに日本語(Shift_JIS)が入っていることがある
(Issue参照)。muChip はメタデータ取り込み時に src/text.c でこれをUTF-8へ
正規化するが、変換表自体はビルド時にPythonへ依存させたくないので、
生成済みヘッダをコミットする(tools/make_glyph.py と同じ方針)。

Python標準の 'cp932' コーデックだけを使い、外部依存は無い。
フォントと違い元データが変わることは無いので、通常は再生成不要。

## テーブル構造

2バイト文字だけを対象にした密な表(2次元配列とみなせるフラット配列)。
1バイト側(0x00-0x7Fはそのまま、半角カナ0xA1-0xDFは
`0xFF61 + (b - 0xA1)` の算術)は表に持たず src/text.c 側でコード化する。

  lead  : 0x81-0x9F, 0xE0-0xFC (未定義の間隙も含めて連続領域として確保)
  trail : 0x40-0xFC (0x7Fも含む。未使用値なので表の中身は0のまま)

  index = lead_to_row(lead) * CP932_TRAIL_COUNT + (trail - CP932_TRAIL_MIN)

値0 = 未定義(unicode private use area等、cp932コーデックが例外を出す組)。
"""
import sys
from pathlib import Path

LEAD_RANGES = [(0x81, 0x9F), (0xE0, 0xFC)]
TRAIL_MIN = 0x40
TRAIL_MAX = 0xFC


def lead_rows():
    rows = []
    for lo, hi in LEAD_RANGES:
        rows.extend(range(lo, hi + 1))
    return rows


def main():
    if len(sys.argv) != 2:
        print(f"使い方: {sys.argv[0]} <出力先.h>", file=sys.stderr)
        return 1
    out_path = Path(sys.argv[1])

    rows = lead_rows()
    trail_count = TRAIL_MAX - TRAIL_MIN + 1

    table = []
    defined = 0
    for lead in rows:
        for trail in range(TRAIL_MIN, TRAIL_MAX + 1):
            try:
                ch = bytes([lead, trail]).decode("cp932")
            except UnicodeDecodeError:
                table.append(0)
                continue
            if len(ch) != 1:
                # サロゲートペア等(cp932の2バイト範囲では基本発生しない)は
                # 安全側に倒して未定義扱いにする。
                table.append(0)
                continue
            cp = ord(ch)
            if cp == 0 or cp > 0xFFFF:
                table.append(0)
                continue
            table.append(cp)
            defined += 1

    lines = []
    lines.append("/* cp932_to_ucs.h - CP932(Shift_JIS拡張)の2バイト文字→Unicode変換表。")
    lines.append(" *")
    lines.append(" * tools/make_cp932_table.py で自動生成した。手で編集しないこと")
    lines.append(" * (生成方法は同スクリプトの冒頭コメント参照)。")
    lines.append(" *")
    lines.append(" * 引き方は src/text.c の cp932_lookup() を参照。0=未定義。")
    lines.append(" */")
    lines.append("#ifndef MUCHIP_CP932_TO_UCS_H")
    lines.append("#define MUCHIP_CP932_TO_UCS_H")
    lines.append("")
    lines.append(f"#define CP932_TRAIL_MIN 0x{TRAIL_MIN:02x}")
    lines.append(f"#define CP932_TRAIL_MAX 0x{TRAIL_MAX:02x}")
    lines.append(f"#define CP932_TRAIL_COUNT {trail_count}")
    lines.append("")
    lines.append("/* lead バイト -> 行番号。範囲外・間隙は負値(呼び出し側で弾く)。 */")
    lines.append("static inline int cp932_lead_row(unsigned char lead) {")
    row_expr = []
    offset = 0
    for lo, hi in LEAD_RANGES:
        row_expr.append(f"    if (lead >= 0x{lo:02x} && lead <= 0x{hi:02x}) return {offset} + (lead - 0x{lo:02x});")
        offset += hi - lo + 1
    lines.extend(row_expr)
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    lines.append(f"#define CP932_LEAD_ROWS {len(rows)}")
    lines.append("")
    lines.append(f"static const unsigned short cp932_to_ucs[CP932_LEAD_ROWS * CP932_TRAIL_COUNT] = {{")
    per_line = 16
    for i in range(0, len(table), per_line):
        chunk = table[i:i + per_line]
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* MUCHIP_CP932_TO_UCS_H */")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"lead行数={len(rows)} trail列数={trail_count} 定義済み={defined}/{len(table)} を {out_path} へ書き出しました")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

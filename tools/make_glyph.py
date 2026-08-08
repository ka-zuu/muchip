#!/usr/bin/env python3
"""muOS のアプリ一覧に出すアイコンを生成する。 (P7)

生成物:
    packaging/muChip/glyph/muchip.png   32x32  リスト表示用
    packaging/muChip/grid/muchip.png    96x96  グリッド表示用

生成物はコミットするので、パッケージング時 (scripts/package.sh) に Pillow は
不要。図案を変えたいときだけこのスクリプトを再実行する。

muOS 側の仕様 (MustardOS/frontend を読んで確認したもの):

* frontend/common/ui/glyph.c の apply_app_glyph() が
  <app_dir>/glyph/<ICON名>.<ext> を、get_app_grid_glyph() が
  <app_dir>/grid/<ICON名>.<ext> を探す。<ICON名> は mux_launch.sh の
  "# ICON: " 行の値 (= muchip)。既定テーマの 640x480 はグリッド表示なので、
  glyph/ だけでは足りず grid/ も必要 (SPEC 9.1 はこれに触れていない)。
* 描画時に lv_style_set_img_recolor() でテーマ色に塗り潰されるため、
  実質アルファチャンネルだけが意味を持つ。色は白固定にしておく。
* PNG はサイズヒントを無視してネイティブサイズで描画される。実際に動作して
  いる muOS アプリ (XMPlayer v0.2.1) の glyph は 32x32 の LA モード PNG
  だったので、それに合わせる。grid は既定テーマの CELL_WIDTH/HEIGHT=140 から
  96px 角とした。

図案は連桁付きの8分音符。符頭を丸ではなく正方形にして Game Boy のドット感を
出している。UI フォント (vendor/font8x8) と同じ「四角い」トーンに揃える意図。
"""

import os

from PIL import Image, ImageDraw
from PIL.PngImagePlugin import PngInfo

# 0..1 の正規化座標で図形を定義する。どのサイズでも同じ見た目になる。
# (x0, y0, x1, y1)
# 2つの符頭を段違いにして符幹の長さを変えると、32px でも「音符」として
# 読みやすくなる(同じ高さに揃えると 'n' の字に見えてしまう)。
SHAPES = [
    (0.30, 0.08, 0.92, 0.21),  # 連桁 (beam)
    (0.30, 0.08, 0.38, 0.72),  # 左の符幹 (stem。符頭が下にあるので長い)
    (0.84, 0.08, 0.92, 0.60),  # 右の符幹
    (0.08, 0.66, 0.38, 0.90),  # 左の符頭 (note head)
    (0.62, 0.54, 0.92, 0.78),  # 右の符頭
]

# アンチエイリアスのための拡大率。この倍率で描いて LANCZOS で縮小する。
SUPERSAMPLE = 8

OUTPUTS = [
    (os.path.join("packaging", "muChip", "glyph", "muchip.png"), 32),
    (os.path.join("packaging", "muChip", "grid", "muchip.png"), 96),
]


def render(size):
    """size x size の LA モード画像を返す。L=白固定、A=図案。"""
    big = size * SUPERSAMPLE
    # アルファチャンネルを 'L' の1枚絵として描く。
    alpha = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(alpha)
    for x0, y0, x1, y1 in SHAPES:
        # PIL の rectangle は終点を含むので -1 して幅を意図どおりにする。
        draw.rectangle(
            (
                round(x0 * big),
                round(y0 * big),
                round(x1 * big) - 1,
                round(y1 * big) - 1,
            ),
            fill=255,
        )
    alpha = alpha.resize((size, size), Image.LANCZOS)

    # muOS が recolor するので L は白で埋める。透明部分の L 値は描画結果に
    # 影響しないが、0 のままだと縁に暗い色が滲むビューアがあるため白にする。
    lum = Image.new("L", (size, size), 255)
    return Image.merge("LA", (lum, alpha))


def main():
    # リポジトリルートからの相対パスで書くため、スクリプトの1つ上へ移動する。
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    for path, size in OUTPUTS:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        img = render(size)
        # 再実行でバイト単位の差分が出ないよう、メタデータ(タイムスタンプ等)を
        # 一切書かない。optimize も Pillow の版によって結果が変わりうるので使わない。
        img.save(path, format="PNG", pnginfo=PngInfo(), optimize=False)
        print(f"{path}: {size}x{size} {img.mode}")


if __name__ == "__main__":
    main()

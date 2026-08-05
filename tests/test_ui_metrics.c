/* test_ui_metrics.c - P6で追加した ui_metrics_compute()/ui_glyph_size_for() の
 * 単体テスト。
 *
 * どちらも SDL_Init を必要としない純関数として src/ui.c から切り出した
 * (SDL_min 等の型だけは使うが、ウィンドウ/レンダラ/テクスチャは一切触らない)。
 * ui.c 全体をリンクするため SDL2 は必要だが、このテストは SDL_Init を
 * 一度も呼ばない。 SPEC 6.2「解像度非依存」「座標のハードコード禁止」の
 * 不変条件が、実機の 640x480 から極端な解像度まで崩れないことを検証する。
 */
#include <stdio.h>

#include "ui.h"
#include "test_util.h"

/* muOS実機で想定される解像度に加え、極端な値・アスペクト比を含める。 */
static const struct { int w, h; } RESOLUTIONS[] = {
    { 128, 64 },    /* 極端に小さい(下限確認用) */
    { 320, 240 },   /* 小型機 */
    { 480, 320 },
    { 640, 480 },   /* 実機(RG35XX PRO) 基準解像度 */
    { 720, 720 },   /* RG CubeXX相当(正方形) */
    { 1024, 768 },  /* TrimUI Brick相当 */
    { 1280, 720 },
    { 1920, 1080 },
    { 1280, 240 },  /* 極端に横長 */
    { 240, 1280 },  /* 極端に縦長 */
    { 640, 64 },
    { 64, 640 },
};
#define RES_COUNT ((int)(sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0])))

/* あらゆる解像度で崩れてはならない不変条件。 */
static int test_invariants(void) {
    for (int i = 0; i < RES_COUNT; i++) {
        int w = RESOLUTIONS[i].w, h = RESOLUTIONS[i].h;
        ui_metrics_t m;
        ui_metrics_compute(w, h, &m);

        char ctx[64];
        snprintf(ctx, sizeof(ctx), "%dx%d", w, h);

        CHECK(m.scale > 0.0f);
        CHECK(m.glyph >= 8);      /* font8x8 の最小サイズ */
        CHECK(m.pad >= 2);
        CHECK(m.line_h > m.glyph); /* pad分の余白があること */

        /* app.c の list_rect() が負の高さを返さないための不変条件。 */
        CHECK(m.header_h + m.footer_h < h);

        /* 最低1行はリストが描画できること。 */
        int list_h = h - m.header_h - m.footer_h;
        CHECK(list_h / m.line_h >= 1);
    }
    return 0;
}

/* 640x480 (scale=1.0) では SPEC 7 相当の基準値になること。 */
static int test_baseline_640x480(void) {
    ui_metrics_t m;
    ui_metrics_compute(640, 480, &m);
    CHECK(m.scale > 0.999f && m.scale < 1.001f);
    CHECK(m.glyph == 16); /* UI_TEXT_BODY(=2) * 8px * scale(1.0) */
    return 0;
}

/* 解像度を上げていくと glyph が縮まない(単調性)こと。 */
static int test_monotonic_glyph(void) {
    static const int sizes[][2] = { { 320, 240 }, { 480, 320 }, { 640, 480 },
                                     { 1024, 768 }, { 1920, 1080 } };
    int prev = 0;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ui_metrics_t m;
        ui_metrics_compute(sizes[i][0], sizes[i][1], &m);
        CHECK(m.glyph >= prev);
        prev = m.glyph;
    }
    return 0;
}

/* scale<1.0 でも pad が2以上を保ち、以前のように4に張り付いたままに
 * ならないこと(P6で発見した不具合の回帰確認)。 320x240 は scale=0.5。 */
static int test_pad_scales_down_too(void) {
    ui_metrics_t half, full;
    ui_metrics_compute(320, 240, &half);  /* scale = 0.5 */
    ui_metrics_compute(640, 480, &full);  /* scale = 1.0 */
    CHECK(half.pad >= 2);
    CHECK(half.pad < full.pad); /* 以前は両方4で一致してしまっていた */
    return 0;
}

/* ui_glyph_size_for() 単体: サイズ段階が大きいほど広く、8px未満にはならない。 */
static int test_glyph_size_for(void) {
    CHECK(ui_glyph_size_for(1.0f, UI_TEXT_SMALL) == 8);
    CHECK(ui_glyph_size_for(1.0f, UI_TEXT_BODY) == 16);
    CHECK(ui_glyph_size_for(1.0f, UI_TEXT_TITLE) == 24);
    CHECK(ui_glyph_size_for(0.01f, UI_TEXT_SMALL) == 8); /* 下限 */
    CHECK(ui_glyph_size_for(2.0f, UI_TEXT_SMALL) < ui_glyph_size_for(2.0f, UI_TEXT_TITLE));
    return 0;
}

int main(void) {
    if (test_invariants()) return 1;
    if (test_baseline_640x480()) return 1;
    if (test_monotonic_glyph()) return 1;
    if (test_pad_scales_down_too()) return 1;
    if (test_glyph_size_for()) return 1;

    printf("test_ui_metrics: すべて成功\n");
    return 0;
}

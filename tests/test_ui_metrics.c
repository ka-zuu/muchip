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
#include <string.h>

#include "ui.h"
#include "test_util.h"

/* SDL_Init不要な ui_t を作る(metricsだけ埋める。win/ren/atlasはNULLのまま
 * でよい。ui_list_visible_rows()/ui_list_clamp_scroll()はmetricsしか
 * 見ないため、ui_draw_list()やui_text()と違ってこれで安全にテストできる)。 */
static ui_t make_ui(int screen_w, int screen_h) {
    ui_t ui;
    memset(&ui, 0, sizeof(ui));
    ui_metrics_compute(screen_w, screen_h, &ui.metrics);
    return ui;
}

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

        /* Issue #41: ヘッダはタイトル(TITLE)+サブ(SMALL)の2段、フッタは
         * 操作ヒント2行(SMALL x2)が物理的に載ること。line_h も
         * glyph+pad*2 まで広げた行間を保つこと。 */
        int title_px = ui_glyph_size_for(m.scale, UI_TEXT_TITLE);
        int small_px = ui_glyph_size_for(m.scale, UI_TEXT_SMALL);
        CHECK(m.header_h >= title_px + small_px);
        CHECK(m.footer_h >= small_px * 2);
        CHECK(m.line_h >= m.glyph + m.pad * 2);

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
    /* Issue #41: line_h=glyph+pad*2, header_h=title_px+small_px+pad*3,
     * footer_h=small_px*2+pad*3。pad=4, title_px=24, small_px=8。 */
    CHECK(m.line_h == 24);
    CHECK(m.header_h == 44);
    CHECK(m.footer_h == 28);
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

/* Issue #8: ui_marquee_offset() 単体。ui_glyph_size_for() と同じく
 * SDL_Init不要な純関数。ui.c の実装コメントにある契約
 * (gap=glyph_px*4, speed=glyph_px*4 px/秒, hold=1000ms) を前提に、
 * 具体的な数値で検証する(test_glyph_size_for()と同じ方針)。 */
static int test_marquee_fits_is_always_zero(void) {
    /* text_w <= max_w なら time_ms に関わらず常に0(=止まっている)。 */
    CHECK(ui_marquee_offset(80, 100, 16, 0) == 0);
    CHECK(ui_marquee_offset(100, 100, 16, 0) == 0);      /* ちょうど収まる境界 */
    CHECK(ui_marquee_offset(80, 100, 16, 999999) == 0);
    CHECK(ui_marquee_offset(80, 100, 0, 500) == 0);       /* glyph_px<=0 も安全側 */
    return 0;
}

static int test_marquee_holds_at_start(void) {
    /* 収まらない文字列は、周期の頭で hold(=1000ms) だけ止まって
     * 曲名の先頭を読ませる。 */
    CHECK(ui_marquee_offset(200, 100, 16, 0) == 0);
    CHECK(ui_marquee_offset(200, 100, 16, 500) == 0);
    CHECK(ui_marquee_offset(200, 100, 16, 999) == 0);
    return 0;
}

static int test_marquee_advances_and_wraps(void) {
    int text_w = 200, max_w = 100, glyph_px = 16;
    int gap = glyph_px * 4;              /* 64 */
    int period = text_w + gap;           /* 264 */
    int speed = glyph_px * 4;            /* 64 px/秒 */
    Uint32 hold_ms = 1000;
    Uint32 period_ms = (Uint32)(((long long)period * 1000) / speed); /* 4125 */
    Uint32 cycle_ms = hold_ms + period_ms;

    /* hold明け直後は0から動き出す。 */
    CHECK(ui_marquee_offset(text_w, max_w, glyph_px, hold_ms) == 0);

    /* 単調非減少で、period未満に収まり続けること。 */
    int prev = -1;
    for (Uint32 t = hold_ms; t < hold_ms + period_ms; t += 137) {
        int off = ui_marquee_offset(text_w, max_w, glyph_px, t);
        CHECK(off >= prev);
        CHECK(off >= 0 && off < period);
        prev = off;
    }
    /* 周期の終盤ではperiod-1に近いところまで進んでいること。 */
    CHECK(prev > period / 2);

    /* 1周期後、同じ位相なら同じオフセットに戻る(周回)。 */
    CHECK(ui_marquee_offset(text_w, max_w, glyph_px, 0) ==
          ui_marquee_offset(text_w, max_w, glyph_px, cycle_ms));
    CHECK(ui_marquee_offset(text_w, max_w, glyph_px, 500) ==
          ui_marquee_offset(text_w, max_w, glyph_px, cycle_ms + 500));
    CHECK(ui_marquee_offset(text_w, max_w, glyph_px, hold_ms + 1234) ==
          ui_marquee_offset(text_w, max_w, glyph_px, cycle_ms + hold_ms + 1234));
    return 0;
}

/* 別の解像度段階(BODY=16px, TITLE=24px相当)でも破綻しないこと。 */
static int test_marquee_multiple_glyph_sizes(void) {
    static const int glyph_sizes[] = { 8, 16, 24 };
    for (size_t i = 0; i < sizeof(glyph_sizes) / sizeof(glyph_sizes[0]); i++) {
        int glyph_px = glyph_sizes[i];
        int text_w = glyph_px * 20;
        int max_w = glyph_px * 8;
        for (Uint32 t = 0; t < 10000; t += 333) {
            int off = ui_marquee_offset(text_w, max_w, glyph_px, t);
            CHECK(off >= 0 && off < text_w + glyph_px * 4);
        }
    }
    return 0;
}

/* Issue #27: ui_draw_list()から抽出したui_list_visible_rows()。
 * テーマエディタ画面(独自の行描画ループを持つ)がこれを共有する。 */
static int test_list_visible_rows(void) {
    ui_t ui = make_ui(640, 480);

    ui_rect_t r5 = { 0, 0, 640, ui.metrics.line_h * 5 };
    CHECK(ui_list_visible_rows(&ui, r5) == 5);

    ui_rect_t zero = { 0, 0, 640, 0 };
    CHECK(ui_list_visible_rows(&ui, zero) == 1); /* r.h==0でも最低1 */

    ui_rect_t partial = { 0, 0, 640, ui.metrics.line_h * 3 + ui.metrics.line_h / 2 };
    CHECK(ui_list_visible_rows(&ui, partial) == 3); /* 端数は切り捨て */
    return 0;
}

/* ui_draw_list()から抽出したui_list_clamp_scroll()。selectedが常に
 * 可視範囲に収まるよう*scrollが調整されること(ui_draw_list()の
 * 挙動そのもの)。 */
static int test_list_clamp_scroll(void) {
    ui_t ui = make_ui(640, 480);
    ui_rect_t r = { 0, 0, 640, ui.metrics.line_h * 5 }; /* visible=5 */

    /* count<=0ならscrollは常に0にリセットされる。 */
    int scroll = 7;
    ui_list_clamp_scroll(&ui, r, 0, 0, &scroll);
    CHECK(scroll == 0);

    /* selectedが可視範囲より下にあれば、selectedが見えるところまで
     * scrollが進む。 */
    scroll = 0;
    ui_list_clamp_scroll(&ui, r, 20, 10, &scroll);
    CHECK(scroll <= 10 && scroll + 5 > 10);

    /* selectedが可視範囲より上にあれば、selectedの位置までscrollが戻る。 */
    scroll = 15;
    ui_list_clamp_scroll(&ui, r, 20, 2, &scroll);
    CHECK(scroll == 2);

    /* max_scroll(count-visible)を超えない。 */
    scroll = 100;
    ui_list_clamp_scroll(&ui, r, 20, 19, &scroll);
    CHECK(scroll == 20 - 5);

    /* countがvisible以下ならscrollは常に0。 */
    scroll = 3;
    ui_list_clamp_scroll(&ui, r, 3, 1, &scroll);
    CHECK(scroll == 0);
    return 0;
}

int main(void) {
    if (test_invariants()) return 1;
    if (test_baseline_640x480()) return 1;
    if (test_monotonic_glyph()) return 1;
    if (test_pad_scales_down_too()) return 1;
    if (test_glyph_size_for()) return 1;
    if (test_marquee_fits_is_always_zero()) return 1;
    if (test_marquee_holds_at_start()) return 1;
    if (test_marquee_advances_and_wraps()) return 1;
    if (test_marquee_multiple_glyph_sizes()) return 1;
    if (test_list_visible_rows()) return 1;
    if (test_list_clamp_scroll()) return 1;

    printf("test_ui_metrics: すべて成功\n");
    return 0;
}

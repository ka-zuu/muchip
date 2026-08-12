/* test_theme.c - theme.c(カラーテーマの色計算・プリセット)の単体テスト。
 * (Issue #27)
 *
 * SDL にもlibgmeにも依存しない純libc(config.c/eq.cと同じ方針)。
 *
 * test_midnight_matches_legacy_literals() が最重要: Issue #27より前の
 * app.c/ui.c に直書きされていた SDL_Color リテラルと完全一致することを
 * 固定する。これが崩れると「テーマ機能を入れたら見た目が変わった」という
 * フェーズ1の目標(docs/design-notes.md「カラーテーマ」参照)が壊れる。
 */
#include <string.h>

#include "theme.h"
#include "test_util.h"

static int color_eq(theme_color_t a, theme_color_t b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static theme_color_t mk(int r, int g, int b) {
    theme_color_t c = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
    return c;
}

static int abs_diff(int a, int b) {
    return a > b ? a - b : b - a;
}

/* 各chの差が全てmax以内か。 */
static int color_near(theme_color_t a, theme_color_t b, int max) {
    return abs_diff(a.r, b.r) <= max && abs_diff(a.g, b.g) <= max && abs_diff(a.b, b.b) <= max;
}

/* ---- 1. midnightが旧リテラルと完全一致 ---------------------------------- */

static int test_midnight_matches_legacy_literals(void) {
    theme_t t;
    theme_preset(THEME_MIDNIGHT, &t);

    CHECK(color_eq(t.slot[THEME_BG],     mk(18,  18,  26)));
    CHECK(color_eq(t.slot[THEME_PANEL],  mk(30,  30,  42))); /* 旧bar_bg/msg_bg。draw_playerの
                                                                * seekbar背景{50,50,60}はTHEME_ROLE_GUTTER
                                                                * (派生)へ分離したので対象外。 */
    CHECK(color_eq(t.slot[THEME_FG],     mk(230, 230, 230)));
    CHECK(color_eq(t.slot[THEME_DIM],    mk(150, 150, 160)));
    CHECK(color_eq(t.slot[THEME_ACCENT], mk(120, 180, 255)));
    CHECK(color_eq(t.slot[THEME_SEL],    mk(60,  90,  160))); /* 旧ui.cのsel_bg */
    CHECK(color_eq(t.slot[THEME_MARK],   mk(255, 210, 90)));
    CHECK(color_eq(t.slot[THEME_WARN],   mk(255, 120, 90)));
    CHECK(color_eq(t.slot[THEME_OK],     mk(120, 220, 140)));
    return 0;
}

/* ---- 2. 派生色が旧リテラルの近傍 ------------------------------------------ */

static int test_midnight_derived_colors_near_legacy(void) {
    theme_t t;
    theme_preset(THEME_MIDNIGHT, &t);

    /* raised(旧list_bg{26,26,36})・gutter(旧seekbar背景{50,50,60})・
     * sunken(旧wave_bg{12,12,20})は各ch±8以内。 */
    CHECK(color_near(theme_role_color(&t, THEME_ROLE_RAISED), mk(26, 26, 36), 8));
    CHECK(color_near(theme_role_color(&t, THEME_ROLE_GUTTER), mk(50, 50, 60), 8));
    CHECK(color_near(theme_role_color(&t, THEME_ROLE_SUNKEN), mk(12, 12, 20), 8));
    /* box_bg(旧{40,22,22})はmix(bg,warn,10%)がb chで+10ずれる
     * (docs/design-notes.md「カラーテーマ」に記録済みの既知の乖離)ので
     * ここだけ許容幅を広げる。 */
    CHECK(color_near(theme_role_color(&t, THEME_ROLE_BOX_BG), mk(40, 22, 22), 12));
    return 0;
}

/* ---- 3. theme_mix の境界 -------------------------------------------------- */

static int test_mix_bounds(void) {
    theme_color_t a = mk(10, 20, 30);
    theme_color_t b = mk(200, 100, 50);

    CHECK(color_eq(theme_mix(a, b, 0), a));
    CHECK(color_eq(theme_mix(a, b, 100), b));
    /* 範囲外pctはクランプする。 */
    CHECK(color_eq(theme_mix(a, b, -50), a));
    CHECK(color_eq(theme_mix(a, b, 999), b));

    /* unsigned charのオーバーフローが起きない(飽和もクランプもchar幅内)。 */
    theme_color_t white = mk(255, 255, 255);
    theme_color_t black = mk(0, 0, 0);
    theme_color_t mid = theme_mix(black, white, 50);
    CHECK(mid.r <= 255 && mid.g <= 255 && mid.b <= 255);
    return 0;
}

/* ---- 4. theme_recede の不変条件 ------------------------------------------ */

static int recede_headroom_ok(theme_color_t base) {
    theme_color_t fg_light = mk(255, 255, 255);
    theme_color_t fg_dark = mk(0, 0, 0);
    /* baseよりfgが明るい場合(黒方向)・暗い場合(白方向)のどちらでも、
     * sunkenはbaseそのものから離れていなければならない
     * (theme.hのヘッドルームフォールバックが機能していることの検証)。 */
    theme_color_t s1 = theme_recede(base, fg_light, 33);
    theme_color_t s2 = theme_recede(base, fg_dark, 33);
    if (theme_luma(s1) == theme_luma(base) && theme_luma(s2) == theme_luma(base)) return 0;
    return 1;
}

static int test_recede_headroom_fallback(void) {
    /* bg=000000/FFFFFFという極端なプリセットでも波形パネルが消えない
     * (theme.h「ヘッドルーム」参照)。少なくとも一方のfg方向で
     * luma差が出ることを確認する(両方向とも余地ゼロなのは黒か白の
     * どちらかだけなので、色によっては片方は必ず動く)。 */
    CHECK(recede_headroom_ok(mk(0, 0, 0)));
    CHECK(recede_headroom_ok(mk(255, 255, 255)));

    /* 全プリセットでもsunkenがbgと完全一致しない(recede()が実質no-opに
     * なっていない = ヘッドルームフォールバックが必要な状況でも
     * ちゃんと動いていること)。bg自体が既に暗いプリセット(midnight/
     * synthwave)では33%寄せても輝度差はわずか(luma差5程度)にしか
     * ならないため、ここでは「区別できるレベルの高コントラスト」までは
     * 求めない(それはtest_preset_contrast()のfg/dim/selの役割)。 */
    for (theme_id_t id = THEME_MIDNIGHT; id < THEME_CUSTOM; id++) {
        theme_t t;
        theme_preset(id, &t);
        theme_color_t bg = t.slot[THEME_BG];
        theme_color_t sunken = theme_role_color(&t, THEME_ROLE_SUNKEN);
        CHECK(!color_eq(bg, sunken));
    }
    return 0;
}

/* ---- 5. 全プリセットのコントラスト不変条件 -------------------------------- */

static int test_preset_contrast(void) {
    const int MIN_DIFF = 30; /* 実測値の最小マージン(gameboyのdim-bg=53)より
                               * 十分小さい安全側の閾値。プリセット追加時の
                               * 品質ゲートとして機能する。 */
    for (theme_id_t id = THEME_MIDNIGHT; id < THEME_CUSTOM; id++) {
        theme_t t;
        theme_preset(id, &t);
        int bg = theme_luma(t.slot[THEME_BG]);
        int fg = theme_luma(t.slot[THEME_FG]);
        int dim = theme_luma(t.slot[THEME_DIM]);
        int sel = theme_luma(t.slot[THEME_SEL]);

        CHECK(abs_diff(fg, bg) >= MIN_DIFF);
        CHECK(abs_diff(dim, bg) >= MIN_DIFF);

        /* ui_draw_list()の選択行の文字色と同じ計算
         * (theme_best_on(sel, fg, bg))。 */
        theme_color_t best = theme_best_on(t.slot[THEME_SEL], t.slot[THEME_FG], t.slot[THEME_BG]);
        CHECK(abs_diff(theme_luma(best), sel) >= MIN_DIFF);
    }
    return 0;
}

static int test_best_on_picks_higher_contrast(void) {
    theme_color_t bg = mk(20, 20, 20);
    theme_color_t near = mk(30, 30, 30);   /* bgに近い(低コントラスト) */
    theme_color_t far = mk(240, 240, 240); /* bgから遠い(高コントラスト) */
    CHECK(color_eq(theme_best_on(bg, near, far), far));
    CHECK(color_eq(theme_best_on(bg, far, near), far)); /* 引数順序に依らない */
    return 0;
}

/* ---- 6. theme_channel_step -------------------------------------------------- */

static int test_channel_step_monotonic_and_fixed_points(void) {
    /* 0/255は固定点(それ以上その方向へは動かない)。 */
    CHECK(theme_channel_step(0, -1) == 0);
    CHECK(theme_channel_step(255, 1) == 255);

    /* 0から上げ続けると単調非減少で255へ到達し、それ以上は増えない。 */
    int v = 0, prev = -1;
    for (int i = 0; i < 64; i++) {
        v = theme_channel_step(v, 1);
        CHECK(v >= prev);
        prev = v;
    }
    CHECK(v == 255);

    /* 255から下げ続けると単調非増加で0へ到達する。 */
    v = 255;
    prev = 256;
    for (int i = 0; i < 64; i++) {
        v = theme_channel_step(v, -1);
        CHECK(v <= prev);
        prev = v;
    }
    CHECK(v == 0);

    /* 梯子に乗っていない値(config.ini手編集を想定)からの初回操作は、
     * 動く方向側へ吸着する。 */
    CHECK(theme_channel_step(18, 1) == 24);
    CHECK(theme_channel_step(18, -1) == 16);
    return 0;
}

/* ---- 7. theme_id_from_name / theme_id_name ------------------------------- */

static int test_theme_id_name_roundtrip(void) {
    for (theme_id_t id = THEME_MIDNIGHT; id < THEME_ID_COUNT; id++) {
        const char *name = theme_id_name(id);
        theme_id_t parsed;
        CHECK(theme_id_from_name(name, &parsed) == 0);
        CHECK(parsed == id);
    }

    /* 大小文字不問。 */
    theme_id_t id;
    CHECK(theme_id_from_name("MIDNIGHT", &id) == 0);
    CHECK(id == THEME_MIDNIGHT);
    CHECK(theme_id_from_name("GameBoy", &id) == 0);
    CHECK(id == THEME_GAMEBOY);

    /* 未知の名前はエラー。 */
    CHECK(theme_id_from_name("nonexistent", &id) != 0);
    return 0;
}

/* ---- 8. theme_color_parse / theme_color_format --------------------------- */

static int test_color_parse_format_roundtrip(void) {
    theme_color_t c;
    CHECK(theme_color_parse("1a2b3c", &c) == 0);
    CHECK(color_eq(c, mk(0x1a, 0x2b, 0x3c)));

    /* '#'付き・大文字も可。 */
    CHECK(theme_color_parse("#1A2B3C", &c) == 0);
    CHECK(color_eq(c, mk(0x1a, 0x2b, 0x3c)));

    char out[7];
    theme_color_format(mk(0x1a, 0x2b, 0x3c), out);
    CHECK_STREQ(out, "1a2b3c");

    /* 桁数違い・非16進・空文字は失敗する。 */
    CHECK(theme_color_parse("1a2b3", &c) != 0);   /* 5桁 */
    CHECK(theme_color_parse("1a2b3c4", &c) != 0); /* 7桁 */
    CHECK(theme_color_parse("zzzzzz", &c) != 0);  /* 非16進 */
    CHECK(theme_color_parse("", &c) != 0);         /* 空文字 */
    return 0;
}

int main(void) {
    if (test_midnight_matches_legacy_literals()) return 1;
    if (test_midnight_derived_colors_near_legacy()) return 1;
    if (test_mix_bounds()) return 1;
    if (test_recede_headroom_fallback()) return 1;
    if (test_preset_contrast()) return 1;
    if (test_best_on_picks_higher_contrast()) return 1;
    if (test_channel_step_monotonic_and_fixed_points()) return 1;
    if (test_theme_id_name_roundtrip()) return 1;
    if (test_color_parse_format_roundtrip()) return 1;
    printf("test_theme: すべて成功\n");
    return 0;
}

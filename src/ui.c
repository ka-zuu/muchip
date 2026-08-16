#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/font8x8/font8x8_basic.h"
#include "../vendor/misaki/misaki_gothic.h"
#include "log.h"

#define GLYPH_PX 8
#define FONT_COLS 16
#define FONT_ROWS 8
#define FONT_GLYPHS 128 /* font8x8_basic は U+0000-U+007F のみ */

/* 非ASCII(美咲フォント)アトラスのレイアウト。128列に固定し、
 * MISAKI_GLYPH_COUNT に応じて必要な行数だけ確保する。 */
#define CJK_COLS 128
#define CJK_ROWS ((MISAKI_GLYPH_COUNT + CJK_COLS - 1) / CJK_COLS)

/* UTF-8の1文字を読み、コードポイントを返してバイト数を返す。
 * 壊れたシーケンスは '?' に落とす(*out_cp='?', 読んだ分だけ進める)。
 * デコードそのものはbasic latin以外(日本語含む)も正しく扱う
 * (実際にどのグリフで描画できるかはui_text()側のアトラス検索次第。
 * ui.h のコメント参照)。*out_len==0 は文字列終端。 */
static int utf8_next(const char *s, int *out_cp) {
    unsigned char c0 = (unsigned char)s[0];
    if (c0 == 0) { *out_cp = 0; return 0; }
    if (c0 < 0x80) { *out_cp = c0; return 1; }

    int len, cp;
    if ((c0 & 0xE0) == 0xC0) { len = 2; cp = c0 & 0x1F; }
    else if ((c0 & 0xF0) == 0xE0) { len = 3; cp = c0 & 0x0F; }
    else if ((c0 & 0xF8) == 0xF0) { len = 4; cp = c0 & 0x07; }
    else { *out_cp = '?'; return 1; } /* 先頭バイトとして不正、または単独の継続バイト */

    for (int i = 1; i < len; i++) {
        unsigned char ci = (unsigned char)s[i];
        if (ci == 0 || (ci & 0xC0) != 0x80) {
            /* シーケンス途中で終端/不正バイト。読んだ分だけ進めて '?' を返す */
            *out_cp = '?';
            return i > 0 ? i : 1;
        }
        cp = (cp << 6) | (ci & 0x3F);
    }
    *out_cp = cp;
    return len;
}

static int build_font_atlas(ui_t *ui) {
    int atlas_w = FONT_COLS * GLYPH_PX;
    int atlas_h = FONT_ROWS * GLYPH_PX;
    Uint32 *pixels = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(Uint32));
    if (!pixels) {
        LOG_ERR("フォントアトラス用のメモリ確保に失敗しました");
        return -1;
    }

    for (int c = 0; c < FONT_GLYPHS; c++) {
        int cx = (c % FONT_COLS) * GLYPH_PX;
        int cy = (c / FONT_COLS) * GLYPH_PX;
        for (int row = 0; row < GLYPH_PX; row++) {
            unsigned char bits = font8x8_basic[c][row];
            for (int col = 0; col < GLYPH_PX; col++) {
                /* font8x8_basic はビット0=左端の桁。オン画素は全チャンネル
                 * 0xFF(白+不透明)にしておき、実際の色は
                 * SDL_SetTextureColorMod/AlphaMod で描画時に変える。
                 * 全バイト同値のためRGBA32のエンディアン差を気にしなくてよい。 */
                Uint32 px = (bits & (1u << col)) ? 0xFFFFFFFFu : 0x00000000u;
                pixels[(cy + row) * atlas_w + (cx + col)] = px;
            }
        }
    }

    ui->font_atlas = SDL_CreateTexture(ui->ren, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STATIC, atlas_w, atlas_h);
    if (!ui->font_atlas) {
        LOG_ERR("SDL_CreateTexture(font atlas) failed: %s", SDL_GetError());
        free(pixels);
        return -1;
    }
    SDL_UpdateTexture(ui->font_atlas, NULL, pixels, atlas_w * (int)sizeof(Uint32));
    SDL_SetTextureBlendMode(ui->font_atlas, SDL_BLENDMODE_BLEND);
    free(pixels);
    return 0;
}

/* build_font_atlas() の非ASCII版。美咲フォント(vendor/misaki)の
 * MISAKI_GLYPH_COUNT個のグリフを CJK_COLS 列のアトラスへ並べる。
 * 配列の添字がそのままアトラス上のスロット番号になる(別途マップ不要)。
 *
 * 失敗しても ui_init() 自体は失敗させない(呼び出し側で戻り値を見て
 * ui->cjk_atlas = NULL のまま続行する。メモリの厳しい環境で日本語
 * メタデータが理由でアプリごと起動しなくなる事態を避けるため)。 */
static int build_cjk_atlas(ui_t *ui) {
    int atlas_w = CJK_COLS * GLYPH_PX;
    int atlas_h = CJK_ROWS * GLYPH_PX;
    Uint32 *pixels = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(Uint32));
    if (!pixels) {
        LOG_WARN("日本語フォントアトラス用のメモリ確保に失敗しました。非ASCII文字は'?'で表示します");
        return -1;
    }

    for (int c = 0; c < MISAKI_GLYPH_COUNT; c++) {
        int cx = (c % CJK_COLS) * GLYPH_PX;
        int cy = (c / CJK_COLS) * GLYPH_PX;
        for (int row = 0; row < GLYPH_PX; row++) {
            unsigned char bits = misaki_glyphs[c].bits[row];
            for (int col = 0; col < GLYPH_PX; col++) {
                /* misaki_glyphs もfont8x8_basicと同じく「ビット0=左端の桁」
                 * (tools/make_misaki_font.py が生成時に揃えている)。 */
                Uint32 px = (bits & (1u << col)) ? 0xFFFFFFFFu : 0x00000000u;
                pixels[(cy + row) * atlas_w + (cx + col)] = px;
            }
        }
    }

    ui->cjk_atlas = SDL_CreateTexture(ui->ren, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, atlas_w, atlas_h);
    if (!ui->cjk_atlas) {
        LOG_WARN("SDL_CreateTexture(cjk atlas) failed: %s。非ASCII文字は'?'で表示します",
                  SDL_GetError());
        free(pixels);
        return -1;
    }
    SDL_UpdateTexture(ui->cjk_atlas, NULL, pixels, atlas_w * (int)sizeof(Uint32));
    SDL_SetTextureBlendMode(ui->cjk_atlas, SDL_BLENDMODE_BLEND);
    free(pixels);
    return 0;
}

/* misaki_glyphs から cp を二分探索する(配列はコードポイント昇順。
 * tools/make_misaki_font.py が保証する)。見つかればアトラス上のスロット
 * 番号(=配列の添字)を、無ければ-1を返す。
 * misaki_glyphs[].cp は unsigned short なので、範囲外(BMP超)は
 * 打ち切って誤ヒットを防ぐ。 */
static int misaki_find(int cp) {
    if (cp < 0 || cp > 0xFFFF) return -1;
    unsigned short target = (unsigned short)cp;
    int lo = 0, hi = MISAKI_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        unsigned short mcp = misaki_glyphs[mid].cp;
        if (mcp == target) return mid;
        if (mcp < target) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

int ui_init(ui_t *ui, int req_w, int req_h, int fullscreen) {
    memset(ui, 0, sizeof(*ui));
    /* Issue #27: config読み込みより前(あるいはconfigを渡さないテスト)でも
     * ui_color()が常に有効な色を返せるよう、既定でmidnightにしておく。
     * app側は起動時にapp_apply_theme()で実効テーマへ上書きする。 */
    theme_preset(THEME_MIDNIGHT, &ui->theme);

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            LOG_ERR("SDL_InitSubSystem(VIDEO) failed: %s", SDL_GetError());
            return -1;
        }
    }

    int w = req_w, h = req_h;
    if (w <= 0 || h <= 0) {
        /* SPEC 6.2: 起動時に実際の解像度を取得する。決め打ち禁止。 */
        SDL_DisplayMode mode;
        if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
            LOG_WARN("SDL_GetCurrentDisplayMode failed: %s (640x480にフォールバック)",
                      SDL_GetError());
            mode.w = 640;
            mode.h = 480;
        }
        w = mode.w;
        h = mode.h;
    }
    LOG_INFO("UI解像度: %dx%d%s", w, h, fullscreen ? " (fullscreen)" : "");

    Uint32 flags = SDL_WINDOW_SHOWN;
    if (fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    } else {
        /* --window での起動時のみリサイズ可能にする(実機は常にfullscreen)。
         * ホストでウィンドウを掴んで伸縮させ、複数解像度でのレイアウトを
         * 手軽に確認できるようにするため(P6)。 */
        flags |= SDL_WINDOW_RESIZABLE;
    }

    ui->win = SDL_CreateWindow("muChip", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                w, h, flags);
    if (!ui->win) {
        LOG_ERR("SDL_CreateWindow failed: %s", SDL_GetError());
        return -1;
    }

    /* ドット文字・矩形をボケさせず整数倍で拡大する。 */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    ui->ren = SDL_CreateRenderer(ui->win, -1, SDL_RENDERER_ACCELERATED);
    if (!ui->ren) {
        LOG_WARN("アクセラレータ付きレンダラを作成できません。ソフトウェアに切り替えます");
        ui->ren = SDL_CreateRenderer(ui->win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ui->ren) {
        LOG_ERR("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(ui->win);
        ui->win = NULL;
        return -1;
    }

    if (SDL_GetRendererOutputSize(ui->ren, &ui->screen_w, &ui->screen_h) != 0) {
        ui->screen_w = w;
        ui->screen_h = h;
    }

    if (build_font_atlas(ui) != 0) {
        SDL_DestroyRenderer(ui->ren);
        SDL_DestroyWindow(ui->win);
        ui->ren = NULL;
        ui->win = NULL;
        return -1;
    }
    build_cjk_atlas(ui); /* 失敗しても致命的にしない(コメント参照。'?'フォールバック) */

    /* SPEC 6.2: 640x480 を基準にスケールする。フォントサイズ・余白・行高
     * すべてをこの scale から導出し、以降どこにも座標を決め打ちしない。 */
    ui_metrics_compute(ui->screen_w, ui->screen_h, &ui->metrics);

    return 0;
}

void ui_handle_resize(ui_t *ui) {
    int w = ui->screen_w, h = ui->screen_h;
    if (SDL_GetRendererOutputSize(ui->ren, &w, &h) == 0) {
        ui->screen_w = w;
        ui->screen_h = h;
    }
    ui_metrics_compute(ui->screen_w, ui->screen_h, &ui->metrics);
    LOG_INFO("UI解像度を再計算しました: %dx%d", ui->screen_w, ui->screen_h);
}

void ui_shutdown(ui_t *ui) {
    /* SDL_Quit()自体は呼び出し側(main.c)の責務。ここではui_init()で
     * 確保したリソースだけを解放する(audio.c/player.cと同じ役割分担)。 */
    if (ui->font_atlas) SDL_DestroyTexture(ui->font_atlas);
    if (ui->cjk_atlas) SDL_DestroyTexture(ui->cjk_atlas);
    if (ui->ren) SDL_DestroyRenderer(ui->ren);
    if (ui->win) SDL_DestroyWindow(ui->win);
    memset(ui, 0, sizeof(*ui));
}

void ui_clear(ui_t *ui, SDL_Color color) {
    SDL_SetRenderDrawColor(ui->ren, color.r, color.g, color.b, color.a);
    SDL_RenderClear(ui->ren);
}

void ui_present(ui_t *ui) {
    SDL_RenderPresent(ui->ren);
}

void ui_fill_rect(ui_t *ui, ui_rect_t r, SDL_Color color) {
    SDL_SetRenderDrawColor(ui->ren, color.r, color.g, color.b, color.a);
    SDL_Rect sr = { r.x, r.y, r.w, r.h };
    SDL_RenderFillRect(ui->ren, &sr);
}

void ui_draw_rect(ui_t *ui, ui_rect_t r, SDL_Color color) {
    SDL_SetRenderDrawColor(ui->ren, color.r, color.g, color.b, color.a);
    SDL_Rect sr = { r.x, r.y, r.w, r.h };
    SDL_RenderDrawRect(ui->ren, &sr);
}

void ui_set_theme(ui_t *ui, const theme_t *theme) {
    ui->theme = *theme;
}

SDL_Color ui_color(const ui_t *ui, theme_role_t role) {
    theme_color_t c = theme_role_color(&ui->theme, role);
    SDL_Color out = { c.r, c.g, c.b, 255 };
    return out;
}

int ui_glyph_size_for(float scale, ui_text_size_t size) {
    int px = (int)(GLYPH_PX * (int)size * scale + 0.5f);
    if (px < GLYPH_PX) px = GLYPH_PX;
    return px;
}

int ui_glyph_size(const ui_t *ui, ui_text_size_t size) {
    return ui_glyph_size_for(ui->metrics.scale, size);
}

void ui_metrics_compute(int screen_w, int screen_h, ui_metrics_t *out) {
    float scale = SDL_min((float)screen_w / 640.0f, (float)screen_h / 480.0f);
    if (scale <= 0.0f) scale = 1.0f;
    out->scale = scale;

    out->glyph = ui_glyph_size_for(scale, UI_TEXT_BODY);

    /* pad は「クランプ後のUI_TEXT_SMALLのグリフサイズ/2」ではなく scale から
     * 直接導出する。以前の実装(ui_glyph_size(ui, UI_TEXT_SMALL)/2)は
     * ui_glyph_size_for() の8px下限のせいで scale∈[0.5, 1.18) の全域で
     * pad=4に固定されてしまい、320x240等の小さい解像度で余白が
     * 詰まりすぎる不具合があった(P6で発見)。 */
    out->pad = (int)(4.0f * scale + 0.5f);
    if (out->pad < 2) out->pad = 2;

    /* リスト行は行間を広げて読みやすくする(参考にしたランチャーUIの
     * 文字バランスに合わせた変更。Issue #41)。以前は glyph+pad で
     * 詰まっていた。 */
    out->line_h = out->glyph + out->pad * 2;

    /* ヘッダ/フッタは line_h からの導出をやめ、実際に載せる文字サイズ
     * 段階の合計から直接組む。ヘッダはタイトル(TITLE)+サブタイトル
     * (SMALL)の2段組、フッタは操作ヒント2行(SMALL x2)。
     * pad|行|pad|行|pad の3等分にする(上下と行間を同じ幅で揃える)。 */
    int title_px = ui_glyph_size_for(scale, UI_TEXT_TITLE);
    int small_px = ui_glyph_size_for(scale, UI_TEXT_SMALL);
    out->header_h = title_px + small_px + out->pad * 3;
    out->footer_h = small_px * 2 + out->pad * 3;

    /* header_h+footer_h が screen_h 以上になると、app.c の list_rect() が
     * 負の高さを返してしまう(極端に低い解像度で発生し得る)。
     * その場合は両者の合計を screen_h の半分に収める(比率1:1を維持)。 */
    if (screen_h > 0 && out->header_h + out->footer_h >= screen_h) {
        int budget = screen_h / 2;
        if (budget < 2) budget = screen_h > 1 ? screen_h - 1 : 0;
        out->header_h = budget / 2;
        out->footer_h = budget - out->header_h;
    }
}

int ui_text_width(const ui_t *ui, ui_text_size_t size, const char *s) {
    int px = ui_glyph_size(ui, size);
    int n = 0;
    const char *p = s;
    while (*p) {
        int cp;
        int adv = utf8_next(p, &cp);
        if (adv <= 0) break;
        p += adv;
        n++;
    }
    return n * px;
}

void ui_text(ui_t *ui, int x, int y, ui_text_size_t size, SDL_Color color, const char *s) {
    int px = ui_glyph_size(ui, size);
    SDL_SetTextureColorMod(ui->font_atlas, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(ui->font_atlas, color.a);
    if (ui->cjk_atlas) {
        SDL_SetTextureColorMod(ui->cjk_atlas, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(ui->cjk_atlas, color.a);
    }

    int cx = x;
    const char *p = s;
    while (*p) {
        int cp;
        int adv = utf8_next(p, &cp);
        if (adv <= 0) break;
        p += adv;

        /* ASCII(U+0000-U+007F)は font_atlas、それ以外は cjk_atlas から
         * 引く(Issue #29)。どちらにも無いコードポイントは '?' に
         * フォールバックする(ui.h のコメント参照)。 */
        SDL_Texture *atlas = ui->font_atlas;
        int glyph;
        int cols;
        if (cp >= 0 && cp < FONT_GLYPHS) {
            glyph = cp;
            cols = FONT_COLS;
        } else {
            int slot = ui->cjk_atlas ? misaki_find(cp) : -1;
            if (slot >= 0) {
                atlas = ui->cjk_atlas;
                glyph = slot;
                cols = CJK_COLS;
            } else {
                atlas = ui->font_atlas;
                glyph = '?';
                cols = FONT_COLS;
            }
        }
        SDL_Rect src = { (glyph % cols) * GLYPH_PX, (glyph / cols) * GLYPH_PX,
                          GLYPH_PX, GLYPH_PX };
        SDL_Rect dst = { cx, y, px, px };
        SDL_RenderCopy(ui->ren, atlas, &src, &dst);
        cx += px;
    }
}

void ui_text_clipped(ui_t *ui, int x, int y, int max_w, ui_text_size_t size,
                      SDL_Color color, const char *s) {
    int px = ui_glyph_size(ui, size);
    if (max_w < px) return; /* 1文字も入らない幅なら何も描かない */

    if (ui_text_width(ui, size, s) <= max_w) {
        ui_text(ui, x, y, size, color, s);
        return;
    }

    int ellipsis_w = px * 3;
    int budget = max_w - ellipsis_w;
    if (budget < 0) budget = 0;

    char buf[512];
    size_t bufpos = 0;
    int w = 0;
    const char *p = s;
    while (*p) {
        int cp;
        int adv = utf8_next(p, &cp);
        if (adv <= 0) break;
        if (w + px > budget) break;
        if (bufpos + (size_t)adv >= sizeof(buf) - 4) break;
        memcpy(buf + bufpos, p, (size_t)adv);
        bufpos += (size_t)adv;
        p += adv;
        w += px;
    }
    buf[bufpos] = 0;
    strcat(buf, "...");
    ui_text(ui, x, y, size, color, buf);
}

/* Issue #8: マーキー1周の内訳は「先頭で hold ms 静止 -> speed px/秒で
 * period px(文字列幅+折返し間隔)を左へ流す」の繰り返し。速度・間隔を
 * glyph_px の倍数で決めているのは、他のレイアウト量と同じく解像度非依存に
 * するため(SPEC 6.2)。速度は毎秒4文字分(px単位でも間隔と同じ glyph_px*4)。 */
int ui_marquee_offset(int text_w, int max_w, int glyph_px, Uint32 time_ms) {
    if (text_w <= max_w || glyph_px <= 0) return 0;

    int gap = glyph_px * 4;
    int period = text_w + gap;
    int speed = glyph_px * 4; /* px/秒 */

    const Uint32 hold_ms = 1000;
    Uint32 period_ms = (Uint32)(((long long)period * 1000) / speed);
    Uint32 cycle_ms = hold_ms + period_ms;
    if (cycle_ms == 0) return 0;

    /* time_ms(SDL_GetTicks())は約49日でUint32が一周するが、% で割った
     * 時点でその1フレームだけ表示がわずかに飛ぶ程度なので許容する。 */
    Uint32 t = time_ms % cycle_ms;
    if (t < hold_ms) return 0;

    long long off = ((long long)(t - hold_ms) * speed) / 1000;
    if (off >= period) off = period - 1;
    if (off < 0) off = 0;
    return (int)off;
}

void ui_text_scroll(ui_t *ui, int x, int y, int max_w, ui_text_size_t size,
                     SDL_Color color, const char *s, Uint32 time_ms) {
    int px = ui_glyph_size(ui, size);
    if (max_w < px) return;

    int text_w = ui_text_width(ui, size, s);
    if (text_w <= max_w) {
        ui_text(ui, x, y, size, color, s);
        return;
    }

    int gap = px * 4;
    int period = text_w + gap;
    int off = ui_marquee_offset(text_w, max_w, px, time_ms);

    /* この描画区間だけクリップ矩形を敷く。ui.c は他にクリップを使う箇所が
     * 無いが、呼び出し側が別の目的で敷いていた場合に備えて退避・復元する。 */
    SDL_bool had_clip = SDL_RenderIsClipEnabled(ui->ren);
    SDL_Rect prev_clip;
    if (had_clip) SDL_RenderGetClipRect(ui->ren, &prev_clip);

    SDL_Rect clip = { x, y, max_w, px };
    SDL_RenderSetClipRect(ui->ren, &clip);

    /* 1回目が画面外へ流れ出た直後、2回目(period 先)が右から現れる。
     * 途切れず周回して見えるよう常に2回描く。 */
    ui_text(ui, x - off, y, size, color, s);
    ui_text(ui, x - off + period, y, size, color, s);

    if (had_clip) SDL_RenderSetClipRect(ui->ren, &prev_clip);
    else SDL_RenderSetClipRect(ui->ren, NULL);
}

void ui_draw_progress(ui_t *ui, ui_rect_t r, float ratio, SDL_Color fg, SDL_Color bg) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    ui_fill_rect(ui, r, bg);
    ui_rect_t inner = r;
    inner.w = (int)(r.w * ratio);
    if (inner.w > 0) ui_fill_rect(ui, inner, fg);
}

ui_rect_t ui_header_rect(const ui_t *ui) {
    ui_rect_t r = { 0, 0, ui->screen_w, ui->metrics.header_h };
    return r;
}

ui_rect_t ui_header_title_row(const ui_t *ui) {
    int pad = ui->metrics.pad;
    int title_px = ui_glyph_size_for(ui->metrics.scale, UI_TEXT_TITLE);
    ui_rect_t r = { pad, pad, ui->screen_w - pad * 2, title_px };
    if (r.w < 0) r.w = 0;
    return r;
}

ui_rect_t ui_footer_rect(const ui_t *ui) {
    ui_rect_t r = { 0, ui->screen_h - ui->metrics.footer_h, ui->screen_w, ui->metrics.footer_h };
    return r;
}

/* Issue #41: ヘッダをタイトル(TITLE)+サブタイトル/カウンタ(SMALL)の
 * 2段組で描く。帯の高さの内訳は ui_metrics_compute() の
 * header_h = title_px + small_px + pad*3 と対応させてある
 * (pad|タイトル行|pad|サブ行|pad)。right_reserve はバッテリー等、
 * タイトル行の右端に既に確保済みの幅(SPEC 6.2の「右詰め要素を先に
 * 確保してから本文を縮める」規則に従い、呼び出し側が
 * draw_battery() 等の戻り値をそのまま渡す)。 */
void ui_draw_header(ui_t *ui, const ui_header_t *h) {
    ui_rect_t bar = ui_header_rect(ui);
    ui_fill_rect(ui, bar, h->bar_color);

    int pad = ui->metrics.pad;
    ui_rect_t title_row = ui_header_title_row(ui);
    if (h->title && h->title[0]) {
        int title_max_w = title_row.w - (h->right_reserve > 0 ? h->right_reserve + pad : 0);
        if (title_max_w < 0) title_max_w = 0;
        ui_text_clipped(ui, title_row.x, title_row.y, title_max_w,
                         UI_TEXT_TITLE, h->title_color, h->title);
    }

    int sub_y = title_row.y + title_row.h + pad;

    int counter_w = 0;
    if (h->counter && h->counter[0]) {
        counter_w = ui_text_width(ui, UI_TEXT_SMALL, h->counter);
        ui_text(ui, ui->screen_w - pad - counter_w, sub_y, UI_TEXT_SMALL,
                h->counter_color, h->counter);
    }
    if (h->subtitle && h->subtitle[0]) {
        int sub_max_w = ui->screen_w - pad * 2 - (counter_w > 0 ? counter_w + pad : 0);
        if (sub_max_w < 0) sub_max_w = 0;
        ui_text_clipped(ui, pad, sub_y, sub_max_w, UI_TEXT_SMALL, h->sub_color, h->subtitle);
    }
}

/* Issue #41: フッタを操作ヒント2行(主要=line1/補助=line2)で描く。
 * line2 が NULL/"" なら1行目だけ描く(帯の高さ自体は変えない。
 * ui_metrics_compute() 側は常に2行ぶんを確保している)。 */
void ui_draw_footer(ui_t *ui, const ui_footer_t *f) {
    ui_rect_t bar = ui_footer_rect(ui);
    ui_fill_rect(ui, bar, f->bar_color);

    int pad = ui->metrics.pad;
    int small_px = ui_glyph_size_for(ui->metrics.scale, UI_TEXT_SMALL);
    int max_w = ui->screen_w - pad * 2;

    if (f->line1 && f->line1[0]) {
        ui_text_clipped(ui, pad, bar.y + pad, max_w, UI_TEXT_SMALL, f->line1_color, f->line1);
    }
    if (f->line2 && f->line2[0]) {
        ui_text_clipped(ui, pad, bar.y + pad * 2 + small_px, max_w,
                         UI_TEXT_SMALL, f->line2_color, f->line2);
    }
}

void ui_draw_waveform(ui_t *ui, ui_rect_t r, const short *samples, int count,
                       SDL_Color fg, SDL_Color bg) {
    if (r.w <= 1 || r.h <= 1) return;

    ui_fill_rect(ui, r, bg);

    int mid = r.y + r.h / 2;
    if (!samples || count <= 0) {
        /* データが無いときは中央に0の線を引く(枠だけが残るより、
         * 「無音である」と読める方がよい)。 */
        SDL_SetRenderDrawColor(ui->ren, fg.r, fg.g, fg.b, fg.a);
        SDL_RenderDrawLine(ui->ren, r.x, mid, r.x + r.w - 1, mid);
        return;
    }

    /* 1画素につき1点を打つ。count が r.w より多ければ間引き、少なければ
     * 同じ点が横に伸びる(どちらでも折れ線として破綻しない)。
     *
     * 縦の正規化に WAVE_DISPLAY_GAIN を掛けている。GBS の出力は
     * libgme の既定ゲイン(Gbs_Emu は set_gain(1.2))でも short の
     * フルスケールにはまず届かず、素直に 32767 -> r.h/2 で写すと
     * 波形が中央に潰れて形が読めない。はみ出しはクランプする
     * (オシロというより「振れているのが分かる」ことを優先した簡易表示。
     *  SPEC 3.2 F-14 も「簡易ビジュアライザ」としている)。 */
#define WAVE_DISPLAY_GAIN 3
    int half = r.h / 2;
    SDL_SetRenderDrawColor(ui->ren, fg.r, fg.g, fg.b, fg.a);
    int prev_x = r.x;
    int prev_y = mid;
    for (int px = 0; px < r.w; px++) {
        int idx = (int)((long)px * count / r.w);
        if (idx >= count) idx = count - 1;
        int y = mid - (int)((long)samples[idx] * WAVE_DISPLAY_GAIN * half / 32768);
        if (y < r.y) y = r.y;
        if (y > r.y + r.h - 1) y = r.y + r.h - 1;
        if (px > 0) {
            SDL_RenderDrawLine(ui->ren, prev_x, prev_y, r.x + px, y);
        }
        prev_x = r.x + px;
        prev_y = y;
    }
}

int ui_save_screenshot(ui_t *ui, const char *path) {
    /* レンダラの実出力サイズで読み戻す(HiDPI考慮後の値。ui_init/ui_handle_resize
     * が screen_w/h に入れているものと同じ)。 */
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ui->screen_w, ui->screen_h,
                                                     32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        LOG_ERR("SDL_CreateRGBSurfaceWithFormat failed: %s", SDL_GetError());
        return -1;
    }
    if (SDL_RenderReadPixels(ui->ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                              s->pixels, s->pitch) != 0) {
        LOG_ERR("SDL_RenderReadPixels failed: %s", SDL_GetError());
        SDL_FreeSurface(s);
        return -1;
    }
    int rc = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    if (rc != 0) {
        LOG_ERR("SDL_SaveBMP(%s) failed: %s", path, SDL_GetError());
        return -1;
    }
    LOG_INFO("スクリーンショットを書き出しました: %s (%dx%d)", path, ui->screen_w, ui->screen_h);
    return 0;
}

int ui_list_visible_rows(const ui_t *ui, ui_rect_t r) {
    int row_h = ui->metrics.line_h;
    int visible = r.h / row_h;
    if (visible < 1) visible = 1;
    return visible;
}

void ui_list_clamp_scroll(const ui_t *ui, ui_rect_t r, int count, int selected, int *scroll) {
    int visible = ui_list_visible_rows(ui, r);

    if (count > 0) {
        if (selected < 0) selected = 0;
        if (selected >= count) selected = count - 1;

        if (*scroll > selected) *scroll = selected;
        if (*scroll < selected - visible + 1) *scroll = selected - visible + 1;

        int max_scroll = count - visible;
        if (max_scroll < 0) max_scroll = 0;
        if (*scroll > max_scroll) *scroll = max_scroll;
        if (*scroll < 0) *scroll = 0;
    } else {
        *scroll = 0;
    }
}

void ui_draw_list(ui_t *ui, ui_rect_t r, int count, int selected, int marked,
                   int *scroll, ui_list_item_fn item_fn, void *ctx) {
    int row_h = ui->metrics.line_h;
    int visible = ui_list_visible_rows(ui, r);

    /* selectedの範囲内クランプは下のハイライト判定(idx==selected)にも
     * 使うので、ui_list_clamp_scroll()に渡す一時値としてだけでなく
     * ここでも保持する(Issue #27でエディタ画面と共有する際、
     * scroll計算とハイライト計算の依存関係を分けすぎないための判断)。 */
    if (count > 0) {
        if (selected < 0) selected = 0;
        if (selected >= count) selected = count - 1;
    }
    ui_list_clamp_scroll(ui, r, count, selected, scroll);

    const SDL_Color sel_bg = ui_color(ui, THEME_ROLE_SEL);
    const SDL_Color sel_edge = ui_color(ui, THEME_ROLE_ACCENT);
    const SDL_Color mark_fg = ui_color(ui, THEME_ROLE_MARK);
    const SDL_Color normal_fg = ui_color(ui, THEME_ROLE_FG);
    /* Issue #27: sel_bgはユーザー編集(custom)でfgと近い値にされうるので、
     * 選択行の文字だけはコントラストの高い方をtheme_best_on()で選ぶ
     * (theme.h参照。全画面の自動補正はしない)。 */
    theme_color_t sel_fg_c = theme_best_on(theme_role_color(&ui->theme, THEME_ROLE_SEL),
                                            theme_role_color(&ui->theme, THEME_ROLE_FG),
                                            theme_role_color(&ui->theme, THEME_ROLE_BG));
    const SDL_Color sel_fg = { sel_fg_c.r, sel_fg_c.g, sel_fg_c.b, 255 };

    for (int row = 0; row < visible; row++) {
        int idx = *scroll + row;
        if (idx >= count) break;

        int y = r.y + row * row_h;
        if (idx == selected) {
            ui_rect_t hi = { r.x, y, r.w, row_h };
            ui_fill_rect(ui, hi, sel_bg);

            /* Issue #41: 選択行の左端に明るいアクセントバーを添える
             * (参考にしたランチャーUIの「行の始まりが立っている」印象を
             * 塗りつぶしだけの旧デザインへ足す)。文字は変わらず r.x+pad
             * から描くので、bar_w<pad なら重ならない。 */
            int bar_w = ui->metrics.pad / 2;
            if (bar_w < 2) bar_w = 2;
            ui_rect_t edge = { r.x, y, bar_w, row_h };
            ui_fill_rect(ui, edge, sel_edge);
        }

        SDL_Color fg = (idx == marked) ? mark_fg : (idx == selected ? sel_fg : normal_fg);
        const char *text = item_fn(ctx, idx);
        ui_text_clipped(ui, r.x + ui->metrics.pad, y + (row_h - ui->metrics.glyph) / 2,
                         r.w - ui->metrics.pad * 2, UI_TEXT_BODY, fg, text);
    }
}

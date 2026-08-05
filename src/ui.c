#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/font8x8/font8x8_basic.h"
#include "log.h"

#define GLYPH_PX 8
#define FONT_COLS 16
#define FONT_ROWS 8
#define FONT_GLYPHS 128 /* font8x8_basic は U+0000-U+007F のみ */

/* UTF-8の1文字を読み、コードポイントを返してバイト数を返す。
 * basic latin(U+0000-U+007F)以外・壊れたシーケンスは '?' に落とす
 * (ui.h のコメント参照。実行時依存を増やさないための割り切り)。
 * *out_len==0 は文字列終端。 */
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

int ui_init(ui_t *ui, int req_w, int req_h, int fullscreen) {
    memset(ui, 0, sizeof(*ui));

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
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    ui->win = SDL_CreateWindow("muGBS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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

    /* SPEC 6.2: 640x480 を基準にスケールする。フォントサイズ・余白・行高
     * すべてをこの scale から導出し、以降どこにも座標を決め打ちしない。 */
    ui->metrics.scale = SDL_min((float)ui->screen_w / 640.0f, (float)ui->screen_h / 480.0f);
    if (ui->metrics.scale <= 0.0f) ui->metrics.scale = 1.0f;

    ui->metrics.glyph = ui_glyph_size(ui, UI_TEXT_BODY);
    ui->metrics.pad = ui_glyph_size(ui, UI_TEXT_SMALL) / 2;
    if (ui->metrics.pad < 2) ui->metrics.pad = 2;
    ui->metrics.line_h = ui->metrics.glyph + ui->metrics.pad;
    ui->metrics.header_h = ui->metrics.line_h + ui->metrics.pad * 2;
    ui->metrics.footer_h = ui->metrics.line_h + ui->metrics.pad * 2;

    return 0;
}

void ui_shutdown(ui_t *ui) {
    /* SDL_Quit()自体は呼び出し側(main.c)の責務。ここではui_init()で
     * 確保したリソースだけを解放する(audio.c/player.cと同じ役割分担)。 */
    if (ui->font_atlas) SDL_DestroyTexture(ui->font_atlas);
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

int ui_glyph_size(const ui_t *ui, ui_text_size_t size) {
    int px = (int)(GLYPH_PX * (int)size * ui->metrics.scale + 0.5f);
    if (px < GLYPH_PX) px = GLYPH_PX;
    return px;
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

    int cx = x;
    const char *p = s;
    while (*p) {
        int cp;
        int adv = utf8_next(p, &cp);
        if (adv <= 0) break;
        p += adv;

        int glyph = (cp >= 0 && cp < FONT_GLYPHS) ? cp : '?';
        SDL_Rect src = { (glyph % FONT_COLS) * GLYPH_PX, (glyph / FONT_COLS) * GLYPH_PX,
                          GLYPH_PX, GLYPH_PX };
        SDL_Rect dst = { cx, y, px, px };
        SDL_RenderCopy(ui->ren, ui->font_atlas, &src, &dst);
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

void ui_draw_progress(ui_t *ui, ui_rect_t r, float ratio, SDL_Color fg, SDL_Color bg) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    ui_fill_rect(ui, r, bg);
    ui_rect_t inner = r;
    inner.w = (int)(r.w * ratio);
    if (inner.w > 0) ui_fill_rect(ui, inner, fg);
}

void ui_draw_list(ui_t *ui, ui_rect_t r, int count, int selected, int marked,
                   int *scroll, ui_list_item_fn item_fn, void *ctx) {
    int row_h = ui->metrics.line_h;
    int visible = r.h / row_h;
    if (visible < 1) visible = 1;

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

    const SDL_Color sel_bg = { 60, 90, 160, 255 };
    const SDL_Color mark_fg = { 255, 210, 90, 255 };
    const SDL_Color normal_fg = { 225, 225, 225, 255 };

    for (int row = 0; row < visible; row++) {
        int idx = *scroll + row;
        if (idx >= count) break;

        int y = r.y + row * row_h;
        if (idx == selected) {
            ui_rect_t hi = { r.x, y, r.w, row_h };
            ui_fill_rect(ui, hi, sel_bg);
        }

        SDL_Color fg = (idx == marked) ? mark_fg : normal_fg;
        const char *text = item_fn(ctx, idx);
        ui_text_clipped(ui, r.x + ui->metrics.pad, y + (row_h - ui->metrics.glyph) / 2,
                         r.w - ui->metrics.pad * 2, UI_TEXT_BODY, fg, text);
    }
}

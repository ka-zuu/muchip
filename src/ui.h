/* ui.h - 解像度非依存の描画プリミティブ。 (SPEC 6.2, P5)
 *
 * ここには「画面に何を出すか」のロジックは置かない(それは app.c の仕事)。
 * ui.c が提供するのは矩形・文字・リストといった部品と、それらを
 * 実機の解像度に応じて一貫してスケールするための ui_metrics_t だけ。
 *
 * フォントは外部ライブラリ(SDL2_ttf等)を使わず、内蔵のビットマップ
 * フォント(vendor/font8x8, パブリックドメイン)を起動時に1枚のテクスチャへ
 * 展開して使う。実機の sysroot には libSDL2 しか無く、SDL2_ttf の有無が
 * 保証できないため、新規の実行時依存を増やさない選択をしている。
 * UI文言は英語、GBSのメタデータもほぼASCIIのため、basic latin
 * (U+0000-U+007F) のみのカバレッジで実用上十分。それ以外の文字は '?' に
 * フォールバックする。
 */
#ifndef MUGBS_UI_H
#define MUGBS_UI_H

#include <SDL.h>

typedef struct {
    int x, y, w, h;
} ui_rect_t;

/* 文字サイズの3段階。実際のピクセルサイズは 8 * (int)size * ui->metrics.scale。
 * 座標のハードコード禁止(SPEC 6.2)の徹底のため、呼び出し側は常にこの enum
 * 経由でサイズを指定し、ピクセル数を直接書かない。 */
typedef enum {
    UI_TEXT_SMALL = 1,
    UI_TEXT_BODY  = 2,
    UI_TEXT_TITLE = 3,
} ui_text_size_t;

typedef struct {
    float scale;    /* min(w/640.0, h/480.0)。SPEC 6.2 の基準スケール */
    int   glyph;     /* UI_TEXT_BODY の1文字分(正方形)のピクセルサイズ */
    int   line_h;      /* リストなどの1行の高さ(glyph + pad) */
    int   pad;           /* 余白の基本単位 */
    int   header_h;       /* ヘッダ帯の高さ */
    int   footer_h;        /* フッタ帯の高さ */
} ui_metrics_t;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *font_atlas; /* 128グリフ(U+0000-U+007F)を並べた1枚のテクスチャ */
    int screen_w, screen_h;   /* レンダラの実際の出力サイズ(HiDPI考慮後) */
    ui_metrics_t metrics;
} ui_t;

/* ウィンドウ・レンダラ・フォントアトラスを初期化する。0で成功。
 * req_w/req_h が両方とも正なら、そのサイズのウィンドウを作る(--window、
 * ホストでのレイアウト確認用)。0以下なら SDL_GetCurrentDisplayMode() で
 * 検出した解像度を使う(実機での実際の起動時の動作)。
 * fullscreen が非0なら SDL_WINDOW_FULLSCREEN_DESKTOP を付ける。 */
int ui_init(ui_t *ui, int req_w, int req_h, int fullscreen);
void ui_shutdown(ui_t *ui);

void ui_clear(ui_t *ui, SDL_Color color);
void ui_present(ui_t *ui);

void ui_fill_rect(ui_t *ui, ui_rect_t r, SDL_Color color);
void ui_draw_rect(ui_t *ui, ui_rect_t r, SDL_Color color); /* 枠線のみ */

/* size 段階1つ分のグリフの正方形ピクセルサイズ。 */
int ui_glyph_size(const ui_t *ui, ui_text_size_t size);

/* UTF-8文字列の描画幅(ピクセル)。等幅フォントなので文字数*グリフ幅。 */
int ui_text_width(const ui_t *ui, ui_text_size_t size, const char *s);

void ui_text(ui_t *ui, int x, int y, ui_text_size_t size, SDL_Color color, const char *s);

/* max_w を超える場合、末尾を "..." に置き換えて収める。
 * ヘッダのパス表示・長い曲名など、幅が予測できない文字列に使う。 */
void ui_text_clipped(ui_t *ui, int x, int y, int max_w, ui_text_size_t size,
                      SDL_Color color, const char *s);

/* シークバー兼用の進捗バー。ratio は 0.0-1.0 にクランプされる。 */
void ui_draw_progress(ui_t *ui, ui_rect_t r, float ratio, SDL_Color fg, SDL_Color bg);

/* Browser/TrackList で共有するスクロール付きリスト描画。
 * item_fn(ctx, index) は行の表示文字列を返す(呼び出し中のみ有効な
 * ポインタでよい。内部で即座に描画する)。
 * selected はカーソル位置(背景ハイライト)。marked は追加の強調
 * (TrackListでの再生中トラック等。不要なら -1)。
 * *scroll は呼び出し間で保持され、selected が常に見える範囲に自動調整される。 */
typedef const char *(*ui_list_item_fn)(void *ctx, int index);
void ui_draw_list(ui_t *ui, ui_rect_t r, int count, int selected, int marked,
                   int *scroll, ui_list_item_fn item_fn, void *ctx);

#endif /* MUGBS_UI_H */

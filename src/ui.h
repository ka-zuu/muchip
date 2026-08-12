/* ui.h - 解像度非依存の描画プリミティブ。 (SPEC 6.2, P5)
 *
 * ここには「画面に何を出すか」のロジックは置かない(それは app.c の仕事)。
 * ui.c が提供するのは矩形・文字・リストといった部品と、それらを
 * 実機の解像度に応じて一貫してスケールするための ui_metrics_t だけ。
 *
 * フォントは外部ライブラリ(SDL2_ttf等)を使わず、内蔵のビットマップ
 * フォントを起動時にテクスチャへ展開して使う。実機の sysroot には
 * libSDL2 しか無く、SDL2_ttf の有無が保証できないため、新規の実行時
 * 依存を増やさない選択をしている。
 *
 * グリフは2枚のアトラスに分かれる(Issue #29):
 *   - font_atlas: ASCII(U+0000-U+007F, vendor/font8x8, パブリックドメイン)
 *   - cjk_atlas:  非ASCII(vendor/misaki, 8x8のJIS第1・第2水準相当。
 *                 フリーソフトウェア。vendor/misaki/README.md 参照)
 * NSF/GBSのヘッダがShift_JIS(CP932)の日本語だった実例があり
 * (src/text.c で取り込み時にUTF-8へ正規化している)、それを描画するために
 * 追加した。全角も1セル8px幅で描く(font8x8_basicと同じセルサイズにして
 * ui_text_width()等の等幅前提を崩さないため)。cjk_atlasに無いコード
 * ポイントは '?' にフォールバックする。 */
#ifndef MUGBS_UI_H
#define MUGBS_UI_H

#include <SDL.h>

#include "theme.h"

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
    SDL_Texture *cjk_atlas;  /* 非ASCII(美咲フォント)のグリフアトラス。
                              * 確保に失敗しても致命的にはせずNULLのまま
                              * 進める(該当文字は '?' フォールバック)。 */
    int screen_w, screen_h;   /* レンダラの実際の出力サイズ(HiDPI考慮後) */
    ui_metrics_t metrics;
    theme_t theme;   /* Issue #27: 現在の実効テーマ。ポインタでなく値で持つ
                       * (27バイトでコピーコストは無視できる。ライフタイム管理が
                       * 要らない)。ui_init()がmidnightで初期化するので、
                       * config読み込み前でも常に有効な色を返す。 */
} ui_t;

/* ウィンドウ・レンダラ・フォントアトラスを初期化する。0で成功。
 * req_w/req_h が両方とも正なら、そのサイズのウィンドウを作る(--window、
 * ホストでのレイアウト確認用)。0以下なら SDL_GetCurrentDisplayMode() で
 * 検出した解像度を使う(実機での実際の起動時の動作)。
 * fullscreen が非0なら SDL_WINDOW_FULLSCREEN_DESKTOP を付ける。 */
int ui_init(ui_t *ui, int req_w, int req_h, int fullscreen);
void ui_shutdown(ui_t *ui);

/* scale だけからグリフ1文字分の正方形ピクセルサイズを求める純関数。
 * ui_t を必要としないため、SDL_Init無しでテストできる(tests/test_ui_metrics.c)。
 * ui_glyph_size(ui, size) はこれの薄いラッパ。 */
int ui_glyph_size_for(float scale, ui_text_size_t size);

/* 出力解像度(ピクセル)から ui_metrics_t を導出する純関数。SPEC 6.2 の
 * scale = min(w/640, h/480) を基準に、フォントサイズ・余白・行高すべてを
 * ここから導出する。header_h+footer_h が screen_h を超えないよう
 * クランプする(極端に低い解像度で list_rect() の高さが負にならないため)。 */
void ui_metrics_compute(int screen_w, int screen_h, ui_metrics_t *out);

/* レンダラの出力サイズを取り直して metrics を再計算する。
 * レイアウトは毎フレーム metrics から導出されキャッシュを持たないため、
 * これを呼ぶだけで次フレームから新しい解像度になる
 * (SDL_WINDOWEVENT_SIZE_CHANGED を受けたときに呼ぶ。P6)。 */
void ui_handle_resize(ui_t *ui);

void ui_clear(ui_t *ui, SDL_Color color);
void ui_present(ui_t *ui);

void ui_fill_rect(ui_t *ui, ui_rect_t r, SDL_Color color);
void ui_draw_rect(ui_t *ui, ui_rect_t r, SDL_Color color); /* 枠線のみ */

/* Issue #27: 現在の実効テーマを差し替える(値コピー)。呼び出し側
 * (app_apply_theme())が毎フレーム呼ぶ想定なのでコストは軽く保ってある。 */
void ui_set_theme(ui_t *ui, const theme_t *theme);

/* ui->theme から role の色を引き、alpha=255 を付けて SDL_Color として返す。
 * 色の計算そのものはtheme.c(SDL非依存)にあり、ここはSDL型への変換のみ。 */
SDL_Color ui_color(const ui_t *ui, theme_role_t role);

/* size 段階1つ分のグリフの正方形ピクセルサイズ。 */
int ui_glyph_size(const ui_t *ui, ui_text_size_t size);

/* UTF-8文字列の描画幅(ピクセル)。等幅フォントなので文字数*グリフ幅。 */
int ui_text_width(const ui_t *ui, ui_text_size_t size, const char *s);

void ui_text(ui_t *ui, int x, int y, ui_text_size_t size, SDL_Color color, const char *s);

/* max_w を超える場合、末尾を "..." に置き換えて収める。
 * ヘッダのパス表示・長い曲名など、幅が予測できない文字列に使う。 */
void ui_text_clipped(ui_t *ui, int x, int y, int max_w, ui_text_size_t size,
                      SDL_Color color, const char *s);

/* Issue #8: max_w に収まらない文字列を横スクロール(マーキー)させるときの、
 * 先頭からのずらし量(px)。text_w <= max_w なら常に0(呼び出し側はこれを
 * 「動いていない」判定に使ってよい)。速度・停止時間・折返し間隔は
 * すべて glyph_px から導出するため解像度非依存(SPEC 6.2)。
 * SDL_Init 不要な純関数(ui_glyph_size_for と同じ方針。
 * tests/test_ui_metrics.c が検証する)。 */
int ui_marquee_offset(int text_w, int max_w, int glyph_px, Uint32 time_ms);

/* ui_text_clipped() の横スクロール版(Issue #8)。max_w に収まる場合は
 * ui_text() と同じ(スクロールしない)。収まらない場合は
 * ui_marquee_offset() が返すずらし量で s を左へ流し、末尾の後に間隔を
 * 空けて先頭が再び現れる周回表示にする。time_ms には呼び出し側の
 * SDL_GetTicks() を渡す。 */
void ui_text_scroll(ui_t *ui, int x, int y, int max_w, ui_text_size_t size,
                     SDL_Color color, const char *s, Uint32 time_ms);

/* シークバー兼用の進捗バー。ratio は 0.0-1.0 にクランプされる。 */
void ui_draw_progress(ui_t *ui, ui_rect_t r, float ratio, SDL_Color fg, SDL_Color bg);

/* 簡易オシロスコープ (F-14)。samples[0..count-1] を r の横幅いっぱいに
 * 線形写像した折れ線で描く。振幅は short のフルスケールを r の高さに
 * 対応させ、r からはみ出さないようクランプする。
 * count が r.w より大きくても小さくても破綻しない(解像度非依存。SPEC 6.2)。
 * 表示を安定させるためのトリガ(ゼロ交差検出)は呼び出し側の責務。 */
void ui_draw_waveform(ui_t *ui, ui_rect_t r, const short *samples, int count,
                       SDL_Color fg, SDL_Color bg);

/* 現在のレンダリング結果をBMPとして path へ書き出す(--screenshot、開発用)。
 * 0で成功、-1で失敗(理由はLOG_ERRに出る)。ui_present() の前に呼ぶこと
 * (SDL_RenderPresent 後のバックバッファの内容は未定義)。
 * 実機には /dev/fb0 のダンプという手段があるが、ホストにはそれが無いため
 * 複数解像度のレイアウトを目視確認する手段として用意している。 */
int ui_save_screenshot(ui_t *ui, const char *path);

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

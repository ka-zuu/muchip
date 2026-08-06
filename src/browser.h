/* browser.h - ファイルブラウザのモデル。 (SPEC 6.1 Browser画面, F-05)
 *
 * UI描画(ui.c)から切り離した純粋なディレクトリ走査ロジック。
 * app.c がこれとui_draw_list()を組み合わせてBrowser画面を作る。
 * SDLに依存しないため、tests/test_browser.c で単体テストできる。
 */
#ifndef MUGBS_BROWSER_H
#define MUGBS_BROWSER_H

typedef struct {
    char *name;   /* ディレクトリエントリ名(パスを含まない。malloc'd) */
    int   is_dir;
} browser_item_t;

typedef struct {
    char *cwd;              /* 現在のディレクトリの絶対/相対パス(malloc'd) */
    browser_item_t *items;  /* ディレクトリ優先 -> 大小文字無視の名前順でソート済み */
    int count;
    int selected;             /* items[] へのカーソル位置 */
    int scroll;                 /* ui_draw_list() が保持するスクロール位置 */
} browser_t;

/* path を開き、items[] を再構築する。0で成功。
 * 呼び出し側は最初の呼び出し前に *b をゼロ初期化しておくこと
 * (例: browser_t b = {0};)。以降は本関数が内部で前の状態を解放してから
 * 差し替えるので、呼び出し側で明示的なbrowser_close的な後始末は不要
 * (最終的にbrowser_free()を呼ぶまで)。
 * 失敗時(opendir不可等)は browser_t を触らない(呼び出し側は直前の
 * ディレクトリに留まれる)。
 * show_all が0なら .gbs/.gb/.m3u/.zip のみを列挙する(拡張子フィルタ、
 * SPEC 6.1)。非0ならすべてのファイルを列挙する
 * (config.ini show_all_files 相当。値の永続化自体はP6)。 */
int browser_open_dir(browser_t *b, const char *path, int show_all);

void browser_free(browser_t *b);

/* selected をdelta動かす(範囲内にクランプ)。 */
void browser_move(browser_t *b, int delta);

/* selected をdelta動かし、端を越えたら反対側へ折り返す(P9)。
 * D-Pad上下による1ステップのカーソル移動専用。ページ送りに使ってはならない
 * (何件も飛ぶ移動が折り返すと、押した回数によって着地位置が変わり
 * 分かりにくくなるため。SPEC 6.3 の注記を参照)。 */
void browser_move_wrap(browser_t *b, int delta);

/* 1ページ分(呼び出し側が渡す行数)動かす。browser_move()と同じくクランプする
 * (折り返さない)。 */
void browser_page(browser_t *b, int page_size);

/* 現在のディレクトリの親へ移動する。ルートに達している場合は何もしない
 * (0を返す)。移動できれば1を返す。show_allはbrowser_open_dir()へそのまま渡す。 */
int browser_up(browser_t *b, int show_all);

/* selected が指す項目がディレクトリなら、そこへ移動する(1を返す)。
 * ファイルなら何もせず0を返す(呼び出し側がbrowser_selected_path()で
 * パスを取り出して開く)。 */
int browser_enter(browser_t *b, int show_all);

/* items[index] の絶対パスを out に書く(cwdとの結合)。
 * index が範囲外なら空文字列を書いて-1を返す。
 * Player画面のファイル一覧(app.c)が、カーソルを動かさずに移動先候補の
 * パスを取り出すために使う(P9)。 */
int browser_path_at(const browser_t *b, int index, char *out, unsigned long out_size);

/* items[selected] の絶対パスを out に書く(cwdとの結合)。
 * count==0(空ディレクトリ)なら空文字列を書いて-1を返す。 */
int browser_selected_path(const browser_t *b, char *out, unsigned long out_size);

/* items[] から name に一致する要素を探し、見つかれば selected をそこへ
 * 動かして1を返す。見つからなければ0を返しselectedは変えない
 * (F-13: last_pathがファイルだった場合に、そのファイルへカーソルを
 * 合わせた状態でBrowserを開始するために使う)。 */
int browser_select_by_name(browser_t *b, const char *name);

#endif /* MUGBS_BROWSER_H */

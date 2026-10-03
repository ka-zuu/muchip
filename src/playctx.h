/* playctx.h - 「いま開いているファイルの次に何を開けるか」というコンテキスト。
 * (Issue #47)
 *
 * P9まではこれを app_t 内の2つ目の browser_t (player_list) で表現していたが、
 * Issue #47 でPlayer画面のファイル一覧UIそのものを撤去したため、UP/DOWNで
 * 前後のファイルへ送るためだけの薄いモデルへ置き換えた。将来のプレイリスト
 * 機能(Issue #18)は、ここに PLAYCTX_PLAYLIST という2つ目の kind を足し、
 * dir/names の代わりにプレイリストの参照ファイル一覧を持たせる形で
 * 拡張できるようにしてある(playctx_open_dir()相当のplaylist版を足し、
 * app.c側は「いまどちらのkindで開いたか」を憶えて呼び分けるだけで済む想定)。
 *
 * browser.c と違い、カーソル移動やページ送りは無い(このモデルは「次/前へ
 * 送る」だけを提供する。実際にファイルを開くのは app.c の app_open_path())。
 * SDLに依存しないため tests/test_playctx.c で単体テストできる。
 */
#ifndef MUGBS_PLAYCTX_H
#define MUGBS_PLAYCTX_H

typedef enum {
    PLAYCTX_NONE = 0,   /* 何も開いていない、またはコンテキストを構築できなかった
                            (一覧に無い場所から開いた等)。current は常に-1 */
    PLAYCTX_DIRECTORY,  /* 開いたファイルが属するディレクトリのファイル一覧
                            (拡張子フィルタはbrowser.c の has_music_ext()相当) */
} playctx_kind_t;

typedef struct {
    playctx_kind_t kind;
    char *dir;      /* PLAYCTX_DIRECTORYのときのディレクトリパス(malloc'd)。
                        それ以外はNULL */
    char **names;   /* ディレクトリ優先を除いた「ファイルのみ」の名前一覧
                        (malloc'd配列、各要素もmalloc'd)。名前順にソート済み
                        (browser.c の item_cmp と同じ大小文字無視の比較)。
                        所有権はこの構造体にあり、playctx_free()で解放する */
    int count;
    int current;    /* names[] 上のいま開いているファイルの添字。
                        一覧に無ければ-1(zip内から開いた・拡張子フィルタ外等) */
} playctx_t;

/* opened_path (app_open_path()に渡されたパスそのもの)の親ディレクトリを
 * browser_open_dir()相当のロジックで読み、ファイルだけを names[] に集める。
 * 0で成功(一覧が空でも0を返す。count==0として扱う)。ディレクトリを開けない
 * 場合は-1を返し、*ctx は前の状態のまま変更しない(呼び出し側は直前の
 * コンテキストを保持できる。browser_open_dir()と同じ契約)。
 *
 * force_rescanが0のとき、既に同じディレクトリを開いていれば readdir を
 * やり直さない(SDカード上での無駄な再走査を避ける。show_all_files切替時
 * だけforce_rescan=1で呼ぶ)。 */
int playctx_open_dir(playctx_t *ctx, const char *opened_path, int show_all,
                      int force_rescan);

/* current から delta 動かした先のファイルの絶対パスを out へ書く(dirとの
 * 結合)。端は反対側へ折り返す(browser_move_wrap()と同じ規約)。
 * current==-1(一覧に無い状態から呼ばれた)のときは、delta>0なら先頭、
 * delta<0なら末尾を返す。count<=0なら何もせず-1を返す。 */
int playctx_step_path(const playctx_t *ctx, int delta, char *out, unsigned long out_size);

void playctx_free(playctx_t *ctx);

#endif /* MUGBS_PLAYCTX_H */

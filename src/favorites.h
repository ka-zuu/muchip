/* favorites.h - お気に入り(Favorites)リストの永続化と照会。 (Issue #18)
 *
 * 固定の1本だけを持つプレイリスト。SDLにもlibgmeにも依存しないため
 * tests/test_favorites.c で単体テストできる。
 *
 * トラックは (container, source_key, track_index) の3つ組で同定する:
 *   container    app_open_path() に渡したパス(.gbs/.nsf/.spc/.m3u/.zip)
 *   source_key   playlist_source_t の zip_entry(zip内)か fs_path
 *   track_index  playlist_entry_t.track_index(m3u適用後のgmeの添字)
 * playlist_open_favorites()(playlist.h)は container ごとに既存の
 * open ロジックでソースを再構築し、source_key で該当ソースを選ぶ。
 * 同定が既存の再生経路と厳密に一致するのはこのため。
 *
 * ファイル形式: 1行1曲・タブ区切り・追加順。
 *   <container>\t<source_key>\t<track_index>\t<title(表示キャッシュ)>
 */
#ifndef MUGBS_FAVORITES_H
#define MUGBS_FAVORITES_H

typedef struct {
    char *container;   /* malloc'd */
    char *source_key;  /* malloc'd */
    int track_index;
    char *title;       /* malloc'd。表示用のキャッシュ(同定には使わない) */
} favorite_t;

typedef struct {
    favorite_t *items; /* malloc'd配列。所有権はこの構造体にあり、favorites_free()で解放する */
    int count;
} favorites_t;

/* お気に入りファイルの置き場所を決める。優先順は環境変数 MUCHIP_FAVORITES >
 * config_path と同じディレクトリの favorites.txt(config_path に '/' が無ければ
 * カレントディレクトリ)。テストが config を共有しても互いのお気に入りを
 * 壊さないよう、config_resolve_path() と同じ形で環境変数の上書きを持つ。 */
void favorites_resolve_path(char *out, unsigned long out_size, const char *config_path);

/* path を読む。ファイルが無ければ空リストで0を返す(初回起動)。
 * フィールド数や track_index が不正な行は警告して読み飛ばす
 * (ファイル自体は書き換えない)。読み込み・メモリ確保の失敗は-1で、
 * その場合 *fav は空のまま。*fav の以前の内容は破棄される。 */
int favorites_load(favorites_t *fav, const char *path);

/* path へ一時ファイル経由で保存する(書いてから rename。電源断対策)。0で成功。 */
int favorites_save(const favorites_t *fav, const char *path);

/* 一致する項目の添字。無ければ-1。 */
int favorites_find(const favorites_t *fav, const char *container,
                   const char *source_key, int track_index);

/* 既にあれば削除して0、無ければ末尾へ追加して1を返す。失敗(メモリ確保、
 * container/source_key にタブ・改行を含む等)は-1で *fav は変わらない。
 * title のタブ・改行は空白へ置き換えて保持する。 */
int favorites_toggle(favorites_t *fav, const char *container,
                     const char *source_key, int track_index, const char *title);

void favorites_free(favorites_t *fav);

#endif /* MUGBS_FAVORITES_H */

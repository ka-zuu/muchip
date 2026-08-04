/* archive.h - zip展開 (miniz)。 (SPEC 4.2, 5.3)
 *
 * 一時ファイルをディスクに書き出さない。zipはセッション中(archive_close
 * まで)開いたまま保持し、必要なエントリだけをその都度メモリに展開する。
 */
#ifndef MUGBS_ARCHIVE_H
#define MUGBS_ARCHIVE_H

#include <stddef.h>

typedef struct archive archive_t; /* 不透明型。miniz実装をここに隠蔽する */

/* zip_path を開く。0で成功。 */
int archive_open(const char *zip_path, archive_t **out);
void archive_close(archive_t *ar);

typedef struct {
    char *name;               /* zip内のエントリ名 (そのまま。malloc'd) */
    int index;                  /* archive_extract() に渡すzip内の添字 */
    int is_music;               /* .gbs/.gb/.nsf/.spc/.vgm 等、libgmeが開ける拡張子 */
    int is_m3u;                  /* .m3u */
    unsigned long long uncompressed_size;
} archive_entry_t;

/* zip内のファイル一覧(ディレクトリを除く)を拡張子で分類して返す。0で成功。 */
int archive_list(archive_t *ar, archive_entry_t **out_entries, int *out_count);
void archive_free_entries(archive_entry_t *entries, int count);

/* name をパス区切り(\ と /)・大小文字の揺れを吸収して検索し、
 * 見つかった zip 内エントリの添字(archive_extract に渡す値)を返す。
 * 見つからなければ -1。m3uが参照するファイル名の解決に使う (SPEC 5.3)。 */
int archive_find(archive_t *ar, const char *name);

/* index の指すエントリをメモリに展開する。
 * 展開後サイズが32MBを超える場合は拒否する(メモリ保護。SPEC 5.3)。
 * 呼び出し側が free() すること。0で成功。 */
int archive_extract(archive_t *ar, int index, void **out_data, size_t *out_size);

#endif /* MUGBS_ARCHIVE_H */

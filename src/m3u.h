/* m3u.h - 拡張M3U(SPEC 5.2)の薄い前処理。
 *
 * トラック番号(10進/16進)・時間(m:ss.mmm)・曲名などのフィールド解析は
 * 一切行わない。それらは常に libgme の M3u_Playlist パーサ
 * (gme_load_m3u_data 経由)に委譲する。自前で再実装しない (SPEC 5.2-1)。
 *
 * ここでやるのは「m3uを、参照ファイルが連続する区間(セグメント)に分割し、
 * 区間ごとに独立したm3uテキストを再構成する」ことだけ。
 * 単一ファイルしか参照しない典型的なGBS用m3uでは、セグメントは常に1つに
 * なるため、playlist.c 側で特別扱いの分岐が不要になる。
 */
#ifndef MUGBS_M3U_H
#define MUGBS_M3U_H

#include <stddef.h>

typedef struct {
    char *filename; /* この区間が参照するファイル名 (m3u記載のまま。malloc'd) */
    char *text;     /* 再構成されたm3uテキスト (NUL終端。malloc'd)。
                       元のヘッダコメント行 (#Game:等) は全セグメントに
                       複製して含める（gmeのM3u_Playlistがコメントから
                       game/artist等のメタ情報を拾うため）。 */
    size_t text_len; /* text の長さ (NUL含まず) */
} m3u_segment_t;

/* m3uテキストをセグメントに分割する。
 * 戻り値0で成功し out_segs/out_count を埋める。
 * 有効なエントリ行が1つも無ければ -1。
 * 解放は m3u_free_segments() で行うこと。 */
int m3u_split_segments(const char *text, size_t len,
                        m3u_segment_t **out_segs, int *out_count);

void m3u_free_segments(m3u_segment_t *segs, int count);

#endif /* MUGBS_M3U_H */

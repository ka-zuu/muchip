/* playlist.h - .gbs単体 / .gbs+同名.m3u / .m3u直接 / (P4で).zip の4入力を
 * 単一のデータモデルへ正規化する。 (SPEC 4.2, 5.2)
 *
 * 上位(player.c、将来のUI)はこの正規化されたモデルだけを見ればよく、
 * 入力がどの経路から来たかを知らなくてよい。
 */
#ifndef MUGBS_PLAYLIST_H
#define MUGBS_PLAYLIST_H

#include <stddef.h>

#include "config.h"

typedef struct {
    char *display_path; /* 表示・ログ用。P4ではzip内なら "rip.zip:Game.gbs" 形式になる */
    char *fs_path;       /* 実ファイルのパス。P3時点では常に非NULL */
    char *zip_entry;      /* zip内エントリ名。P3では常にNULL (P4で使用) */
    char *m3u_text;        /* このソース用に再構成されたm3uテキスト。
                               無ければNULL(m3u無しでファイル単体を列挙する場合) */
    size_t m3u_len;
} playlist_source_t; /* = 1回 Music_Emu を開く単位 (m3uのセグメントに対応) */

typedef struct {
    char *title;       /* 表示名。m3uの曲名があればそれ、無ければ "Track NN" */
    int   source_index; /* sources[] への添字 */
    int   track_index;  /* gme_start_track に渡す値 (0始まり) */
} playlist_entry_t;

typedef struct {
    playlist_source_t *sources;
    int source_count;
    playlist_entry_t *entries;
    int entry_count;
    char *game; /* 表示用ゲーム名。取得できた最初のソースの情報を使う。空文字列もあり得る */
} playlist_t;

/* path (.gbs単体 または .m3u) を開き、統一データモデルを構築する。
 * 0で成功し *out にプレイリストを設定する。
 *
 * - .gbs を直接指定した場合、同ディレクトリに同名の .m3u があれば
 *   自動で読み込む (SPEC 5.2-4, F-03)。無ければ全トラックを
 *   "Track 01".. と自動命名して列挙する (SPEC 5.2-3)。
 * - .m3u を直接指定した場合、参照ファイルごとにセグメント分割し
 *   (m3u.c)、それぞれを個別のソースとして開く。存在しないファイルを
 *   参照するセグメントは警告して読み飛ばす (T-13)。
 *
 * P3時点ではローカルファイルシステムのみを扱う。zip対応はP4。 */
int playlist_open(const char *path, const mugbs_config_t *config, playlist_t **out);

void playlist_free(playlist_t *pl);

#endif /* MUGBS_PLAYLIST_H */

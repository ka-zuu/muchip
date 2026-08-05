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

/* gme.h の前方宣言だけを使う。playlist.h の利用側(ui.c/app.c等)に
 * <gme/gme.h> への依存を強制しないため、フルインクルードはしない。 */
typedef struct gme_info_t gme_info_t;

typedef struct {
    char *display_path; /* 表示・ログ用。P4ではzip内なら "rip.zip:Game.gbs" 形式になる */
    char *fs_path;       /* 実ファイルのパス。P3時点では常に非NULL */
    char *zip_entry;      /* zip内エントリ名。P3では常にNULL (P4で使用) */
    char *m3u_text;        /* このソース用に再構成されたm3uテキスト。
                               無ければNULL(m3u無しでファイル単体を列挙する場合) */
    size_t m3u_len;

    /* P5: Player画面表示用のファイル単位メタデータ。
     * 最初のトラックの gme_track_info() から取得する(SPEC 6.1)。
     * 取得できなかった場合は空文字列("")になる(NULLにはしない)。 */
    char *author;
    char *copyright;
    char *system;
} playlist_source_t; /* = 1回 Music_Emu を開く単位 (m3uのセグメントに対応) */

typedef struct {
    char *title;       /* 表示名。m3uの曲名があればそれ、無ければ "Track NN" */
    int   source_index; /* sources[] への添字 */
    int   track_index;  /* gme_start_track に渡す値 (0始まり) */

    /* P5: シークバー・残り時間表示用。player.c の fade_start_ms() 相当を
     * playlist_effective_length_ms() に括り出し、スキャン時点で確定させる。 */
    int duration_ms;    /* フェード開始までの実効曲長(ms)。既定長フォールバック込み */
    int length_known;   /* 非0: info->length か loop構造から得た実測値。
                            0: default_length_sec によるフォールバック */
} playlist_entry_t;

typedef struct {
    playlist_source_t *sources;
    int source_count;
    playlist_entry_t *entries;
    int entry_count;
    char *game; /* 表示用ゲーム名。取得できた最初のソースの情報を使う。空文字列もあり得る */

    struct archive *archive; /* .zip から開いた場合のみ非NULL。セッション中保持し、
                                 sources[].zip_entry の実体展開に使う (P4)。
                                 所有権はplaylist_tにあり、playlist_free()で閉じる */
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

/* info(gme_track_info の結果) と config から、フェード開始時刻=実効曲長(ms)を
 * 判定する。player.c と playlist.c の双方が同じ判定を必要とするため
 * ここに集約する(元は player.c 内の static fade_start_ms())。
 *
 * gme_info_t.play_length は曲長不明時に -1 ではなく 150000(既定150秒)を
 * 返してしまう(PLAN.md 記載のSPECとの既知の乖離#1)。そのため
 * length(総曲長)とintro_length+loop_length(ループ構造)の有無で
 * 「本当に既知か」を判定し、どちらも無ければ config->default_length_sec
 * にフォールバックする (F-08)。
 * out_known に非NULLを渡すと、既知(非0)/フォールバック(0)の別を返す。 */
int playlist_effective_length_ms(const gme_info_t *info, const mugbs_config_t *cfg,
                                  int *out_known);

/* cfg->default_length_sec が変わったとき、フォールバックで曲長を決めていた
 * エントリ(length_known==0)だけ duration_ms を付け替える。実測値
 * (length_known!=0)には触らない。libgmeを呼び直さないので安価で、
 * ファイルを開き直す必要がない (P6 Settings画面用)。既に再生中のトラックの
 * 既に armed 済みのフェードはこれだけでは変わらない(次トラックから反映)。 */
void playlist_apply_default_length(playlist_t *pl, const mugbs_config_t *cfg);

#endif /* MUGBS_PLAYLIST_H */

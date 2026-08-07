/* playlist.h - 単体音楽ファイル(.gbs/.gb/.nsf/.nsfe) / 同名.m3u同梱 /
 * .m3u直接 / (P4で).zip の4入力を単一のデータモデルへ正規化する。
 * (SPEC 4.2, 5.2)
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
    int duration_ms;    /* フェード開始までの実効曲長(ms)。既定長フォールバック・
                            Issue #19のLength上書き込みの「今使うべき」値 */
    int length_known;   /* 非0: info->length か loop構造から得た実測値。
                            0: default_length_sec によるフォールバック */
    int natural_ms;      /* Issue #19: length_known!=0のときのみ意味を持つ、
                            上書き(length_override_sec)を無視した実測曲長(ms)。
                            Lengthをautoへ戻したときにこの値へ復帰するために
                            duration_msとは別に保持しておく。length_known==0なら0 */
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

/* path (.gbs/.gb/.nsf/.nsfe単体 または .m3u) を開き、統一データモデルを
 * 構築する。0で成功し *out にプレイリストを設定する。
 *
 * - 単体音楽ファイルを直接指定した場合、同ディレクトリに同名の .m3u が
 *   あれば自動で読み込む (SPEC 5.2-4, F-03)。無ければ全トラックを
 *   "Track 01".. と自動命名して列挙する (SPEC 5.2-3)。
 * - .m3u を直接指定した場合、参照ファイルごとにセグメント分割し
 *   (m3u.c)、それぞれを個別のソースとして開く。存在しないファイルを
 *   参照するセグメントは警告して読み飛ばす (T-13)。
 *
 * P3時点ではローカルファイルシステムのみを扱う。zip対応はP4。 */
int playlist_open(const char *path, const mugbs_config_t *config, playlist_t **out);

void playlist_free(playlist_t *pl);

/* info(gme_track_info の結果)から、上書き設定を無視した「素の」曲長(ms)を
 * 判定する。player.c と playlist.c の双方が同じ判定を必要とするため
 * ここに集約する(元は player.c 内の static fade_start_ms())。
 *
 * gme_info_t.play_length は曲長不明時に -1 ではなく 150000(既定150秒)を
 * 返してしまう(PLAN.md 記載のSPECとの既知の乖離#1)。そのため
 * length(総曲長)とintro_length+loop_length(ループ構造)の有無で
 * 「本当に既知か」を判定する。
 * out_known に非NULLを渡すと、既知(非0)/不明(0)の別を返す。不明な場合の
 * 戻り値は0(呼び出し側がdefault_length_secへフォールバックする)。 */
int playlist_natural_length_ms(const gme_info_t *info, int *out_known);

/* natural_ms/known(playlist_natural_length_ms()の結果)とconfigから、
 * フェード開始時刻=実効曲長(ms)を決定する。
 * cfg->length_override_sec が非0なら(Issue #19: ながさチェンジ)、known/
 * unknownを問わず全トラックをその秒数へ強制する (F-28)。0(auto)なら
 * 従来どおりknownならnatural_ms、そうでなければcfg->default_length_sec
 * にフォールバックする (F-08)。 */
int playlist_resolve_length_ms(int natural_ms, int known, const mugbs_config_t *cfg);

/* playlist_natural_length_ms() + playlist_resolve_length_ms() をまとめて
 * 呼ぶ薄いラッパ。player.c の start_track_at() 等、素の曲長を経由せず
 * 実効曲長だけを1回で得たい呼び出し元向けに残している。
 * out_known は playlist_natural_length_ms() と同じ意味(実測かどうか)を
 * 返す。上書き(length_override_sec)の有無とは独立。 */
int playlist_effective_length_ms(const gme_info_t *info, const mugbs_config_t *cfg,
                                  int *out_known);

/* cfg->default_length_sec / cfg->length_override_sec が変わったとき、
 * 全エントリの duration_ms を playlist_resolve_length_ms() で計算し直す。
 * natural_ms(実測値)自体は書き換えない(Lengthをautoへ戻したときに
 * 復元できるようにするため)。libgmeを呼び直さないので安価で、
 * ファイルを開き直す必要がない (P6 Settings画面用)。既に再生中のトラックの
 * 既に armed 済みのフェードはこれだけでは変わらない
 * (player_apply_config()が別途 fade_at_ms を追随させる。Issue #19)。 */
void playlist_apply_length_config(playlist_t *pl, const mugbs_config_t *cfg);

/* Issue #15: repeat_mode が REPEAT_ONE のときはフェードせずエンドレスに
 * 再生する(「この曲をずっと鳴らしっぱなしにしたい」という one の意図に
 * 合わせる)。length_ms(playlist_effective_length_ms()が返した名目の
 * フェード開始時刻)をそのまま gme_set_fade_msecs() の start に渡すか、
 * 負値にしてフェードそのものを無効化するかをここで一本化する。
 * 負の開始時刻を渡すとフェードが無効になることは、
 * vendor/game-music-emu/gme/Music_Emu.cpp の Music_Emu::play_() が
 * `fade_start >= 0` のときだけ handle_fade() を呼ぶことで保証されている
 * (set_fade()はmsec_to_samples()を通すだけで符号は保たれる)。
 * player.c の start_track_at() と player_apply_config() の双方が使う。 */
int playlist_fade_start_ms(int length_ms, const mugbs_config_t *cfg);

#endif /* MUGBS_PLAYLIST_H */

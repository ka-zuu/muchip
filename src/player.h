/* player.h - 再生状態機械。 (SPEC 4.2, 5.4)
 *
 * STOPPED --open--> LOADED --play--> PLAYING <-> PAUSED
 *                                       |
 *                                       +- track_ended -> next_track()
 *                                       +- user next/prev -> start_track()
 *
 * P3 でファイルをまたぐ遷移(m3uの複数ファイル参照)に対応するまでは、
 * 1つの Music_Emu (= 1ファイル) の中でのトラック送りのみを扱う。
 */
#ifndef MUGBS_PLAYER_H
#define MUGBS_PLAYER_H

#include <gme/gme.h>

#include "audio.h"
#include "config.h"

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_LOADED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_state_t;

typedef struct {
    mugbs_audio_t audio;
    Music_Emu *emu;        /* 現在開いている emu。所有権はここ (player) にある。
                               audio コールバックと共有するため、start_track/seek/
                               delete 等で触るときは必ず audio_lock/unlock で保護する */
    player_state_t state;
    mugbs_config_t config;

    int track_count;       /* 現在開いているファイルの全トラック数 */
    int track_index;       /* 現在のトラック (0始まり) */
} player_t;

/* SDLオーディオデバイスを開いて初期化する。0で成功。 */
int player_init(player_t *p, const mugbs_config_t *config);

/* 開いている emu があれば閉じ、オーディオデバイスを閉じる。 */
void player_shutdown(player_t *p);

/* path を開き、track_index (0始まり) を再生開始する。
 * P1時点では単体 .gbs/.nsf 等のみを想定し、m3u/zip は扱わない (P3/P4で対応)。
 * すでに何か再生中なら、その emu を破棄してから開き直す。 */
int player_open_and_play(player_t *p, const char *path, int track_index);

void player_toggle_pause(player_t *p);

/* オーディオコールバックが曲の終端(フェード完了)を検出済みなら非0。 */
int player_is_track_ended(player_t *p);

/* 次のトラックへ進む。リピートモード (F-11) を考慮する: (SPEC 5.4)
 *   REPEAT_NONE: 最終トラックの次で emu を閉じ STOPPED になる
 *   REPEAT_ONE : 常に現在のトラックを再開する
 *   REPEAT_ALL : 最終トラックの次で先頭に戻る
 * フェード中でも即座に切り替わる(gme_start_trackを呼ぶだけなので)。
 * 戻り値: 0=再生継続, -1=再生する曲がなくなり STOPPED になった。 */
int player_next_track(player_t *p);

/* 前のトラックへ戻る。先頭より前に戻る場合は REPEAT_ALL なら最終トラックへ、
 * それ以外は先頭に留まる（STOPPEDにはしない）。 */
int player_prev_track(player_t *p);

/* 現在のトラック内でシークする。 */
int player_seek(player_t *p, int msec);

#endif /* MUGBS_PLAYER_H */

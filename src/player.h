/* player.h - 再生状態機械。 (SPEC 4.2, 5.4)
 *
 * P1 時点では open + start_track + 一時停止までの最小実装。
 * next_track/prev_track/seek/リピートモード/フェード判定は P2 で追加する。
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

#endif /* MUGBS_PLAYER_H */

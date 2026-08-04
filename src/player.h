/* player.h - 再生状態機械。 (SPEC 4.2, 5.4)
 *
 * STOPPED --load_playlist--> LOADED --play_entry--> PLAYING <-> PAUSED
 *                                       |
 *                                       +- track_ended -> next_track()
 *                                       +- user next/prev -> play_entry()
 *
 * player は playlist_t の「エントリ列(entries[])」だけを見て再生する。
 * エントリが指す source (= ファイル) が現在開いているものと変われば
 * emu を閉じて開き直す (P3: ファイルをまたぐm3uへの対応)。
 * playlist_t の所有権は呼び出し側にあり、player は参照するだけ。
 */
#ifndef MUGBS_PLAYER_H
#define MUGBS_PLAYER_H

#include <gme/gme.h>

#include "audio.h"
#include "config.h"
#include "playlist.h"

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

    const playlist_t *playlist; /* 参照のみ。所有権は呼び出し側 */
    int current_source;          /* 現在開いている sources[] の添字。-1=未オープン */
    int current_entry;            /* 現在の entries[] の添字。-1=未選択 */
} player_t;

/* SDLオーディオデバイスを開いて初期化する。0で成功。 */
int player_init(player_t *p, const mugbs_config_t *config);

/* 開いている emu があれば閉じ、オーディオデバイスを閉じる。 */
void player_shutdown(player_t *p);

/* 再生対象のプレイリストを差し替える。開いていたemuは閉じる。
 * pl の所有権は呼び出し側のまま(player_shutdown/次のload_playlistまで
 * 呼び出し側が生存させ続けること)。まだ何も再生しない。 */
int player_load_playlist(player_t *p, const playlist_t *pl);

/* entries[entry_index] を再生開始する。source が現在と異なれば
 * emu を開き直す (m3uが複数ファイルを参照する場合。SPEC 5.4)。 */
int player_play_entry(player_t *p, int entry_index);

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

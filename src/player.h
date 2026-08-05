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

    /* 現在のトラックで gme_set_fade_msecs() に渡したフェード長(ms)。
     * player_current_duration_ms() が「本当に無音になる時刻」
     * (=entries[].duration_ms + fade_len_ms) を返すために保持する。 */
    int fade_len_ms;
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

/* 現在の再生位置(ms)。emuが無い/STOPPEDなら0。 (P5: シークバー表示用) */
int player_tell_ms(player_t *p);

/* 現在のエントリが実際に無音になる(gme_track_ended()が真になる)時刻(ms)。
 * playlist側が確定させた playlist_entry_t.duration_ms(フェード開始時刻)
 * に、再生開始時に設定したフェード長を足したもの。duration_msだけを
 * 返すと、フェード中(duration_ms 〜 この値の間)に経過時間が表示上の
 * 「合計時間」を追い越して見えてしまうため注意(実機確認で発見した不具合)。
 * 何も再生していなければ0。 (P5: シークバー・シーク上限表示用) */
int player_current_duration_ms(const player_t *p);

/* 音量を設定する (0-100)。範囲外は丸める。 (SPEC 6.3 D-Pad上下) */
void player_set_volume(player_t *p, int volume_0_100);

/* リピートモードを変更する。次回の player_next_track()/player_prev_track()
 * から反映される。 (SPEC 6.3 Player画面 Y ボタン) */
void player_set_repeat_mode(player_t *p, repeat_mode_t mode);

/* 開いているemuがあれば閉じてSTOPPEDにする。ブラウザへ戻る際に使う。
 * playlist自体は保持したまま(参照ポインタはクリアしない)にはしない -
 * 呼び出し側が別ファイルを開く前の後始末として使う想定。 */
void player_stop(player_t *p);

#endif /* MUGBS_PLAYER_H */

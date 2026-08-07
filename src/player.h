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
#include "shuffle.h"

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

    /* 参照のみ。所有権は呼び出し側(app_t、あるいはCLIハーネスのmain())にあり、
     * player_shutdown() まで生存させること。P5までは値コピーだったが、
     * 複数のコピーが独立に書き換えられて食い違う問題(P6で発覚)を避けるため
     * ポインタ化した。audioコールバックスレッドはこれを一切参照しないため
     * (audio.c が触るのは a->emu のみ)、ポインタ化しても
     * 新たなスレッド間共有は生じない。値を変えたら player_apply_config() を
     * 呼ぶこと。 */
    const mugbs_config_t *config;

    const playlist_t *playlist; /* 参照のみ。所有権は呼び出し側 */
    int current_source;          /* 現在開いている sources[] の添字。-1=未オープン */
    int current_entry;            /* 現在の entries[] の添字。-1=未選択 */

    /* config->shuffle が有効なときの再生順 (F-25, P10)。player_play_entry()
     * が呼ばれるたびに現在のエントリへ同期される(TrackListからのジャンプ・
     * L2/R2でのソース切替など、next/prev以外の経路で曲が変わっても
     * 追従する)ので、player_next_track()/player_prev_track()はここから
     * 前後を引くだけでよい。config->shuffle==0のときはorder==NULLを保つ。 */
    shuffle_t shuffle;

    /* 現在のトラックで gme_set_fade_msecs() に渡したフェード長(ms)。
     * player_current_duration_ms() が「本当に無音になる時刻」
     * (=entries[].duration_ms + fade_len_ms) を返すために保持する。 */
    int fade_len_ms;

    /* Issue #15: playlist_effective_length_ms() が算出した「名目の」
     * フェード開始時刻(ms)。REPEAT_ONE中はこれをそのまま
     * gme_set_fade_msecs() には渡さず playlist_fade_start_ms() 経由で
     * -1(フェード無効)にしているため、one を抜けたときに元の時刻へ
     * 復元できるようここへ保持しておく(player_apply_config()参照)。
     * Issue #19: length_override_sec の変更時は player_apply_config() が
     * entries[current_entry].duration_ms から読み直して更新する。 */
    int fade_at_ms;
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
 * config->shuffle (F-25, P10) が有効なときは「次」がシャッフル順になる
 * (REPEAT_ONEはシャッフルより優先し、常に同一トラックを再開する)。
 * フェード中でも即座に切り替わる(gme_start_trackを呼ぶだけなので)。
 * 戻り値: 0=再生継続, -1=再生する曲がなくなり STOPPED になった。 */
int player_next_track(player_t *p);

/* 前のトラックへ戻る。先頭より前に戻る場合は REPEAT_ALL なら最終トラックへ、
 * それ以外は先頭に留まる（STOPPEDにはしない）。shuffle有効時はシャッフル順を
 * 逆にたどる(直前にnextで進んだ場合はそれをちょうど巻き戻す)。 */
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
 * 何も再生していない場合に加え、REPEAT_ONEでフェードが無効(Issue #15)の
 * 場合も0を返す(=長さ不定。呼び出し側はこれを「エンドレス」の表示に使う。
 * app.cのdraw_player()参照)。 (P5: シークバー・シーク上限表示用) */
int player_current_duration_ms(const player_t *p);

/* p->config が指す値の変更を、いま反映できる範囲で反映する。
 * stereo_depth / eq_* は現在開いている emu へ即時(audio_lock で
 * 保護。Effects_Buffer::config() は再確保を行わないため再生中に呼んでも
 * 安全 -- vendor/game-music-emu/gme/Effects_Buffer.cpp 参照)。
 * repeat_mode のうち REPEAT_ONE への出入り(Issue #15)と
 * length_override_sec(Issue #19: ながさチェンジ)はいま鳴っている
 * トラックのフェード有無/開始時刻へ即時反映する(audio_lockで保護。
 * 詳細はplayer.c。呼び出し側は先に playlist_apply_length_config() で
 * entries[].duration_ms を更新してから呼ぶこと)。
 * それ以外(REPEAT_NONE<->ALLの切り替えやシャッフル)は次のトラック送りから、
 * default_length_sec/fade_length_ms は次に start_track_at() を通ったとき
 * (=次トラック)から自然に反映される(config はポインタなので player 側で
 * 改めて何かをコピーする必要がない)。
 * sample_rate はオーディオデバイスを開き直さないと反映されない
 * (P6では非対応。config.ini編集+再起動)。呼び出し側(Settings画面)で
 * 値を変更するたびに呼ぶこと。 */
void player_apply_config(player_t *p);

/* 直近の出力波形を古い順に out[0..n-1] へ書き出す (F-14 ビジュアライザ)。
 * n は AUDIO_SCOPE_SAMPLES 以下を推奨(超える分は0で埋まる)。
 * 再生していないときは無音(全0)が返る。 */
void player_snapshot_scope(player_t *p, short *out, int n);

/* 開いているemuがあれば閉じてSTOPPEDにする。ブラウザへ戻る際に使う。
 * playlist自体は保持したまま(参照ポインタはクリアしない)にはしない -
 * 呼び出し側が別ファイルを開く前の後始末として使う想定。 */
void player_stop(player_t *p);

#endif /* MUGBS_PLAYER_H */

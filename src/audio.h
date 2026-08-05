/* audio.h - SDL2オーディオコールバックとlibgmeの橋渡し。 (SPEC 4.2)
 *
 * gme_* API はスレッドセーフではない。オーディオコールバックはSDLの
 * オーディオスレッドから、それ以外の gme_* 呼び出し（start_track/seek/
 * track_info/delete等）はメインスレッドから行われるため、後者は必ず
 * audio_lock()/audio_unlock()（内部は SDL_LockAudioDevice/Unlock）で
 * コールバックの実行と排他する。 (SPEC 5.1 落とし穴2)
 */
#ifndef MUGBS_AUDIO_H
#define MUGBS_AUDIO_H

#include <SDL.h>
#include <gme/gme.h>

/* ビジュアライザ(F-14)が描く波形の点数。1画面ぶんの窓の長さは
 * AUDIO_SCOPE_SAMPLES * scope_stride フレーム。44100Hz・stride=3 なら
 * 約17msぶんで、チップチューンの矩形波(数百Hz)が数周期入る。 */
#define AUDIO_SCOPE_SAMPLES 256

typedef struct {
    SDL_AudioDeviceID dev;
    Music_Emu *emu;              /* コールバックが参照する再生対象。
                                     読み書きは常に audio_lock/unlock の内側か
                                     audio_set_emu() 経由で行うこと */
    SDL_atomic_t track_ended;    /* コールバックが gme_track_ended() を検出したら1を立てる */
    SDL_atomic_t volume;         /* 0-100。audio_set_volume() 経由でのみ変更する
                                     (SDL_atomic_tなのでコールバックからロック無しで読める) */

    /* F-14 ビジュアライザ用。コールバックが音量適用後の出力をモノラルへ
     * 落として間引いたものを書き込むリングバッファ。読み出しは
     * audio_snapshot_scope() 経由(内部で audio_lock する)。
     * SDL_atomic_t を使わないのは、単一の値ではなく配列全体の一貫性が
     * 必要なため -- 512バイトの memcpy でコールバックを止める時間は
     * 無視できる。 */
    short scope[AUDIO_SCOPE_SAMPLES];
    int scope_pos;               /* 次に書く位置(リングの先頭 = 最も古い点) */
    int scope_stride;            /* 何フレームおきに1点拾うか。audio_init() が決める */
    int scope_phase;             /* コールバックをまたいで間引き位相を保つ */
} mugbs_audio_t;

/* SDLオーディオデバイスを開く（開始時は一時停止状態）。0で成功。 */
int audio_init(mugbs_audio_t *a, int sample_rate);
void audio_shutdown(mugbs_audio_t *a);

/* オーディオコールバックとの排他区間。gme_start_track/gme_seek/gme_delete等
 * emu に触るメインスレッド側の操作は必ずこの区間で行う。 */
void audio_lock(mugbs_audio_t *a);
void audio_unlock(mugbs_audio_t *a);

/* 再生対象の emu を差し替える（内部でロックする）。
 * emu の所有権（open/deleteの責務）は呼び出し側が持つ。NULL可（無音になる）。 */
void audio_set_emu(mugbs_audio_t *a, Music_Emu *emu);

void audio_set_pause(mugbs_audio_t *a, int paused);

/* 音量を設定する(0-100)。範囲外はクランプする。
 * コールバックはgme_play()が生成したサンプルへ整数ゲインを掛けるだけの
 * ソフトウェアミキシングで実装する(SPEC には無いP5独自追加。F-12近傍の
 * 実用上の要求として、D-Pad上下に音量を割り当てるため)。 */
void audio_set_volume(mugbs_audio_t *a, int volume_0_100);

/* コールバックが曲終端を検出済みなら非0を返す。 */
int audio_poll_track_ended(mugbs_audio_t *a);
void audio_clear_track_ended(mugbs_audio_t *a);

/* 直近の波形を古い順に out[0..n-1] へ書き出す (F-14)。
 * n は AUDIO_SCOPE_SAMPLES 以下であること(超える分は0で埋める)。
 * 内部で audio_lock/unlock するため、オーディオコールバック実行中の
 * 中途半端なリングを読むことはない。描画スレッド(メインループ)から
 * 毎フレーム呼んでよい。 */
void audio_snapshot_scope(mugbs_audio_t *a, short *out, int n);

/* 波形リングを無音で埋める。一時停止・停止でコールバックが止まったときに
 * 直前の波形が凍りついたまま残るのを防ぐ。 */
void audio_clear_scope(mugbs_audio_t *a);

#endif /* MUGBS_AUDIO_H */

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

typedef struct {
    SDL_AudioDeviceID dev;
    Music_Emu *emu;              /* コールバックが参照する再生対象。
                                     読み書きは常に audio_lock/unlock の内側か
                                     audio_set_emu() 経由で行うこと */
    SDL_atomic_t track_ended;    /* コールバックが gme_track_ended() を検出したら1を立てる */
    SDL_atomic_t volume;         /* 0-100。audio_set_volume() 経由でのみ変更する
                                     (SDL_atomic_tなのでコールバックからロック無しで読める) */
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

#endif /* MUGBS_AUDIO_H */

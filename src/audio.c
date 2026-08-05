#include "audio.h"

#include <string.h>

#include "log.h"

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    mugbs_audio_t *a = (mugbs_audio_t *)userdata;

    /* len はバイト数。gme_play() の第2引数は「ステレオインタリーブされた
     * short の個数」であり、フレーム数でもバイト数でもない。
     * len/4（フレーム数）を渡すと倍速再生になる。 (SPEC 5.1 落とし穴1) */
    int count = len / (int)sizeof(short);

    if (!a->emu) {
        memset(stream, 0, (size_t)len);
        return;
    }

    gme_err_t err = gme_play(a->emu, count, (short *)stream);
    if (err) {
        LOG_ERR("gme_play: %s", err);
        memset(stream, 0, (size_t)len);
        return;
    }

    /* ソフトウェア音量。100(既定)のときは乗算を一切行わず、
     * 従来(P1〜P4)と完全に同一の出力にする。 */
    int volume = SDL_AtomicGet(&a->volume);
    if (volume < 100) {
        short *samples = (short *)stream;
        for (int i = 0; i < count; i++) {
            samples[i] = (short)((samples[i] * volume) / 100);
        }
    }

    if (gme_track_ended(a->emu)) {
        SDL_AtomicSet(&a->track_ended, 1);
    }
}

int audio_init(mugbs_audio_t *a, int sample_rate) {
    memset(a, 0, sizeof(*a));
    SDL_AtomicSet(&a->track_ended, 0);
    SDL_AtomicSet(&a->volume, 100);

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            LOG_ERR("SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
            return -1;
        }
    }

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = a;

    /* allow_change=0: gme_play() は要求したフォーマット固定で
     * サンプルを生成するため、実デバイスに勝手に合わせ変えさせない。 */
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (a->dev == 0) {
        LOG_ERR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return -1;
    }

    if (have.format != AUDIO_S16SYS || have.channels != 2) {
        LOG_WARN("要求したオーディオフォーマットと異なる形式が割り当てられました "
                 "(format=%d ch=%d)", have.format, have.channels);
    }

    /* SDL_OpenAudioDevice はデバイスを一時停止状態で返す。
     * 実際の再生開始は audio_set_pause(a, 0) を呼んだ側の責務。 */
    return 0;
}

void audio_shutdown(mugbs_audio_t *a) {
    if (a->dev) {
        SDL_CloseAudioDevice(a->dev);
        a->dev = 0;
    }
}

void audio_lock(mugbs_audio_t *a) {
    SDL_LockAudioDevice(a->dev);
}

void audio_unlock(mugbs_audio_t *a) {
    SDL_UnlockAudioDevice(a->dev);
}

void audio_set_emu(mugbs_audio_t *a, Music_Emu *emu) {
    audio_lock(a);
    a->emu = emu;
    SDL_AtomicSet(&a->track_ended, 0);
    audio_unlock(a);
}

void audio_set_pause(mugbs_audio_t *a, int paused) {
    SDL_PauseAudioDevice(a->dev, paused ? 1 : 0);
}

void audio_set_volume(mugbs_audio_t *a, int volume_0_100) {
    if (volume_0_100 < 0) volume_0_100 = 0;
    if (volume_0_100 > 100) volume_0_100 = 100;
    SDL_AtomicSet(&a->volume, volume_0_100);
}

int audio_poll_track_ended(mugbs_audio_t *a) {
    return SDL_AtomicGet(&a->track_ended);
}

void audio_clear_track_ended(mugbs_audio_t *a) {
    SDL_AtomicSet(&a->track_ended, 0);
}

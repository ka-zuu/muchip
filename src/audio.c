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
        memset(a->scope, 0, sizeof(a->scope)); /* 波形表示も無音に落とす (F-14) */
        return;
    }

    gme_err_t err = gme_play(a->emu, count, (short *)stream);
    if (err) {
        LOG_ERR("gme_play: %s", err);
        memset(stream, 0, (size_t)len);
        memset(a->scope, 0, sizeof(a->scope));
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

    /* F-14: 音量適用後(= 実際にスピーカーへ出る波形)をモノラルへ落として
     * 間引き、リングへ積む。ここは audio_lock の内側と同じ排他区間
     * (コールバック実行中は audio_snapshot_scope() が待たされる)。 */
    short *frames = (short *)stream;
    int frame_count = count / 2;
    int phase = a->scope_phase;
    int pos = a->scope_pos;
    for (int i = 0; i < frame_count; i++) {
        if (++phase >= a->scope_stride) {
            phase = 0;
            /* L/Rを平均する前に個別に>>1しておくと、両chがフルスケール
             * 近くでも中間結果がshortを溢れない。 */
            a->scope[pos] = (short)((frames[i * 2] >> 1) + (frames[i * 2 + 1] >> 1));
            if (++pos >= AUDIO_SCOPE_SAMPLES) pos = 0;
        }
    }
    a->scope_phase = phase;
    a->scope_pos = pos;

    if (gme_track_ended(a->emu)) {
        SDL_AtomicSet(&a->track_ended, 1);
    }
}

int audio_init(mugbs_audio_t *a, int sample_rate) {
    memset(a, 0, sizeof(*a));
    SDL_AtomicSet(&a->track_ended, 0);
    SDL_AtomicSet(&a->volume, 100);

    /* F-14: 実効12kHz程度まで間引く。GBのパルス波は最も高い設定でも
     * 数kHzなので、これで波形の形は保ったまま1画面(256点)に
     * 数周期分が収まる。サンプルレートによらず見た目の時間窓を
     * 一定に保つため、レートから求める。 */
    a->scope_stride = sample_rate / 12000;
    if (a->scope_stride < 1) a->scope_stride = 1;

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

void audio_snapshot_scope(mugbs_audio_t *a, short *out, int n) {
    if (n > AUDIO_SCOPE_SAMPLES) {
        memset(out + AUDIO_SCOPE_SAMPLES, 0,
               sizeof(short) * (size_t)(n - AUDIO_SCOPE_SAMPLES));
        n = AUDIO_SCOPE_SAMPLES;
    }
    if (n <= 0) return;

    audio_lock(a);
    /* scope_pos は「次に書く位置」= リング上で最も古い点。そこから
     * 折り返して古い順に並べ直す(描画側は素直な配列として扱える)。
     * 直近 n 点だけが欲しいので、末尾から n 点を取る。 */
    int start = (a->scope_pos - n) % AUDIO_SCOPE_SAMPLES;
    if (start < 0) start += AUDIO_SCOPE_SAMPLES;
    int first = AUDIO_SCOPE_SAMPLES - start;
    if (first > n) first = n;
    memcpy(out, a->scope + start, sizeof(short) * (size_t)first);
    if (first < n) {
        memcpy(out + first, a->scope, sizeof(short) * (size_t)(n - first));
    }
    audio_unlock(a);
}

void audio_clear_scope(mugbs_audio_t *a) {
    audio_lock(a);
    memset(a->scope, 0, sizeof(a->scope));
    audio_unlock(a);
}

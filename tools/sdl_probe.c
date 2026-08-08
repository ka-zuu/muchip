/* sdl_probe.c - muOS実機でのSDL2動作確認用の使い捨て診断ツール。
 *
 * muChip本体の一部ではない。「実機からSDL2を抜かずに、Debianのarm64向け
 * SDL2をクロスビルドして動的リンクしたバイナリが実機で動くか」を
 * 検証するための最小プログラム。 (SPEC 8.3の前提を実機で確認する)
 *
 * やること:
 *   1. ビデオ/オーディオサブシステムを初期化し、実際に選ばれた
 *      ドライバ名(KMSDRM/fbdev/dummy等)を表示する
 *   2. ウィンドウを開いて数秒間、色を変えながら表示する
 *   3. 短いビープ音(矩形波)を鳴らす
 *   4. 各段階の成否を明示的にログ出力する(SSH越しにlog.txtを見て判断できるように)
 *
 * 成功/失敗の判定は最終的な標準出力の "PROBE RESULT: ..." 行を見る。
 */
#include <SDL.h>
#include <math.h>
#include <stdio.h>

#define SAMPLE_RATE 44100
#define BEEP_HZ 440.0
#define BEEP_SECONDS 2

static double g_phase = 0.0;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    int n = len / (int)sizeof(Sint16) / 2; /* フレーム数 (ステレオ) */
    double step = 2.0 * M_PI * BEEP_HZ / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        Sint16 s = (Sint16)(sin(g_phase) * 12000.0);
        out[i * 2] = s;
        out[i * 2 + 1] = s;
        g_phase += step;
        if (g_phase > 2.0 * M_PI) g_phase -= 2.0 * M_PI;
    }
}

int main(void) {
    int video_ok = 0, window_ok = 0, audio_ok = 0;

    printf("=== sdl_probe: 開始 ===\n");
    printf("SDL_GetPlatform: %s\n", SDL_GetPlatform());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init 失敗: %s\n", SDL_GetError());
    } else {
        printf("SDL_Init 成功\n");
        printf("  video driver = %s\n", SDL_GetCurrentVideoDriver());
        video_ok = 1;
    }

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (video_ok) {
        SDL_DisplayMode mode;
        if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
            printf("  display mode = %dx%d @ %dHz\n", mode.w, mode.h, mode.refresh_rate);
        } else {
            printf("  SDL_GetCurrentDisplayMode 失敗: %s\n", SDL_GetError());
            mode.w = 640;
            mode.h = 480;
        }

        win = SDL_CreateWindow("sdl_probe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                mode.w, mode.h, SDL_WINDOW_SHOWN);
        if (!win) {
            printf("SDL_CreateWindow 失敗: %s\n", SDL_GetError());
        } else {
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
            if (!ren) {
                printf("  ACCELERATEDレンダラ失敗、SOFTWAREにフォールバック: %s\n", SDL_GetError());
                ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
            }
            if (!ren) {
                printf("SDL_CreateRenderer 失敗: %s\n", SDL_GetError());
            } else {
                printf("SDL_CreateWindow / SDL_CreateRenderer 成功\n");
                window_ok = 1;
            }
        }
    }

    SDL_AudioDeviceID dev = 0;
    if (video_ok) { /* SDL_Init(AUDIO)は上でまとめて呼んでいる */
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 2048;
        want.callback = audio_callback;

        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (dev == 0) {
            printf("SDL_OpenAudioDevice 失敗: %s\n", SDL_GetError());
        } else {
            printf("SDL_OpenAudioDevice 成功: driver=%s freq=%d format=%d channels=%d\n",
                   SDL_GetCurrentAudioDriver(), have.freq, have.format, have.channels);
            audio_ok = 1;
        }
    }

    /* 3色を1秒ずつ表示しつつ、途中でビープを鳴らす */
    if (window_ok) {
        SDL_Color colors[3] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
        for (int i = 0; i < 3; i++) {
            if (i == 1 && audio_ok) {
                printf("ビープ再生開始 (%d秒)\n", BEEP_SECONDS);
                SDL_PauseAudioDevice(dev, 0);
            }
            Uint32 t0 = SDL_GetTicks();
            while (SDL_GetTicks() - t0 < 1000) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) { /* イベントは捨てる。閉じるボタン等は無視してよい */
                }
                SDL_SetRenderDrawColor(ren, colors[i].r, colors[i].g, colors[i].b, 255);
                SDL_RenderClear(ren);
                SDL_RenderPresent(ren);
                SDL_Delay(16);
            }
        }
        if (audio_ok) {
            SDL_Delay((BEEP_SECONDS - 1) * 1000);
            SDL_PauseAudioDevice(dev, 1);
            printf("ビープ再生終了\n");
        }
    }

    if (dev) SDL_CloseAudioDevice(dev);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    if (video_ok) SDL_Quit();

    printf("=== sdl_probe: 終了 ===\n");
    printf("PROBE RESULT: video_init=%s window=%s audio=%s\n",
           video_ok ? "OK" : "NG", window_ok ? "OK" : "NG", audio_ok ? "OK" : "NG");

    return (video_ok && window_ok && audio_ok) ? 0 : 1;
}

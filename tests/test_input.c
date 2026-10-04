/* test_input.c - input.c の Y 単押し(INPUT_Y_TAP)判定の単体テスト。 (Issue #53)
 *
 * SDL_PushEvent で合成したキーボードイベントを input_poll() に通す。
 * Yは Y+D-pad コンボの修飾キーでもあるため、「コンボを使わずに離した
 * ときだけ」INPUT_Y_TAP を返すことを確認する(コンボ後に離すと
 * お気に入りが誤爆する)。キーボードの 'S' が Y、方向キーが D-pad。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "input.h"
#include "test_util.h"

static void push_key(Uint32 type, SDL_Keycode sym, int repeat) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key.keysym.sym = sym;
    ev.key.repeat = (Uint8)repeat;
    SDL_PushEvent(&ev);
}

/* 保留中のイベントをすべて処理し、INPUT_NONE以外のアクションを順に out へ詰める。 */
static int drain(input_t *in, input_action_t *out, int max) {
    int n = 0;
    input_action_t a;
    while (input_poll(in, &a)) {
        if (a != INPUT_NONE && n < max) out[n++] = a;
    }
    return n;
}

static int count_of(const input_action_t *acts, int n, input_action_t want) {
    int c = 0;
    for (int i = 0; i < n; i++) c += acts[i] == want;
    return c;
}

/* Yだけ押して離す -> 押下でINPUT_Y、離した時にINPUT_Y_TAPが1回 */
static int test_plain_tap(input_t *in) {
    input_action_t acts[8];
    push_key(SDL_KEYDOWN, SDLK_s, 0);
    push_key(SDL_KEYUP, SDLK_s, 0);
    int n = drain(in, acts, 8);
    CHECK(n == 2);
    CHECK(acts[0] == INPUT_Y);
    CHECK(acts[1] == INPUT_Y_TAP);
    return 0;
}

/* Y+方向でコンボを使ったら、離してもタップ扱いにしない */
static int test_combo_suppresses_tap(input_t *in) {
    input_action_t acts[8];
    push_key(SDL_KEYDOWN, SDLK_s, 0);
    push_key(SDL_KEYDOWN, SDLK_RIGHT, 0);
    push_key(SDL_KEYUP, SDLK_RIGHT, 0);
    push_key(SDL_KEYUP, SDLK_s, 0);
    int n = drain(in, acts, 8);
    CHECK(count_of(acts, n, INPUT_Y_RIGHT) == 1);
    CHECK(count_of(acts, n, INPUT_Y_TAP) == 0);
    return 0;
}

/* コンボの後に改めてYだけ押して離せばタップになる(フラグが押下ごとに戻る) */
static int test_tap_after_combo(input_t *in) {
    input_action_t acts[8];
    push_key(SDL_KEYDOWN, SDLK_s, 0);
    push_key(SDL_KEYUP, SDLK_s, 0);
    int n = drain(in, acts, 8);
    CHECK(count_of(acts, n, INPUT_Y_TAP) == 1);
    return 0;
}

/* Yの押しっぱなし(OSキーリピート)でコンボ済みフラグが戻らない */
static int test_key_repeat_keeps_combo_flag(input_t *in) {
    input_action_t acts[8];
    push_key(SDL_KEYDOWN, SDLK_s, 0);
    push_key(SDL_KEYDOWN, SDLK_UP, 0);
    push_key(SDL_KEYDOWN, SDLK_s, 1); /* リピート */
    push_key(SDL_KEYUP, SDLK_s, 0);
    int n = drain(in, acts, 8);
    CHECK(count_of(acts, n, INPUT_Y_UP) == 1);
    CHECK(count_of(acts, n, INPUT_Y_TAP) == 0);
    return 0;
}

/* リピートだけが続いても(コンボ無し)、離した時のタップは1回だけ */
static int test_key_repeat_without_combo_taps_once(input_t *in) {
    input_action_t acts[8];
    push_key(SDL_KEYDOWN, SDLK_s, 0);
    push_key(SDL_KEYDOWN, SDLK_s, 1);
    push_key(SDL_KEYDOWN, SDLK_s, 1);
    push_key(SDL_KEYUP, SDLK_s, 0);
    int n = drain(in, acts, 8);
    CHECK(count_of(acts, n, INPUT_Y_TAP) == 1);
    return 0;
}

/* Yを押していない時の方向キーはコンボにならず、Y_TAPも出ない */
static int test_dpad_without_y(input_t *in) {
    input_action_t acts[8];
    push_key(SDL_KEYDOWN, SDLK_RIGHT, 0);
    push_key(SDL_KEYUP, SDLK_RIGHT, 0);
    int n = drain(in, acts, 8);
    CHECK(n == 1);
    CHECK(acts[0] == INPUT_RIGHT);
    return 0;
}

int main(void) {
    /* 映像/音声は使わない。イベントキューとゲームコントローラ初期化だけ。 */
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init(EVENTS) failed: %s\n", SDL_GetError());
        return 1;
    }

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    input_t in;
    input_init(&in, &cfg);

    int rc = 0;
    if (!rc) rc = test_plain_tap(&in);
    if (!rc) rc = test_combo_suppresses_tap(&in);
    if (!rc) rc = test_tap_after_combo(&in);
    if (!rc) rc = test_key_repeat_keeps_combo_flag(&in);
    if (!rc) rc = test_key_repeat_without_combo_taps_once(&in);
    if (!rc) rc = test_dpad_without_y(&in);

    input_shutdown(&in);
    SDL_Quit();
    if (rc == 0) printf("test_input: すべて成功\n");
    return rc;
}

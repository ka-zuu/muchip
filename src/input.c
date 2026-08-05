#include "input.h"

#include <string.h>

#include "log.h"

/* L2/R2はSDL_GameControllerの抽象化ではアナログ軸(トリガー)として届く。
 * ハードウェアがデジタルスイッチのトリガーでも、SDLが軸値へ正規化して
 * 渡してくる。半分を超えたら「押した」とみなし、閾値をまたいだ瞬間
 * (エッジ)だけをアクションとして発火させる。押しっぱなしでの多重発火を
 * 防ぐため、直前の押下状態を input_t 側に保持する。 */
#define TRIGGER_THRESHOLD 16384

static input_action_t key_to_action(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP:     return INPUT_UP;
        case SDLK_DOWN:   return INPUT_DOWN;
        case SDLK_LEFT:   return INPUT_LEFT;
        case SDLK_RIGHT:  return INPUT_RIGHT;
        case SDLK_z:      return INPUT_A;
        case SDLK_x:      return INPUT_B;
        case SDLK_a:      return INPUT_X;
        case SDLK_s:      return INPUT_Y;
        case SDLK_q:      return INPUT_L1;
        case SDLK_w:      return INPUT_R1;
        case SDLK_1:      return INPUT_L2;
        case SDLK_2:      return INPUT_R2;
        case SDLK_RETURN: return INPUT_START;
        case SDLK_SPACE:  return INPUT_SELECT;
        case SDLK_ESCAPE: return INPUT_QUIT;
        default:          return INPUT_NONE;
    }
}

/* SDL_CONTROLLER_BUTTON_* の論理名で判定する。生のボタン番号を
 * 決め打ちしない (SPEC 13 チェックリスト)。 */
static input_action_t controller_button_to_action(Uint8 button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:        return INPUT_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:      return INPUT_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:      return INPUT_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:     return INPUT_RIGHT;
        case SDL_CONTROLLER_BUTTON_A:              return INPUT_A;
        case SDL_CONTROLLER_BUTTON_B:              return INPUT_B;
        case SDL_CONTROLLER_BUTTON_X:              return INPUT_X;
        case SDL_CONTROLLER_BUTTON_Y:              return INPUT_Y;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:   return INPUT_L1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:  return INPUT_R1;
        case SDL_CONTROLLER_BUTTON_START:          return INPUT_START;
        case SDL_CONTROLLER_BUTTON_BACK:           return INPUT_SELECT;
        default:                                   return INPUT_NONE;
    }
}

static int is_log_joystick(input_t *in, SDL_JoystickID id) {
    for (int i = 0; i < (int)(sizeof(in->log_joysticks) / sizeof(in->log_joysticks[0])); i++) {
        if (in->log_joysticks[i] && SDL_JoystickInstanceID(in->log_joysticks[i]) == id) return 1;
    }
    return 0;
}

void input_init(input_t *in) {
    memset(in, 0, sizeof(*in));

    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) !=
        (SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
            LOG_WARN("SDL_InitSubSystem(GAMECONTROLLER) failed: %s (キーボードのみで続行します)",
                      SDL_GetError());
            return;
        }
    }

    int n = SDL_NumJoysticks();
    int max_log = (int)(sizeof(in->log_joysticks) / sizeof(in->log_joysticks[0]));
    for (int i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            if (!in->controller) {
                in->controller = SDL_GameControllerOpen(i);
                if (in->controller) {
                    LOG_INFO("GameControllerを検出しました: %s", SDL_GameControllerName(in->controller));
                }
            }
            continue;
        }
        /* GameControllerとして認識されなかったJoystick。実機のボタン配置は
         * デバイスごとに異なるため決め打ちせず(SPEC 13)、実際に押された
         * ボタン番号をログへ出すことでP6のマッピング表作成に使う。
         * ここではログ収集のためだけに開き、アクションへは変換しない。 */
        if (in->log_joystick_count < max_log) {
            SDL_Joystick *j = SDL_JoystickOpen(i);
            if (j) {
                LOG_INFO("Joystick(GameController未対応)を検出: \"%s\" button数=%d "
                          "-> config.iniでのマッピングが必要です(P6)",
                          SDL_JoystickName(j), SDL_JoystickNumButtons(j));
                in->log_joysticks[in->log_joystick_count++] = j;
            }
        }
    }

    if (!in->controller && in->log_joystick_count == 0) {
        LOG_INFO("GameController/Joystickは検出されませんでした。キーボード操作のみ有効です");
    }
}

void input_shutdown(input_t *in) {
    if (in->controller) SDL_GameControllerClose(in->controller);
    for (int i = 0; i < in->log_joystick_count; i++) {
        if (in->log_joysticks[i]) SDL_JoystickClose(in->log_joysticks[i]);
    }
    memset(in, 0, sizeof(*in));
}

int input_poll(input_t *in, input_action_t *out) {
    SDL_Event ev;
    if (!SDL_PollEvent(&ev)) return 0;

    switch (ev.type) {
        case SDL_QUIT:
            *out = INPUT_QUIT;
            return 1;

        case SDL_KEYDOWN: {
            input_action_t a = key_to_action(ev.key.keysym.sym);
            /* 上下左右以外はキーリピートを無視する(押しっぱなしでA連打等の
             * 誤操作にならないようにする)。 */
            if (ev.key.repeat && a != INPUT_UP && a != INPUT_DOWN &&
                a != INPUT_LEFT && a != INPUT_RIGHT) {
                *out = INPUT_NONE;
                return 1;
            }
            *out = a;
            return 1;
        }

        case SDL_CONTROLLERBUTTONDOWN:
            *out = controller_button_to_action(ev.cbutton.button);
            return 1;

        case SDL_CONTROLLERAXISMOTION: {
            *out = INPUT_NONE;
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                int down = ev.caxis.value > TRIGGER_THRESHOLD;
                if (down && !in->trigger_l_down) *out = INPUT_L2;
                in->trigger_l_down = down;
            } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                int down = ev.caxis.value > TRIGGER_THRESHOLD;
                if (down && !in->trigger_r_down) *out = INPUT_R2;
                in->trigger_r_down = down;
            }
            return 1;
        }

        case SDL_CONTROLLERDEVICEADDED:
            if (!in->controller) {
                in->controller = SDL_GameControllerOpen(ev.cdevice.which);
                if (in->controller) {
                    LOG_INFO("GameControllerを検出しました: %s",
                              SDL_GameControllerName(in->controller));
                }
            }
            *out = INPUT_NONE;
            return 1;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (in->controller &&
                ev.cdevice.which ==
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in->controller))) {
                LOG_WARN("GameControllerが切断されました");
                SDL_GameControllerClose(in->controller);
                in->controller = NULL;
            }
            *out = INPUT_NONE;
            return 1;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                /* SDL_WINDOWEVENT_RESIZED ではなくこちらを見る: ドラッグでの
                 * リサイズだけでなく、プログラムによるサイズ変更・
                 * フルスクリーン切り替えでも発火するため。 */
                in->window_resized = 1;
            }
            *out = INPUT_NONE;
            return 1;

        case SDL_JOYBUTTONDOWN:
            /* GameControllerとして開けているデバイスからのJOYBUTTONDOWNは
             * CONTROLLERBUTTONDOWNと二重に届く(SDLがGameControllerを
             * Joystick層の上に実装しているため)。ログ対象は
             * input_init()でis_log_joystickに登録した「未対応デバイス」
             * のみに絞る。 */
            if (is_log_joystick(in, ev.jbutton.which)) {
                LOG_INFO("Joystickボタン押下: instance=%d button=%d",
                          ev.jbutton.which, ev.jbutton.button);
            }
            *out = INPUT_NONE;
            return 1;

        default:
            *out = INPUT_NONE;
            return 1;
    }
}

int input_take_window_resized(input_t *in) {
    int v = in->window_resized;
    in->window_resized = 0;
    return v;
}

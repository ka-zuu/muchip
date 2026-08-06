#include "input.h"

#include <string.h>

#include "log.h"

/* L2/R2はSDL_GameControllerの抽象化ではアナログ軸(トリガー)として届く。
 * ハードウェアがデジタルスイッチのトリガーでも、SDLが軸値へ正規化して
 * 渡してくる。半分を超えたら「押した」とみなし、閾値をまたいだ瞬間
 * (エッジ)だけをアクションとして発火させる。押しっぱなしでの多重発火を
 * 防ぐため、直前の押下状態を input_t 側に保持する。 */
#define TRIGGER_THRESHOLD 16384

/* START+SELECT同時押しでの終了検出用ビット(input_t.held_mask)。 */
#define HELD_START  (1u << 0)
#define HELD_SELECT (1u << 1)

/* D-pad長押しリピートのタイミング(input_t.dpad_held[]参照)。
 * 押してから最初のリピートまでは長めに、以降は速めにして、
 * 「1回押すだけの操作」を誤爆させずに長押しでの連続移動を素早くする。 */
#define DPAD_REPEAT_DELAY_MS 350
#define DPAD_REPEAT_RATE_MS   70

static int dpad_index(input_action_t a) {
    switch (a) {
        case INPUT_UP:    return 0;
        case INPUT_DOWN:  return 1;
        case INPUT_LEFT:  return 2;
        case INPUT_RIGHT: return 3;
        default:          return -1;
    }
}

static input_action_t dpad_action(int index) {
    switch (index) {
        case 0: return INPUT_UP;
        case 1: return INPUT_DOWN;
        case 2: return INPUT_LEFT;
        case 3: return INPUT_RIGHT;
        default: return INPUT_NONE;
    }
}

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
        /* muOSのMENUボタン相当(SPEC 6.3「Menu長押し=終了」)。ただしmuOS側の
         * オーバーレイに吸われてSDLへ届かない可能性があるため、
         * START+SELECT同時押しでも終了できるようにしてある(下記参照)。 */
        case SDL_CONTROLLER_BUTTON_GUIDE:          return INPUT_QUIT;
        default:                                   return INPUT_NONE;
    }
}

static int is_log_joystick(input_t *in, SDL_JoystickID id) {
    for (int i = 0; i < (int)(sizeof(in->log_joysticks) / sizeof(in->log_joysticks[0])); i++) {
        if (in->log_joysticks[i] && SDL_JoystickInstanceID(in->log_joysticks[i]) == id) return 1;
    }
    return 0;
}

void input_init(input_t *in, const mugbs_config_t *cfg) {
    memset(in, 0, sizeof(*in));

    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) !=
        (SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
            LOG_WARN("SDL_InitSubSystem(GAMECONTROLLER) failed: %s (キーボードのみで続行します)",
                      SDL_GetError());
            return;
        }
    }

    /* デバイス走査より前にマッピングDBを読み込む必要がある。
     * mux_launch.sh 経由で SDL_GAMECONTROLLERCONFIG_FILE が export
     * されていれば実機ではこれ自体は不要だが(muOSが/usr/lib/
     * gamecontrollerdb.txtを同梱している)、SSH直接起動等それを経由
     * しない場合や、DBに載っていない機種向けの上書き手段として
     * config.iniからも読めるようにしてある(SPEC 6.3)。 */
    if (cfg && cfg->gamecontroller_db[0]) {
        int added = SDL_GameControllerAddMappingsFromFile(cfg->gamecontroller_db);
        if (added < 0) {
            LOG_WARN("gamecontroller_db を読み込めません: %s (%s)",
                      cfg->gamecontroller_db, SDL_GetError());
        } else {
            LOG_INFO("gamecontroller_db から%d件のマッピングを読み込みました: %s",
                      added, cfg->gamecontroller_db);
        }
    }
    if (cfg && cfg->controller_mapping[0]) {
        int rc = SDL_GameControllerAddMapping(cfg->controller_mapping);
        if (rc < 0) {
            LOG_WARN("controller_mapping が不正です: %s", SDL_GetError());
        } else {
            LOG_INFO("controller_mapping を%s", rc == 1 ? "追加しました" : "更新しました");
        }
    }

    int n = SDL_NumJoysticks();
    int max_log = (int)(sizeof(in->log_joysticks) / sizeof(in->log_joysticks[0]));
    for (int i = 0; i < n; i++) {
        int opened_as_controller = 0;
        if (SDL_IsGameController(i)) {
            if (!in->controller) {
                in->controller = SDL_GameControllerOpen(i);
                if (in->controller) {
                    LOG_INFO("GameControllerを検出しました: %s", SDL_GameControllerName(in->controller));
                    opened_as_controller = 1;
                } else {
                    /* SDL_IsGameController()がtrueでもOpen()が失敗することがある
                     * (P6で発見したバグ: 以前はここでcontinueしてしまい、
                     * Joystickとしても開かれず入力が完全に死んでいた)。
                     * この場合は下の生Joystickパスへフォールスルーする。 */
                    LOG_WARN("SDL_GameControllerOpen(index=%d)に失敗しました: %s "
                              "-> Joystickとして開きます", i, SDL_GetError());
                }
            } else {
                /* 既に1台GameControllerを開いている。複数コントローラの
                 * 同時使用はP6のスコープ外(muOSの携帯機はコントローラ1台が前提)。 */
                opened_as_controller = 1;
            }
        }
        if (opened_as_controller) continue;

        /* GameControllerとして認識されなかった(または開けなかった)Joystick。
         * 実機のボタン配置はデバイスごとに異なるため決め打ちせず(SPEC 13)、
         * 名前・GUID・ボタン/軸/ハット数をログへ出すだけに留める
         * (P6の方針転換: 生イベントの自前解釈はしない。input.h冒頭参照)。
         * ユーザーがconfig.iniの[input] controller_mappingを書く際、
         * このGUIDが必要になる。 */
        if (in->log_joystick_count < max_log) {
            SDL_Joystick *j = SDL_JoystickOpen(i);
            if (j) {
                char guid_str[64];
                SDL_JoystickGUID guid = SDL_JoystickGetGUID(j);
                SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
                LOG_INFO("Joystick(GameController未対応)を検出: \"%s\" GUID=%s "
                          "button数=%d axis数=%d hat数=%d "
                          "-> config.iniの[input] gamecontroller_db/controller_mapping "
                          "で対応させてください",
                          SDL_JoystickName(j), guid_str,
                          SDL_JoystickNumButtons(j), SDL_JoystickNumAxes(j),
                          SDL_JoystickNumHats(j));
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
    if (!SDL_PollEvent(&ev)) {
        /* 新しいSDLイベントが無くても、D-padが押しっぱなしならリピートを
         * 合成して返す(SDL_CONTROLLERBUTTONDOWNは単発でOSリピートが無い
         * ため。上記 dpad_held[] 参照)。1回の呼び出しでは最大1件だけ返し、
         * 呼び出し側のポーリングループに複数回呼んでもらう(他のイベント
         * 種別の扱いと揃える)。 */
        Uint32 now = SDL_GetTicks();
        for (int i = 0; i < 4; i++) {
            if (in->dpad_held[i] && now >= in->dpad_next_repeat_at[i]) {
                in->dpad_next_repeat_at[i] = now + DPAD_REPEAT_RATE_MS;
                *out = dpad_action(i);
                return 1;
            }
        }
        return 0;
    }

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

        case SDL_CONTROLLERBUTTONDOWN: {
            input_action_t a = controller_button_to_action(ev.cbutton.button);

            int di = dpad_index(a);
            if (di >= 0) {
                in->dpad_held[di] = 1;
                in->dpad_next_repeat_at[di] = SDL_GetTicks() + DPAD_REPEAT_DELAY_MS;
            }

            /* START+SELECT同時押しでの終了検出(GUIDEがmuOS側に吸われて
             * 届かない場合の代替。SPEC 6.3「Menu長押し=終了」相当)。 */
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) in->held_mask |= HELD_START;
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) in->held_mask |= HELD_SELECT;
            if ((in->held_mask & (HELD_START | HELD_SELECT)) == (HELD_START | HELD_SELECT)) {
                a = INPUT_QUIT;
            }

            *out = a;
            return 1;
        }

        case SDL_CONTROLLERBUTTONUP: {
            int di = dpad_index(controller_button_to_action(ev.cbutton.button));
            if (di >= 0) in->dpad_held[di] = 0;
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) in->held_mask &= ~HELD_START;
            if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) in->held_mask &= ~HELD_SELECT;
            *out = INPUT_NONE;
            return 1;
        }

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
                /* 切断中に押しっぱなしと誤認して幽霊リピートを出し続けないように。 */
                memset(in->dpad_held, 0, sizeof(in->dpad_held));
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

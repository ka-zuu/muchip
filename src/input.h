/* input.h - SDL入力を論理アクションへ翻訳する。 (SPEC 6.3, 4.2)
 *
 * muOSはデバイスごとにボタン配置が異なるため、上位(app.c)はキーボードか
 * GameControllerかを一切意識せず、この論理アクションだけを見ればよい。
 * ボタン番号を決め打ちしない(SPEC 13 チェックリスト): SDL_GameController
 * 経由では SDL_CONTROLLER_BUTTON_* の論理名を使い、生のボタン番号を
 * 直接比較しない。
 *
 * P6での方針転換: 当初は「GameControllerとして認識されないJoystickは
 * 生イベントを自前でconfig.iniのマッピング表に従って解釈する」計画
 * だったが、実際に公開されているmuOS向けアプリ(XMPlayer v0.2.1)の
 * .muxappを展開して調べたところ、そのような自前実装はしていなかった。
 * muOSは /usr/lib/gamecontrollerdb.txt を実機に同梱しており、
 * mux_launch.sh が SDL_GAMECONTROLLERCONFIG_FILE を export するだけで
 * 物理ボタンが SDL_GameController として認識される(XMPlayerはこれに加え
 * gptokeyb2でキーボードへも変換しているが、mugbsは元々キーボードにも
 * 対応済みなのでgptokeyb2は不要)。よってP6では:
 *   - SDL_GameControllerAddMappingsFromFile()/AddMapping() で
 *     config.ini の [input] gamecontroller_db/controller_mapping を
 *     読み込む経路を用意する(mux_launch.sh を経由しない開発時や、
 *     DBに載っていない機種向けの上書き手段)
 *   - GameControllerとして認識されなかったJoystickは、名前・GUID・
 *     ボタン/軸/ハット数をLOG_INFOに出すだけに留める(生イベントの
 *     自前解釈はしない)
 * SPEC 6.3 の「SDL_Joystickにフォールバックし、config.iniでマッピング
 * 上書きを可能にする」は、この形で満たす(PLAN.mdに乖離として記録)。
 */
#ifndef MUGBS_INPUT_H
#define MUGBS_INPUT_H

#include <SDL.h>

#include "config.h"

typedef enum {
    INPUT_NONE = 0,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_A,
    INPUT_B,
    INPUT_X,
    INPUT_Y,
    INPUT_L1,
    INPUT_R1,
    INPUT_L2,
    INPUT_R2,
    INPUT_START,
    INPUT_SELECT,
    INPUT_QUIT, /* ウィンドウを閉じる / Menu長押し相当 */
} input_action_t;

typedef struct {
    SDL_GameController *controller; /* 開けなければNULL(キーボードのみで動作する) */

    /* GameControllerとして認識されなかったJoystickを、ログ収集専用に
     * 開いておくためのハンドル(操作には使わない。P6のマッピング表作成用)。 */
    SDL_Joystick *log_joysticks[4];
    int log_joystick_count;

    /* L2/R2(アナログトリガー軸)のエッジ検出用の直前の押下状態。 */
    int trigger_l_down;
    int trigger_r_down;

    /* START+SELECT同時押しでの終了(SPEC 6.3「Menu長押し=終了」の代替)用。
     * GameController の GUIDE ボタン(muOSのMENU相当)は muOS 側のオーバーレイに
     * 吸われてSDLへ届かない可能性があるため、二重の終了手段として持つ。
     * 長押しにはタイマーが要るが、input_poll()はイベント駆動のみなので
     * 同時押しの方が安く実装できる。 */
    unsigned held_mask;

    /* SDL_WINDOWEVENT_SIZE_CHANGED を受けたら1。input_take_window_resized()で
     * 読んでクリアする。ウィンドウリサイズは input_action_t の語彙に
     * 入れない(action表は[input]設定と--ui-scriptの語彙も兼ねており、
     * リサイズはユーザーが割り当てる操作ではないため)。フラグ方式なら
     * ドラッグ中に大量に飛ぶイベントも1フレーム1回の再計算にまとまる。 */
    int window_resized;
} input_t;

/* 起動時に一度呼ぶ。cfg->gamecontroller_db/controller_mapping があれば
 * デバイス走査より先に SDL へ登録し、その後 SDL_GameControllerOpen()を
 * 試みる。開けたコントローラを保持する。GameControllerとして開けない
 * Joystickが接続されていれば、名前・GUID・ボタン/軸/ハット数を
 * LOG_INFOに出す(config.ini の controller_mapping を書くための情報)。
 * cfgがNULLならマッピング登録をスキップする(テスト用)。失敗しても
 * 致命的ではないため戻り値はなく、常にキーボード操作は有効なまま続行する。 */
void input_init(input_t *in, const mugbs_config_t *cfg);
void input_shutdown(input_t *in);

/* 保留中のSDLイベントを1件処理し、対応する論理アクションを *out に書く。
 * イベントが無くなれば0を返す(*outは触らない)。イベントはあったが
 * 論理アクションに対応しない場合は INPUT_NONE を書いて非0を返す
 * (呼び出し側はループでポーリングを続ければよい)。 */
int input_poll(input_t *in, input_action_t *out);

/* 直前の input_poll() の呼び出し群の間にウィンドウサイズ変更があれば1を
 * 返し、内部フラグをクリアする(呼び出し側はui_handle_resize()を呼ぶ)。 */
int input_take_window_resized(input_t *in);

#endif /* MUGBS_INPUT_H */

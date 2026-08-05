/* input.h - SDL入力を論理アクションへ翻訳する。 (SPEC 6.3, 4.2)
 *
 * muOSはデバイスごとにボタン配置が異なるため、上位(app.c)はキーボードか
 * GameControllerかを一切意識せず、この論理アクションだけを見ればよい。
 * ボタン番号を決め打ちしない(SPEC 13 チェックリスト): SDL_GameController
 * 経由では SDL_CONTROLLER_BUTTON_* の論理名を使い、生のボタン番号を
 * 直接比較しない。
 *
 * P5時点のスコープ: キーボード(ホスト確認用) + SDL_GameController の
 * 既定マッピングのみ。SDL_Joystickへのフォールバックと config.ini による
 * マッピング上書きはP6で追加する。GameControllerとして認識されなかった
 * Joystickは、名前とボタン番号をLOG_INFOに出すだけに留める
 * (P6のマッピング表を作るための実機データになる)。
 */
#ifndef MUGBS_INPUT_H
#define MUGBS_INPUT_H

#include <SDL.h>

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

    /* SDL_WINDOWEVENT_SIZE_CHANGED を受けたら1。input_take_window_resized()で
     * 読んでクリアする。ウィンドウリサイズは input_action_t の語彙に
     * 入れない(action表は[input]設定と--ui-scriptの語彙も兼ねており、
     * リサイズはユーザーが割り当てる操作ではないため)。フラグ方式なら
     * ドラッグ中に大量に飛ぶイベントも1フレーム1回の再計算にまとまる。 */
    int window_resized;
} input_t;

/* 起動時に一度呼ぶ。SDL_GameControllerOpen()を試み、開けたコントローラを
 * 保持する。GameControllerとして開けないJoystickが接続されていれば、
 * その名前をLOG_INFOに出す(P6用の情報収集)。失敗しても致命的ではないため
 * 戻り値はなく、常にキーボード操作は有効なまま続行する。 */
void input_init(input_t *in);
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

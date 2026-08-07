/* battery.h - バッテリー残量の読み取り (Issue #7)。
 *
 * SDL_GetPowerInfo() の薄いラッパ。実機のSDL2 (2.28.5) は
 * /sys/class/power_supply バックエンドを組み込み済みで export しており
 * (`nm -D` で確認済み)、新規の外部依存にはならない(SPEC 12)。
 *
 * SDL_GetPowerInfo() は Linux バックエンドで毎回 sysfs を読み直すため、
 * 毎フレーム(60Hz)呼んではならない。battery_poll() が内部で throttle する。
 *
 * config.h の battery_show_t (表示条件の設定) はここではなく config.h 側に
 * ある。battery.h は「いま残量がいくつか」だけを扱い、「それをどう表示に
 * 反映するか」の判断関数(battery_should_show 等)もここに置くのは、
 * ui_marquee_offset() 等と同じく SDL_Init 無しでテストできる純関数として
 * 独立させるため(tests/test_battery.c 参照)。
 */
#ifndef MUGBS_BATTERY_H
#define MUGBS_BATTERY_H

#include <SDL.h>
#include <stddef.h>

#include "config.h" /* battery_show_t */

/* sysfs を読み直す間隔(ms)。残量は分単位でしか動かないので2秒で十分
 * (充電ケーブルを挿してから色が変わるまでの遅延の上限でもある)。 */
#define BATTERY_POLL_INTERVAL_MS 2000u

/* 既定の低残量しきい値(%)。muOS 側に設定が見つからないときに使う。
 * Issue #7「そうじゃなければ10%とする」。 */
#define BATTERY_LOW_PCT_DEFAULT 10

/* MUGBS_BATTERY_LOW_PCT がどんな値でもこれを超えてクランプしない。
 * muOS 側が壊れた値を返しても「常時赤」にはならないようにするため。 */
#define BATTERY_LOW_PCT_MAX 50

typedef struct {
    int present;  /* バッテリーが存在するか。0=据置/デスクトップ、または不明 */
    int percent;  /* 0-100。不明なら -1 */
    int charging; /* 0/1。SDL_POWERSTATE_CHARGING/CHARGED はどちらも1 */
} battery_status_t;

typedef struct {
    battery_status_t last;
    Uint32 last_poll_ms;
    int have;         /* 一度でも読んだか (last_poll_ms==0 と区別する) */
    int fake_percent; /* MUGBS_BATTERY_FAKE による開発用の上書き。-1=無し */
    int fake_charging;
    int logged_once;  /* 初回の読み取り結果をLOG_INFOしたか */
} battery_t;

/* MUGBS_BATTERY_FAKE を読んでおく。ホストにはバッテリーが無いことが多く、
 * それだとこの機能の描画経路もCIも一度も踏めないための開発用の抜け道
 * (--screenshot/--ui-scriptと同格。src/app.h参照)。 */
void battery_init(battery_t *b);

/* 毎フレーム呼んでよい。前回のポーリングから BATTERY_POLL_INTERVAL_MS
 * 経っていなければキャッシュを返す。now_ms には呼び出し側の
 * SDL_GetTicks() を渡す(ui_marquee_offset()と同じ方針)。 */
const battery_status_t *battery_poll(battery_t *b, Uint32 now_ms);

/* --- 以下はSDLを一切呼ばない純関数 (tests/test_battery.c が検証する) --- */

/* 前回のポーリングから BATTERY_POLL_INTERVAL_MS 以上経ったか。
 * SDL_GetTicks() は約49日で32bitを一周するため、差は必ず符号なしで取る
 * (符号付きで引くと一周した瞬間に「二度とポーリングしない」状態になる)。 */
int battery_should_poll(Uint32 last_ms, Uint32 now_ms, int have_sample);

/* 残量が「少ない」か。percent<0 や present==0 (=読めていない)なら0。
 * 充電中かどうかは見ない(充電中でも残量が少ないのは事実で、色で示す)。 */
int battery_is_low(const battery_status_t *st, int low_pct);

/* いま描くべきか。percent<0 / present==0 のときはどのmodeでも常に0
 * (読めていない値をそれらしく描かない)。 */
int battery_should_show(battery_show_t mode, const battery_status_t *st, int low_pct);

/* MUGBS_BATTERY_LOW_PCT の値を解釈する。NULL/空/非数値/範囲外は
 * BATTERY_LOW_PCT_DEFAULT。上限は BATTERY_LOW_PCT_MAX にクランプする。 */
int battery_low_threshold_from_env(const char *env_value);

#endif /* MUGBS_BATTERY_H */

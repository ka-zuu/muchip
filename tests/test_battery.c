/* test_battery.c - Issue #7 で追加した battery.c の単体テスト。
 *
 * SDL_GetPowerInfo() を呼ぶのは battery_poll()/battery_read_raw() だけで、
 * ここではそれらを直接は呼ばない(実行環境のバッテリー有無に結果が左右され、
 * CIでは再現できないため)。battery_should_poll/battery_is_low/
 * battery_should_show/battery_low_threshold_from_env はいずれもSDLを
 * 呼ばない純関数なので、battery.c 全体をリンクしても SDL_Init は一度も
 * 呼ばれない(test_ui_metrics.c/test_scope.c と同じ方針)。
 */
#include <stdio.h>

#include "battery.h"
#include "test_util.h"

/* --- battery_should_poll --------------------------------------------- */

static int test_should_poll(void) {
    /* 初回(サンプル無し)は時刻に関係なく必ずポーリングする。 */
    CHECK(battery_should_poll(0, 0, 0) == 1);
    CHECK(battery_should_poll(12345, 12345, 0) == 1);

    /* 間隔内は偽。 */
    CHECK(battery_should_poll(1000, 1000 + BATTERY_POLL_INTERVAL_MS - 1, 1) == 0);
    /* 間隔ちょうど・それ以降は真。 */
    CHECK(battery_should_poll(1000, 1000 + BATTERY_POLL_INTERVAL_MS, 1) == 1);
    CHECK(battery_should_poll(1000, 1000 + BATTERY_POLL_INTERVAL_MS + 500, 1) == 1);

    /* SDL_GetTicks() は Uint32 なので約49日で折り返す。符号なし引き算に
     * なっていないと、折り返しの瞬間に「二度とポーリングしない」まま
     * 固まってしまう。 */
    Uint32 last = 0xFFFFF000u;
    Uint32 now = 0x00000100u; /* last から折り返しを跨いで 0x1100 経過 */
    CHECK(battery_should_poll(last, now, 1) == 1);
    CHECK(battery_should_poll(last, last + 1, 1) == 0); /* 折り返し前は普通に間隔内 */
    return 0;
}

/* --- battery_is_low / battery_should_show ------------------------------ */

static battery_status_t mk(int present, int percent, int charging) {
    battery_status_t st;
    st.present = present;
    st.percent = percent;
    st.charging = charging;
    return st;
}

static int test_is_low(void) {
    /* 読めていない値は「低い」と判定しない。 */
    battery_status_t no_batt = mk(0, 50, 0);
    battery_status_t unknown_pct = mk(1, -1, 0);
    CHECK(battery_is_low(&no_batt, 10) == 0);
    CHECK(battery_is_low(&unknown_pct, 10) == 0);

    /* 境界: percent == low_pct は「低い」に含む。 */
    battery_status_t st10 = mk(1, 10, 0);
    CHECK(battery_is_low(&st10, 10) == 1);
    battery_status_t st11 = mk(1, 11, 0);
    CHECK(battery_is_low(&st11, 10) == 0);
    battery_status_t st5 = mk(1, 5, 0);
    CHECK(battery_is_low(&st5, 10) == 1);

    /* 充電中でも、残量自体が低ければ「低い」(色で充電中は別途示す)。 */
    battery_status_t st5c = mk(1, 5, 1);
    CHECK(battery_is_low(&st5c, 10) == 1);
    return 0;
}

static int test_should_show(void) {
    battery_status_t unknown_pct = mk(1, -1, 0);
    battery_status_t no_batt = mk(0, 50, 0);
    battery_status_t high = mk(1, 50, 0);
    battery_status_t low = mk(1, 5, 0);
    battery_status_t low_charging = mk(1, 5, 1);
    battery_status_t boundary = mk(1, 10, 0);

    /* 読めていない値は、どのmodeでも常に非表示。 */
    CHECK(battery_should_show(BATTERY_SHOW_ALWAYS, &unknown_pct, 10) == 0);
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &unknown_pct, 10) == 0);
    CHECK(battery_should_show(BATTERY_SHOW_ALWAYS, &no_batt, 10) == 0);
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &no_batt, 10) == 0);

    /* OFF はどんな値でも非表示。 */
    CHECK(battery_should_show(BATTERY_SHOW_OFF, &high, 10) == 0);
    CHECK(battery_should_show(BATTERY_SHOW_OFF, &low, 10) == 0);

    /* ALWAYS は読めている限り常に表示。 */
    CHECK(battery_should_show(BATTERY_SHOW_ALWAYS, &high, 10) == 1);
    CHECK(battery_should_show(BATTERY_SHOW_ALWAYS, &low, 10) == 1);

    /* LOW は低いときだけ(境界含む)。 */
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &high, 10) == 0);
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &low, 10) == 1);
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &boundary, 10) == 1);
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &low_charging, 10) == 1);

    /* しきい値を変えると境界も動く。 */
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &boundary, 20) == 1);
    CHECK(battery_should_show(BATTERY_SHOW_LOW, &high, 60) == 1);
    return 0;
}

/* --- battery_low_threshold_from_env ------------------------------------ */

static int test_threshold_from_env(void) {
    CHECK(battery_low_threshold_from_env(NULL) == BATTERY_LOW_PCT_DEFAULT);
    CHECK(battery_low_threshold_from_env("") == BATTERY_LOW_PCT_DEFAULT);
    CHECK(battery_low_threshold_from_env("abc") == BATTERY_LOW_PCT_DEFAULT);
    CHECK(battery_low_threshold_from_env("12abc") == BATTERY_LOW_PCT_DEFAULT);
    CHECK(battery_low_threshold_from_env("0") == BATTERY_LOW_PCT_DEFAULT);
    CHECK(battery_low_threshold_from_env("-5") == BATTERY_LOW_PCT_DEFAULT);
    CHECK(battery_low_threshold_from_env("100") == BATTERY_LOW_PCT_DEFAULT);

    CHECK(battery_low_threshold_from_env("20") == 20);
    CHECK(battery_low_threshold_from_env("1") == 1);
    CHECK(battery_low_threshold_from_env("99") == BATTERY_LOW_PCT_MAX); /* 上限クランプ */
    CHECK(battery_low_threshold_from_env("80") == BATTERY_LOW_PCT_MAX); /* 上限クランプ */
    return 0;
}

/* --- battery_init (MUGBS_BATTERY_FAKE) --------------------------------- */

static int test_fake_env_via_init(void) {
    /* battery_init() は getenv() を直接読むため、setenv() で差し替えて
     * 確認する(SDL_Init は呼ばれない)。 */
    battery_t b;

    setenv("MUGBS_BATTERY_FAKE", "85", 1);
    battery_init(&b);
    CHECK(b.fake_percent == 85);
    CHECK(b.fake_charging == 0);

    setenv("MUGBS_BATTERY_FAKE", "+30", 1);
    battery_init(&b);
    CHECK(b.fake_percent == 30);
    CHECK(b.fake_charging == 1);

    setenv("MUGBS_BATTERY_FAKE", "nope", 1);
    battery_init(&b);
    CHECK(b.fake_percent == -1);

    setenv("MUGBS_BATTERY_FAKE", "150", 1); /* 範囲外は無効 */
    battery_init(&b);
    CHECK(b.fake_percent == -1);

    unsetenv("MUGBS_BATTERY_FAKE");
    battery_init(&b);
    CHECK(b.fake_percent == -1);

    /* fake_percentが設定されていれば、SDLを呼ばずにその値を返す。 */
    setenv("MUGBS_BATTERY_FAKE", "+7", 1);
    battery_init(&b);
    const battery_status_t *st = battery_poll(&b, 0);
    CHECK(st->present == 1);
    CHECK(st->percent == 7);
    CHECK(st->charging == 1);

    /* 間隔内の再ポーリングはキャッシュを返す(fake値を変えても即座には
     * 反映されない)。 */
    setenv("MUGBS_BATTERY_FAKE", "99", 1);
    battery_t b2;
    battery_init(&b2);
    b2.fake_percent = 50; /* テストのため直接差し替え(実運用では起動時1回だけ読む) */
    (void)battery_poll(&b2, 0);
    b2.fake_percent = 90;
    const battery_status_t *cached = battery_poll(&b2, BATTERY_POLL_INTERVAL_MS - 1);
    CHECK(cached->percent == 50); /* まだ更新されていない */
    const battery_status_t *fresh = battery_poll(&b2, BATTERY_POLL_INTERVAL_MS);
    CHECK(fresh->percent == 90);

    unsetenv("MUGBS_BATTERY_FAKE");
    return 0;
}

int main(void) {
    if (test_should_poll()) return 1;
    if (test_is_low()) return 1;
    if (test_should_show()) return 1;
    if (test_threshold_from_env()) return 1;
    if (test_fake_env_via_init()) return 1;

    printf("test_battery: すべて成功\n");
    return 0;
}

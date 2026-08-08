/* battery.c - battery.h の実体 (Issue #7)。 */
#include "battery.h"

#include <stdlib.h>

#include "log.h"

/* MUCHIP_BATTERY_FAKE を解釈する。書式は "85"(非充電) / "+85"(充電中)。
 * 不正な値は「上書きしない」(-1)。開発機でも描画経路と色分岐を
 * 一通り踏めるようにするための隠しオプション。 */
static void parse_fake_env(const char *s, int *out_percent, int *out_charging) {
    *out_percent = -1;
    *out_charging = 0;
    if (!s || !*s) return;

    int charging = 0;
    if (*s == '+') { charging = 1; s++; }

    if (!*s) return;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || (end && *end)) return; /* 末尾にゴミが残る入力は無効 */
    if (v < 0 || v > 100) return;

    *out_percent = (int)v;
    *out_charging = charging;
}

void battery_init(battery_t *b) {
    b->last.present = 0;
    b->last.percent = -1;
    b->last.charging = 0;
    b->last_poll_ms = 0;
    b->have = 0;
    b->logged_once = 0;
    parse_fake_env(getenv("MUCHIP_BATTERY_FAKE"), &b->fake_percent, &b->fake_charging);
}

int battery_should_poll(Uint32 last_ms, Uint32 now_ms, int have_sample) {
    if (!have_sample) return 1;
    /* Uint32の引き算は折り返しを跨いでも正しい経過時間になる(符号なし演算)。 */
    return (Uint32)(now_ms - last_ms) >= BATTERY_POLL_INTERVAL_MS;
}

static battery_status_t battery_read_raw(const battery_t *b) {
    battery_status_t st = { 0, -1, 0 };

    if (b->fake_percent >= 0) {
        st.present = 1;
        st.percent = b->fake_percent;
        st.charging = b->fake_charging;
        return st;
    }

    int secs = 0, pct = 0;
    SDL_PowerState state = SDL_GetPowerInfo(&secs, &pct);
    (void)secs;

    switch (state) {
        case SDL_POWERSTATE_ON_BATTERY:
            st.present = 1;
            st.charging = 0;
            break;
        case SDL_POWERSTATE_CHARGING:
        case SDL_POWERSTATE_CHARGED:
            st.present = 1;
            st.charging = 1;
            break;
        case SDL_POWERSTATE_NO_BATTERY:
        case SDL_POWERSTATE_UNKNOWN:
        default:
            st.present = 0;
            st.charging = 0;
            break;
    }

    if (st.present && pct >= 0) {
        st.percent = pct;
        if (st.percent > 100) st.percent = 100;
    } else {
        st.percent = -1;
    }

    return st;
}

const battery_status_t *battery_poll(battery_t *b, Uint32 now_ms) {
    if (battery_should_poll(b->last_poll_ms, now_ms, b->have)) {
        b->last = battery_read_raw(b);
        b->last_poll_ms = now_ms;
        b->have = 1;

        if (!b->logged_once && b->last.present && b->last.percent >= 0) {
            b->logged_once = 1;
            LOG_INFO("battery: present=%d percent=%d charging=%d",
                     b->last.present, b->last.percent, b->last.charging);
        }
    }
    return &b->last;
}

int battery_is_low(const battery_status_t *st, int low_pct) {
    if (!st->present || st->percent < 0) return 0;
    return st->percent <= low_pct;
}

int battery_should_show(battery_show_t mode, const battery_status_t *st, int low_pct) {
    if (!st->present || st->percent < 0) return 0; /* 読めていない値は描かない */
    switch (mode) {
        case BATTERY_SHOW_OFF:    return 0;
        case BATTERY_SHOW_LOW:    return battery_is_low(st, low_pct);
        case BATTERY_SHOW_ALWAYS: return 1;
    }
    return 0;
}

int battery_low_threshold_from_env(const char *env_value) {
    if (!env_value || !*env_value) return BATTERY_LOW_PCT_DEFAULT;

    char *end = NULL;
    long v = strtol(env_value, &end, 10);
    if (end == env_value || (end && *end)) return BATTERY_LOW_PCT_DEFAULT;
    if (v < 1 || v > 99) return BATTERY_LOW_PCT_DEFAULT;
    if (v > BATTERY_LOW_PCT_MAX) v = BATTERY_LOW_PCT_MAX;
    return (int)v;
}

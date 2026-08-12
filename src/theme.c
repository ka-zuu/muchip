/* theme.c - theme.h の実装。SDL非依存の純libc (Issue #27)。
 *
 * 派生色の導出規則・プリセットの設計判断は docs/design-notes.md
 * 「カラーテーマ」参照。ここでは計算そのものだけを扱う。
 */
#include "theme.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

/* 大小文字を無視した完全一致。config.c の同名staticヘルパと同じ実装
 * (どちらもSDL非依存の純libcモジュールで、共有ヘッダを作るほどの
 * 重複でもないため独立に持つ)。 */
static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == 0 && *b == 0;
}

static int clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static int clamp_pct(int pct) {
    if (pct < 0) return 0;
    if (pct > 100) return 100;
    return pct;
}

theme_color_t theme_mix(theme_color_t a, theme_color_t b, int pct) {
    pct = clamp_pct(pct);
    theme_color_t out;
    out.r = (unsigned char)clamp_u8(a.r + ((int)b.r - (int)a.r) * pct / 100);
    out.g = (unsigned char)clamp_u8(a.g + ((int)b.g - (int)a.g) * pct / 100);
    out.b = (unsigned char)clamp_u8(a.b + ((int)b.b - (int)a.b) * pct / 100);
    return out;
}

int theme_luma(theme_color_t c) {
    /* ITU-R BT.601相当。1000倍で整数演算し、四捨五入して0..255に収める。 */
    long v = (299L * c.r + 587L * c.g + 114L * c.b + 500) / 1000;
    return (int)clamp_u8((int)v);
}

theme_color_t theme_recede(theme_color_t base, theme_color_t fg, int pct) {
    pct = clamp_pct(pct);
    int target_luma = (theme_luma(fg) > theme_luma(base)) ? 0 : 255;
    theme_color_t target = { (unsigned char)target_luma, (unsigned char)target_luma,
                              (unsigned char)target_luma };

    /* baseからtargetへ向かう方向のヘッドルーム(各chの余地の最大値)が
     * 小さすぎる(例: base自体が黒に近く、黒方向へ寄せる余地がほぼ無い)
     * 場合、recedeが実質no-opになってsunken==bgのような事故が起きる
     * (theme.h参照)。閾値未満ならmix(base,fg,pct/3)へ逃がす。 */
    int headroom = 0;
    int dr = (target.r > base.r) ? (target.r - base.r) : (base.r - target.r);
    int dg = (target.g > base.g) ? (target.g - base.g) : (base.g - target.g);
    int db = (target.b > base.b) ? (target.b - base.b) : (base.b - target.b);
    if (dr > headroom) headroom = dr;
    if (dg > headroom) headroom = dg;
    if (db > headroom) headroom = db;

    if (headroom < 24) {
        return theme_mix(base, fg, pct / 3);
    }
    return theme_mix(base, target, pct);
}

theme_color_t theme_best_on(theme_color_t bg, theme_color_t a, theme_color_t b) {
    int bg_l = theme_luma(bg);
    int da = theme_luma(a) - bg_l;
    if (da < 0) da = -da;
    int db = theme_luma(b) - bg_l;
    if (db < 0) db = -db;
    return (da >= db) ? a : b;
}

theme_color_t theme_role_color(const theme_t *t, theme_role_t role) {
    /* THEME_SLOT_COUNTはtheme_slot_t(別のenum型)の値なのでintへキャストして
     * 比較する(-Wenum-compare対策。値そのものはtheme.hのコメントどおり
     * theme_role_tの先頭9件と一致させてある)。 */
    if ((int)role < (int)THEME_SLOT_COUNT) {
        return t->slot[role];
    }
    switch (role) {
        case THEME_ROLE_RAISED:
            return theme_mix(t->slot[THEME_BG], t->slot[THEME_FG], 4);
        case THEME_ROLE_GUTTER:
            return theme_mix(t->slot[THEME_BG], t->slot[THEME_FG], 15);
        case THEME_ROLE_SUNKEN:
            return theme_recede(t->slot[THEME_BG], t->slot[THEME_FG], 33);
        case THEME_ROLE_BOX_BG:
            return theme_mix(t->slot[THEME_BG], t->slot[THEME_WARN], 10);
        default:
            break;
    }
    return t->slot[THEME_BG];
}

/* ---- プリセット ---------------------------------------------------------
 *
 * 並びは theme_slot_t(bg,panel,fg,dim,accent,sel,mark,warn,ok)と一致させる。
 * midnight は P6以前からのリテラル値そのもの(見た目を変えないための
 * 唯一の制約)。他の4つはダーク系のみ(実機の反射型/低輝度パネル向け。
 * ライト系が欲しければ Edit theme で custom を作る)。 */
static const theme_t PRESETS[5] = {
    /* midnight は Issue #27 より前の app.c/ui.c のリテラル値そのもの
     * (panel=bar_bg、dim、sel=ui.cのsel_bg)。「見た目を変えない」ための
     * 唯一の制約なので、他プリセットのような mix() 由来の値にしないこと。
     * tests/test_theme.c の test_midnight_matches_legacy_literals() が
     * これを固定する。 */
    [THEME_MIDNIGHT] = { .slot = {
        [THEME_BG]     = { 18,  18,  26 },
        [THEME_PANEL]  = { 30,  30,  42 },
        [THEME_FG]     = { 230, 230, 230 },
        [THEME_DIM]    = { 150, 150, 160 },
        [THEME_ACCENT] = { 120, 180, 255 },
        [THEME_SEL]    = { 60,  90,  160 },
        [THEME_MARK]   = { 255, 210, 90 },
        [THEME_WARN]   = { 255, 120, 90 },
        [THEME_OK]     = { 120, 220, 140 },
    } },
    [THEME_GAMEBOY] = { .slot = {
        [THEME_BG]     = { 15,  56,  15 },
        [THEME_PANEL]  = { 32,  72,  15 },
        [THEME_FG]     = { 155, 188, 15 },
        [THEME_DIM]    = { 78,  115, 15 },
        [THEME_ACCENT] = { 196, 222, 92 },
        [THEME_SEL]    = { 96,  131, 50 },
        [THEME_MARK]   = { 171, 202, 46 },
        [THEME_WARN]   = { 200, 80,  40 },
        [THEME_OK]     = { 139, 172, 15 },
    } },
    [THEME_MONO] = { .slot = {
        [THEME_BG]     = { 10,  10,  10 },
        [THEME_PANEL]  = { 38,  38,  38 },
        [THEME_FG]     = { 240, 240, 240 },
        [THEME_DIM]    = { 114, 114, 114 },
        [THEME_ACCENT] = { 255, 255, 255 },
        [THEME_SEL]    = { 120, 120, 120 },
        [THEME_MARK]   = { 246, 246, 246 },
        [THEME_WARN]   = { 255, 90,  90 },
        [THEME_OK]     = { 120, 220, 140 },
    } },
    [THEME_AMBER] = { .slot = {
        [THEME_BG]     = { 20,  13,  0 },
        [THEME_PANEL]  = { 48,  33,  0 },
        [THEME_FG]     = { 255, 176, 0 },
        [THEME_DIM]    = { 126, 86,  0 },
        [THEME_ACCENT] = { 255, 211, 122 },
        [THEME_SEL]    = { 126, 102, 55 },
        [THEME_MARK]   = { 255, 190, 49 },
        [THEME_WARN]   = { 255, 90,  60 },
        [THEME_OK]     = { 180, 230, 120 },
    } },
    [THEME_SYNTHWAVE] = { .slot = {
        [THEME_BG]     = { 26,  11,  46 },
        [THEME_PANEL]  = { 52,  37,  71 },
        [THEME_FG]     = { 240, 230, 255 },
        [THEME_DIM]    = { 122, 110, 140 },
        [THEME_ACCENT] = { 255, 79,  216 },
        [THEME_SEL]    = { 129, 42,  122 },
        [THEME_MARK]   = { 246, 170, 239 },
        [THEME_WARN]   = { 255, 110, 110 },
        [THEME_OK]     = { 120, 240, 200 },
    } },
};

void theme_preset(theme_id_t id, theme_t *out) {
    if (id < 0 || id >= THEME_CUSTOM) {
        /* THEME_CUSTOMも含め範囲外はmidnightへフォールバックする
         * (theme.hのコメントどおり。custom用の実データは呼び出し側が
         * 別に持つ mugbs_config_t.theme_custom で、ここでは扱わない)。 */
        *out = PRESETS[THEME_MIDNIGHT];
        return;
    }
    *out = PRESETS[id];
}

void theme_resolve(theme_id_t id, const theme_t *custom, theme_t *out) {
    if (id == THEME_CUSTOM && custom != NULL) {
        *out = *custom;
        return;
    }
    theme_preset(id, out);
}

/* ---- プリセット名 -------------------------------------------------------- */

static const char *const THEME_ID_NAMES[THEME_ID_COUNT] = {
    "midnight", "gameboy", "mono", "amber", "synthwave", "custom",
};

static const char *const THEME_ID_LABELS[THEME_ID_COUNT] = {
    "Midnight", "Game Boy", "Mono", "Amber", "Synthwave", "Custom",
};

int theme_id_from_name(const char *name, theme_id_t *out) {
    for (int i = 0; i < THEME_ID_COUNT; i++) {
        if (ieq(name, THEME_ID_NAMES[i])) {
            *out = (theme_id_t)i;
            return 0;
        }
    }
    return -1;
}

const char *theme_id_name(theme_id_t id) {
    if (id < 0 || id >= THEME_ID_COUNT) return THEME_ID_NAMES[THEME_MIDNIGHT];
    return THEME_ID_NAMES[id];
}

const char *theme_id_label(theme_id_t id) {
    if (id < 0 || id >= THEME_ID_COUNT) return THEME_ID_LABELS[THEME_MIDNIGHT];
    return THEME_ID_LABELS[id];
}

/* ---- スロット名 -------------------------------------------------------- */

static const char *const THEME_SLOT_KEYS[THEME_SLOT_COUNT] = {
    "bg", "panel", "fg", "dim", "accent", "sel", "mark", "warn", "ok",
};

static const char *const THEME_SLOT_LABELS[THEME_SLOT_COUNT] = {
    "Background", "Panel", "Text", "Dim", "Accent",
    "Selection", "Now playing", "Warning", "Charging",
};

const char *theme_slot_key(theme_slot_t slot) {
    if (slot < 0 || slot >= THEME_SLOT_COUNT) return "bg";
    return THEME_SLOT_KEYS[slot];
}

const char *theme_slot_label(theme_slot_t slot) {
    if (slot < 0 || slot >= THEME_SLOT_COUNT) return THEME_SLOT_LABELS[THEME_BG];
    return THEME_SLOT_LABELS[slot];
}

/* ---- 16進カラー文字列 ---------------------------------------------------- */

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int theme_color_parse(const char *s, theme_color_t *out) {
    if (*s == '#') s++;
    int v[6];
    for (int i = 0; i < 6; i++) {
        if (!s[i]) return -1;
        v[i] = hex_nibble(s[i]);
        if (v[i] < 0) return -1;
    }
    if (s[6] != '\0') return -1; /* 桁数超過(末尾ゴミ)は失敗させる */
    out->r = (unsigned char)((v[0] << 4) | v[1]);
    out->g = (unsigned char)((v[2] << 4) | v[3]);
    out->b = (unsigned char)((v[4] << 4) | v[5]);
    return 0;
}

void theme_color_format(theme_color_t c, char out[7]) {
    static const char digits[] = "0123456789abcdef";
    out[0] = digits[(c.r >> 4) & 0xF];
    out[1] = digits[c.r & 0xF];
    out[2] = digits[(c.g >> 4) & 0xF];
    out[3] = digits[c.g & 0xF];
    out[4] = digits[(c.b >> 4) & 0xF];
    out[5] = digits[c.b & 0xF];
    out[6] = '\0';
}

/* ---- チャンネル値の刻み -------------------------------------------------- */

/* 梯子: idx 0..31 は idx*8 (0..248)、idx 32 は 255。33段。 */
static int ladder_value(int idx) {
    if (idx <= 0) return 0;
    if (idx >= 32) return 255;
    return idx * 8;
}

/* v以下の最大の梯子値のindex(floor)。 */
static int ladder_index_floor(int v) {
    if (v <= 0) return 0;
    if (v >= 255) return 32;
    return v / 8; /* 8*idx <= v は idx = v/8 (整数除算=floor) で成立 */
}

int theme_channel_step(int v, int dir) {
    v = clamp_u8(v);
    int idx = ladder_index_floor(v);

    if (dir > 0) {
        idx += 1;
    } else if (dir < 0) {
        /* vが梯子値ちょうどでなければ、まずfloor自体への吸着を1段とみなす。
         * ちょうどの場合はさらに1段下げる。 */
        if (v <= ladder_value(idx)) {
            idx -= 1;
        }
    }

    if (idx < 0) idx = 0;
    if (idx > 32) idx = 32;
    return ladder_value(idx);
}

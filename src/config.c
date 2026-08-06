/* config.c - config.ini (INI形式) の読み書き。 (SPEC 7, P6)
 *
 * 外部のINIライブラリは使わない(SPEC 12「依存追加は事前に相談」。
 * バイナリサイズと実機のglibc互換性に直結するため)。純 libc のみ。
 *
 * 読み込みと書き出しは同じキー表 KEYS[] で駆動しており、
 * 「読めるが書けない」「書いたが読み戻せない」というズレが
 * 構造的に起きないようにしてある。
 *
 * ロケールについて: strtod() と printf("%f") は小数点文字がロケール依存だが、
 * 本プロジェクトも SDL も setlocale() を呼ばないため常に "C" ロケールで動く。
 * この前提が崩れると stereo_depth のラウンドトリップが壊れる。
 */
#include "config.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

/* 1行の最大長。これを超える行は「残りを読み捨てて警告」する。
 * 単に途中で切ると、行の後半が次の行として解釈されてパーサが
 * デシンクするため、必ず改行まで読み飛ばすこと。
 * 最長の値である last_path (MUGBS_PATH_MAX=1024) に "key = " が付いても
 * 収まるよう、余裕をもって MUGBS_PATH_MAX の倍にしてある。 */
#define CONFIG_LINE_MAX (MUGBS_PATH_MAX * 2)

typedef enum {
    CFG_INT,
    CFG_BOOL,
    CFG_DOUBLE,
    CFG_REPEAT,
    CFG_STR,
} config_kind_t;

typedef struct {
    const char *section;
    const char *key;      /* INI上のキー名。構造体のフィールド名と一致しなくてよい */
    config_kind_t kind;
    size_t offset;        /* offsetof(mugbs_config_t, ...) */
    double min, max;      /* CFG_INT / CFG_DOUBLE のクランプ範囲 */
    size_t str_size;      /* CFG_STR のバッファサイズ */
} config_key_t;

/* 並び順がそのまま config_save() の出力順になる。
 * クランプ範囲は「手で書き換えられた不正な値でプログラムが壊れない」ことを
 * 保証するためのもの。例えば sample_rate=0 は audio_init() を失敗させ、
 * eq_bass=999 は gme_set_equalizer() に無茶な値を渡すことになる。 */
static const config_key_t KEYS[] = {
    { "playback", "default_length_sec", CFG_INT,    offsetof(mugbs_config_t, default_length_sec), 1, 3600,  0 },
    { "playback", "fade_length_ms",     CFG_INT,    offsetof(mugbs_config_t, fade_length_ms),     0, 60000, 0 },
    { "playback", "repeat_mode",        CFG_REPEAT, offsetof(mugbs_config_t, repeat_mode),        0, 0,     0 },
    { "playback", "shuffle",            CFG_BOOL,   offsetof(mugbs_config_t, shuffle),            0, 0,     0 },
    { "playback", "sample_rate",        CFG_INT,    offsetof(mugbs_config_t, sample_rate),     8000, 96000, 0 },

    { "audio", "stereo_depth", CFG_DOUBLE, offsetof(mugbs_config_t, stereo_depth), 0.0,  1.0, 0 },
    { "audio", "eq_bass",      CFG_INT,    offsetof(mugbs_config_t, eq_bass),     -100, 100, 0 },
    { "audio", "eq_treble",    CFG_INT,    offsetof(mugbs_config_t, eq_treble),   -100, 100, 0 },

    { "ui", "show_all_files", CFG_BOOL, offsetof(mugbs_config_t, show_all_files), 0, 0, 0 },
    { "ui", "last_path",      CFG_STR,  offsetof(mugbs_config_t, last_path),      0, 0, MUGBS_PATH_MAX },

    { "input", "gamecontroller_db",  CFG_STR, offsetof(mugbs_config_t, gamecontroller_db),  0, 0, MUGBS_PATH_MAX },
    { "input", "controller_mapping", CFG_STR, offsetof(mugbs_config_t, controller_mapping), 0, 0, MUGBS_MAPPING_MAX },
};

#define KEY_COUNT ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

/* config_save() が各セクションの直前に出す説明。無ければNULL。 */
static const char *section_comment(const char *section) {
    if (strcmp(section, "input") == 0) {
        return "; muOS実機の物理ボタンは、SDLのゲームコントローラDBが読み込まれていれば\n"
               "; SDL_GameController として認識される。muOSは /usr/lib/gamecontrollerdb.txt を\n"
               "; 同梱している。空欄なら環境変数 SDL_GAMECONTROLLERCONFIG_FILE 等、\n"
               "; SDLの既定動作に任せる。\n"
               "; controller_mapping はDBに載っていない機種向けの追加マッピング1行\n"
               "; (SDL_GameControllerAddMapping形式)。GUIDは起動時のログに出る。\n";
    }
    return NULL;
}

/* ---- 文字列ユーティリティ --------------------------------------------- */

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = 0;
    return s;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == 0 && *b == 0;
}

/* 行末コメントを切り落とす。
 * ';' は「行頭」または「空白の直後」にある場合のみコメント開始とみなす。
 * SPEC 7 のサンプル自体が `default_length_sec = 150   ; 曲長不明時の...` と
 * 行末コメントを使っているため対応が必要。'#' は行頭のみコメントとする
 * (パス等に '#' が含まれても壊れないようにするため)。
 * 副作用として、last_path 等の文字列値に " ;" を含められない。
 * config_save() 側でその値を検出して警告する。 */
static void strip_inline_comment(char *s) {
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == ';' && (i == 0 || isspace((unsigned char)s[i - 1]))) {
            s[i] = 0;
            return;
        }
    }
}

/* ---- 値のパース ------------------------------------------------------- */

/* 末尾にゴミが残る入力("12abc")は失敗させる。 */
static int parse_long_value(const char *s, long *out) {
    if (!*s) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || (end && *end)) return -1;
    *out = v;
    return 0;
}

static int parse_double_value(const char *s, double *out) {
    if (!*s) return -1;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || (end && *end)) return -1;
    *out = v;
    return 0;
}

static int parse_bool_value(const char *s, int *out) {
    if (ieq(s, "true") || ieq(s, "yes") || ieq(s, "on") || ieq(s, "1")) { *out = 1; return 0; }
    if (ieq(s, "false") || ieq(s, "no") || ieq(s, "off") || ieq(s, "0")) { *out = 0; return 0; }
    return -1;
}

static int parse_repeat_value(const char *s, repeat_mode_t *out) {
    if (ieq(s, "none")) { *out = REPEAT_NONE; return 0; }
    if (ieq(s, "one"))  { *out = REPEAT_ONE;  return 0; }
    if (ieq(s, "all"))  { *out = REPEAT_ALL;  return 0; }
    return -1;
}

static const char *repeat_name(repeat_mode_t m) {
    switch (m) {
        case REPEAT_NONE: return "none";
        case REPEAT_ONE:  return "one";
        case REPEAT_ALL:  return "all";
    }
    return "all";
}

/* ---- 既定値 ------------------------------------------------------------ */

void config_set_defaults(mugbs_config_t *c) {
    memset(c, 0, sizeof(*c));

    c->default_length_sec = 150;
    c->fade_length_ms = 8000;
    c->repeat_mode = REPEAT_ALL;
    c->shuffle = 0;
    c->sample_rate = 44100;

    c->stereo_depth = 0.15;
    c->eq_bass = 0;
    c->eq_treble = 0;

    c->show_all_files = 0;
    c->last_path[0] = 0;

    c->gamecontroller_db[0] = 0;
    c->controller_mapping[0] = 0;
}

/* ---- 読み込み ---------------------------------------------------------- */

static const config_key_t *find_key(const char *section, const char *key) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (ieq(KEYS[i].section, section) && ieq(KEYS[i].key, key)) return &KEYS[i];
    }
    return NULL;
}

/* 1つのキーへ値を適用する。値が不正なら警告して既定値のまま残す
 * (呼び出し側は失敗を無視してよい)。 */
static void apply_value(mugbs_config_t *c, const config_key_t *k, const char *value,
                        const char *where, int lineno) {
    void *field = (char *)c + k->offset;

    switch (k->kind) {
        case CFG_INT: {
            long v;
            if (parse_long_value(value, &v) != 0) {
                LOG_WARN("%s:%d: %s の値が数値ではありません: \"%s\" (既定値を使います)",
                         where, lineno, k->key, value);
                return;
            }
            if (v < (long)k->min) {
                LOG_WARN("%s:%d: %s = %ld は下限 %ld を下回るためクランプします",
                         where, lineno, k->key, v, (long)k->min);
                v = (long)k->min;
            } else if (v > (long)k->max) {
                LOG_WARN("%s:%d: %s = %ld は上限 %ld を超えるためクランプします",
                         where, lineno, k->key, v, (long)k->max);
                v = (long)k->max;
            }
            *(int *)field = (int)v;
            return;
        }
        case CFG_DOUBLE: {
            double v;
            if (parse_double_value(value, &v) != 0) {
                LOG_WARN("%s:%d: %s の値が数値ではありません: \"%s\" (既定値を使います)",
                         where, lineno, k->key, value);
                return;
            }
            if (v < k->min) {
                LOG_WARN("%s:%d: %s = %g をクランプします (下限 %g)", where, lineno, k->key, v, k->min);
                v = k->min;
            } else if (v > k->max) {
                LOG_WARN("%s:%d: %s = %g をクランプします (上限 %g)", where, lineno, k->key, v, k->max);
                v = k->max;
            }
            *(double *)field = v;
            return;
        }
        case CFG_BOOL: {
            int v;
            if (parse_bool_value(value, &v) != 0) {
                LOG_WARN("%s:%d: %s の値が真偽値ではありません: \"%s\" "
                         "(true/false/yes/no/on/off/1/0)", where, lineno, k->key, value);
                return;
            }
            *(int *)field = v;
            return;
        }
        case CFG_REPEAT: {
            repeat_mode_t v;
            if (parse_repeat_value(value, &v) != 0) {
                LOG_WARN("%s:%d: %s の値が不正です: \"%s\" (none|one|all)",
                         where, lineno, k->key, value);
                return;
            }
            *(repeat_mode_t *)field = v;
            return;
        }
        case CFG_STR: {
            char *dst = (char *)field;
            size_t n = strlen(value);
            if (n >= k->str_size) {
                LOG_WARN("%s:%d: %s の値が長すぎるため %zu 文字に切り詰めます",
                         where, lineno, k->key, k->str_size - 1);
                n = k->str_size - 1;
            }
            memcpy(dst, value, n);
            dst[n] = 0;
            return;
        }
    }
}

/* 1行読む。0=EOF、1=読めた。*truncated が1なら行が長すぎて
 * 残りを読み捨てた(呼び出し側が警告する)。 */
static int read_line(FILE *f, char *buf, size_t n, int *truncated) {
    *truncated = 0;
    if (!fgets(buf, (int)n, f)) return 0;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] != '\n') {
        /* 改行に達していない = バッファより長い行。
         * 残りを読み捨てないと後半が次の行として解釈されてしまう。 */
        int ch;
        while ((ch = fgetc(f)) != EOF && ch != '\n') { /* 捨てる */ }
        *truncated = 1;
    }
    return 1;
}

int config_load_stream(mugbs_config_t *c, FILE *f, const char *display_name) {
    char line[CONFIG_LINE_MAX];
    char section[64] = "";
    const char *where = display_name ? display_name : "config.ini";
    int lineno = 0;
    int truncated = 0;

    while (read_line(f, line, sizeof(line), &truncated)) {
        lineno++;
        if (truncated) {
            LOG_WARN("%s:%d: 行が長すぎます(%d文字超)。この行は無視します",
                     where, lineno, CONFIG_LINE_MAX - 1);
            continue;
        }

        /* CRLF の '\r' は trim() が空白として落とす(SDカード上で
         * Windows から編集されることを想定)。 */
        char *p = trim(line);
        if (p[0] == '#') continue; /* '#' は行頭のみコメント */
        strip_inline_comment(p);
        p = trim(p);
        if (!*p) continue;

        if (*p == '[') {
            char *close = strchr(p, ']');
            if (!close) {
                LOG_WARN("%s:%d: ']' がありません: \"%s\"", where, lineno, p);
                continue;
            }
            *close = 0;
            char *name = trim(p + 1);
            snprintf(section, sizeof(section), "%s", name);
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) {
            LOG_WARN("%s:%d: '=' がありません: \"%s\"", where, lineno, p);
            continue;
        }
        *eq = 0;
        char *key = trim(p);
        char *value = trim(eq + 1);

        if (!*key) {
            LOG_WARN("%s:%d: キー名が空です", where, lineno);
            continue;
        }
        if (!*section) {
            LOG_WARN("%s:%d: セクション指定より前にキー \"%s\" があります", where, lineno, key);
            continue;
        }

        const config_key_t *k = find_key(section, key);
        if (!k) {
            /* 未知のキー/セクションで load を失敗させない。
             * 将来のフェーズ(P8のEQ等)が増やしたキーを含む config.ini を
             * 古いバイナリが読んでも壊れないようにするため。 */
            LOG_WARN("%s:%d: 未知の設定を無視します: [%s] %s", where, lineno, section, key);
            continue;
        }

        apply_value(c, k, value, where, lineno);
    }

    return 0;
}

int config_load(mugbs_config_t *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        /* 初回起動では存在しないのが正常。エラーにはしない。 */
        LOG_INFO("config: %s が無いため既定値で起動します", path);
        return -1;
    }
    int rc = config_load_stream(c, f, path);
    fclose(f);
    if (rc == 0) LOG_INFO("config: %s を読み込みました", path);
    return rc;
}

/* ---- 書き出し ---------------------------------------------------------- */

/* 文字列値がそのまま読み戻せるかを判定する。読み戻せない値を書くと
 * 次回起動時に黙って別の値になるため、書かずに警告する方を選ぶ。 */
static int str_value_is_safe(const char *s, const char *key) {
    if (strchr(s, '\n') || strchr(s, '\r')) {
        LOG_WARN("config: %s に改行が含まれるため保存しません", key);
        return 0;
    }
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == ';' && (i == 0 || isspace((unsigned char)s[i - 1]))) {
            LOG_WARN("config: %s に行末コメントと区別できない \" ;\" が含まれるため保存しません", key);
            return 0;
        }
    }
    return 1;
}

static int write_config(const mugbs_config_t *c, FILE *f) {
    fprintf(f,
        "; muGBS config.ini\n"
        "; 起動時に読み込み、終了時に自動保存されます。\n"
        "; 終了時にこのファイルは丸ごと書き直されるため、手書きしたコメントや\n"
        "; 並び順は保存されません(値そのものは保持されます)。\n");

    const char *cur_section = NULL;
    for (int i = 0; i < KEY_COUNT; i++) {
        const config_key_t *k = &KEYS[i];

        if (!cur_section || strcmp(cur_section, k->section) != 0) {
            cur_section = k->section;
            fputc('\n', f);
            const char *comment = section_comment(cur_section);
            if (comment) fputs(comment, f);
            fprintf(f, "[%s]\n", cur_section);
        }

        const void *field = (const char *)c + k->offset;
        switch (k->kind) {
            case CFG_INT:
                fprintf(f, "%s = %d\n", k->key, *(const int *)field);
                break;
            case CFG_DOUBLE:
                fprintf(f, "%s = %.3f\n", k->key, *(const double *)field);
                break;
            case CFG_BOOL:
                fprintf(f, "%s = %s\n", k->key, *(const int *)field ? "true" : "false");
                break;
            case CFG_REPEAT:
                fprintf(f, "%s = %s\n", k->key, repeat_name(*(const repeat_mode_t *)field));
                break;
            case CFG_STR: {
                const char *s = (const char *)field;
                fprintf(f, "%s = %s\n", k->key, str_value_is_safe(s, k->key) ? s : "");
                break;
            }
        }
    }

    if (ferror(f)) return -1;
    return 0;
}

int config_save(const mugbs_config_t *c, const char *path) {
    /* いきなり本体を上書きすると、書き込み中に電源が落ちた場合に
     * config.ini が壊れる(携帯機なので現実的なリスク)。
     * 一時ファイルへ書いてから rename() で差し替える。 */
    char tmp[MUGBS_PATH_MAX + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        LOG_ERR("config: 保存できません(書き込み用に開けない): %s", tmp);
        return -1;
    }

    int rc = write_config(c, f);
    if (fclose(f) != 0) rc = -1;

    if (rc != 0) {
        LOG_ERR("config: 書き込みに失敗しました: %s", tmp);
        remove(tmp);
        return -1;
    }

    if (rename(tmp, path) != 0) {
        LOG_ERR("config: 保存できません(rename失敗): %s -> %s", tmp, path);
        remove(tmp);
        return -1;
    }

    LOG_INFO("config: %s を保存しました", path);
    return 0;
}

/* ---- パス解決 ---------------------------------------------------------- */

void config_resolve_path(char *out, size_t out_size, const char *explicit_path) {
    if (explicit_path && explicit_path[0]) {
        snprintf(out, out_size, "%s", explicit_path);
        return;
    }
    const char *env = getenv("MUGBS_CONFIG");
    if (env && env[0]) {
        snprintf(out, out_size, "%s", env);
        return;
    }
    /* muOS の mux_launch.sh は `cd "$APP_DIR"` してから起動するため、
     * これが SPEC 9.1 の <APP_DIR>/config.ini を指す。 */
    snprintf(out, out_size, "./config.ini");
}

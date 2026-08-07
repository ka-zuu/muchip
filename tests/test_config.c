/* test_config.c - P6で追加したconfig.c(INIパーサ)の単体テスト。
 *
 * パース系は tmpfile() + config_load_stream() でメモリ上だけで完結させ、
 * フィクスチャファイルを置かずに済ませている。
 * config_save() のラウンドトリップだけは rename() を伴うため実ファイルを使う。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "test_util.h"

/* 文字列をそのまま config.ini として読み込ませる。 */
static int load_str(mugbs_config_t *c, const char *text) {
    FILE *f = tmpfile();
    if (!f) return -1;
    fputs(text, f);
    rewind(f);
    int rc = config_load_stream(c, f, "test");
    fclose(f);
    return rc;
}

static int near(double a, double b) {
    double d = a - b;
    return d < 1e-9 && d > -1e-9;
}

/* --- 既定値 (SPEC 7 の config.ini サンプルと一致すること) --------------- */

static int test_defaults(void) {
    mugbs_config_t c;
    config_set_defaults(&c);

    CHECK(c.default_length_sec == 180);
    CHECK(c.fade_length_ms == 8000);
    CHECK(c.repeat_mode == REPEAT_ALL);
    CHECK(c.shuffle == 0);
    CHECK(c.sample_rate == 44100);
    CHECK(near(c.stereo_depth, 0.15));
    CHECK(c.eq_bass == 0);
    CHECK(c.eq_treble == 0);
    CHECK(c.show_all_files == 0);
    CHECK(c.title_scroll == 1);
    CHECK(c.battery_show == BATTERY_SHOW_LOW);
    CHECK(c.last_path[0] == 0);
    CHECK(c.gamecontroller_db[0] == 0);
    CHECK(c.controller_mapping[0] == 0);
    return 0;
}

/* --- SPEC 7 のサンプルをそのまま(行末コメント込みで)読めること --------- */

static int test_spec_sample(void) {
    static const char *sample =
        "[playback]\n"
        "default_length_sec = 180   ; 曲長不明時の再生秒数\n"
        "fade_length_ms     = 8000\n"
        "repeat_mode        = all   ; none | one | all\n"
        "shuffle            = false\n"
        "sample_rate        = 44100\n"
        "\n"
        "[audio]\n"
        "stereo_depth = 0.15\n"
        "eq_bass      = 0\n"
        "eq_treble    = 0\n"
        "\n"
        "[ui]\n"
        "show_all_files = false\n"
        "title_scroll   = true   ; 曲名が見切れるとき横スクロールさせる (Issue #8)\n"
        "battery_show   = low    ; off | low | always (Issue #7)\n"
        "last_path      = /mnt/mmc/MUSIC\n";

    mugbs_config_t c;
    config_set_defaults(&c);
    /* 既定値と区別が付くよう、あえて全部ずらしてから読ませる。 */
    c.default_length_sec = 1;
    c.eq_bass = 1;
    c.repeat_mode = REPEAT_NONE;
    c.shuffle = 1;
    c.show_all_files = 1;
    c.title_scroll = 0;
    c.battery_show = BATTERY_SHOW_OFF;

    CHECK(load_str(&c, sample) == 0);

    CHECK(c.default_length_sec == 180);
    CHECK(c.fade_length_ms == 8000);
    CHECK(c.repeat_mode == REPEAT_ALL);
    CHECK(c.shuffle == 0);
    CHECK(c.sample_rate == 44100);
    CHECK(near(c.stereo_depth, 0.15));
    CHECK(c.eq_bass == 0);
    CHECK(c.show_all_files == 0);
    CHECK(c.title_scroll == 1);
    CHECK(c.battery_show == BATTERY_SHOW_LOW);
    CHECK_STREQ(c.last_path, "/mnt/mmc/MUSIC");
    return 0;
}

/* --- 旧バージョンが書いた config.ini をそのまま読めること ---------------
 *
 * 実機には P8以前の mugbs が書いた [audio] volume と [voices] mute_mask を
 * 含む config.ini が残っている。どちらも削除済みの機能なので未知のキーとして
 * 黙って飛ばし、前後の有効なキーは通常どおり効くこと。 */
static int test_legacy_keys_from_older_version(void) {
    mugbs_config_t c;
    config_set_defaults(&c);

    CHECK(load_str(&c,
        "[audio]\n"
        "stereo_depth = 0.5\n"
        "volume = 80\n"          /* P9で廃止 */
        "eq_bass = 25\n"
        "\n"
        "[voices]\n"             /* P8で廃止 */
        "mute_mask = 3\n"
        "\n"
        "[ui]\n"
        "show_all_files = true\n") == 0);

    CHECK(near(c.stereo_depth, 0.5));
    CHECK(c.eq_bass == 25);       /* volume を挟んでも後続のキーが効く */
    CHECK(c.show_all_files == 1); /* 未知セクションの後も読めている */
    return 0;
}

/* --- 未知のセクション/キーを飛ばしても前後のキーは効くこと ------------- */

static int test_unknown_keys_are_skipped(void) {
    mugbs_config_t c;
    config_set_defaults(&c);

    CHECK(load_str(&c,
        "[playback]\n"
        "volume_from_the_future = 12\n"   /* 未知のキー */
        "default_length_sec = 42\n"
        "\n"
        "[bogus_section]\n"
        "whatever = 1\n"
        "\n"
        "[audio]\n"
        "eq_treble = 33\n") == 0);

    CHECK(c.default_length_sec == 42);
    CHECK(c.eq_treble == 33);
    return 0;
}

/* --- 壊れた行があっても load が中断せず、既存の値も壊れないこと -------- */

static int test_malformed_lines(void) {
    mugbs_config_t c;
    config_set_defaults(&c);

    CHECK(load_str(&c,
        "orphan_key = 1\n"          /* セクション指定より前 */
        "[unterminated\n"           /* ']' が無い */
        "[playback]\n"
        "no_equals_sign\n"          /* '=' が無い */
        "= 5\n"                     /* キー名が空 */
        "default_length_sec =\n"    /* 値が空 -> 数値でないので既定値のまま */
        "sample_rate = 22050\n") == 0);

    CHECK(c.default_length_sec == 180); /* 既定値のまま残っている */
    CHECK(c.sample_rate == 22050);      /* 壊れた行の後も読めている */
    return 0;
}

/* --- 範囲外の値をクランプすること -------------------------------------- */

static int test_clamping(void) {
    mugbs_config_t c;
    config_set_defaults(&c);
    CHECK(load_str(&c,
        "[audio]\n"
        "eq_bass = 999\n"
        "stereo_depth = 5.0\n"
        "[playback]\n"
        "sample_rate = 0\n") == 0);
    CHECK(c.eq_bass == 100);
    CHECK(near(c.stereo_depth, 1.0));
    CHECK(c.sample_rate == 8000);

    config_set_defaults(&c);
    CHECK(load_str(&c,
        "[audio]\n"
        "eq_bass = -999\n"
        "stereo_depth = -1.0\n") == 0);
    CHECK(c.eq_bass == -100);
    CHECK(near(c.stereo_depth, 0.0));
    return 0;
}

/* --- 列挙値・真偽値の表記ゆれ ------------------------------------------ */

static int test_enum_and_bool_forms(void) {
    mugbs_config_t c;

    config_set_defaults(&c);
    CHECK(load_str(&c, "[playback]\nrepeat_mode = NONE\n") == 0);
    CHECK(c.repeat_mode == REPEAT_NONE);

    config_set_defaults(&c);
    CHECK(load_str(&c, "[playback]\nrepeat_mode = One\n") == 0);
    CHECK(c.repeat_mode == REPEAT_ONE);

    /* 不正な値は直前の値(=既定値)を維持する */
    config_set_defaults(&c);
    CHECK(load_str(&c, "[playback]\nrepeat_mode = sideways\n") == 0);
    CHECK(c.repeat_mode == REPEAT_ALL);

    static const char *truthy[] = { "true", "TRUE", "yes", "on", "1" };
    static const char *falsy[]  = { "false", "No", "off", "0" };
    for (size_t i = 0; i < sizeof(truthy) / sizeof(truthy[0]); i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[ui]\nshow_all_files = %s\n", truthy[i]);
        config_set_defaults(&c);
        CHECK(load_str(&c, buf) == 0);
        CHECK(c.show_all_files == 1);
    }
    for (size_t i = 0; i < sizeof(falsy) / sizeof(falsy[0]); i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[ui]\nshow_all_files = %s\n", falsy[i]);
        config_set_defaults(&c);
        c.show_all_files = 1;
        CHECK(load_str(&c, buf) == 0);
        CHECK(c.show_all_files == 0);
    }

    /* title_scroll(Issue #8)。既定はonなので、offにできることを見る。 */
    for (size_t i = 0; i < sizeof(falsy) / sizeof(falsy[0]); i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[ui]\ntitle_scroll = %s\n", falsy[i]);
        config_set_defaults(&c);
        CHECK(load_str(&c, buf) == 0);
        CHECK(c.title_scroll == 0);
    }
    for (size_t i = 0; i < sizeof(truthy) / sizeof(truthy[0]); i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[ui]\ntitle_scroll = %s\n", truthy[i]);
        config_set_defaults(&c);
        c.title_scroll = 0;
        CHECK(load_str(&c, buf) == 0);
        CHECK(c.title_scroll == 1);
    }

    /* battery_show(Issue #7)。表記ゆれと不正値。 */
    config_set_defaults(&c);
    CHECK(load_str(&c, "[ui]\nbattery_show = OFF\n") == 0);
    CHECK(c.battery_show == BATTERY_SHOW_OFF);

    config_set_defaults(&c);
    CHECK(load_str(&c, "[ui]\nbattery_show = Always\n") == 0);
    CHECK(c.battery_show == BATTERY_SHOW_ALWAYS);

    /* 不正な値は直前の値(=既定値)を維持する */
    config_set_defaults(&c);
    CHECK(load_str(&c, "[ui]\nbattery_show = sideways\n") == 0);
    CHECK(c.battery_show == BATTERY_SHOW_LOW);
    return 0;
}

/* --- CRLF (SDカードをWindowsで編集した場合) ----------------------------- */

static int test_crlf(void) {
    mugbs_config_t c;
    config_set_defaults(&c);
    CHECK(load_str(&c,
        "[playback]\r\n"
        "default_length_sec = 90\r\n"
        "[ui]\r\n"
        "last_path = /mnt/sdcard\r\n") == 0);
    CHECK(c.default_length_sec == 90);
    CHECK_STREQ(c.last_path, "/mnt/sdcard");
    return 0;
}

/* --- 長すぎる行でパーサがデシンクしないこと ----------------------------- */

static int test_overlong_line(void) {
    /* CONFIG_LINE_MAX (= MUGBS_PATH_MAX*2) を確実に超える長さにする。 */
    size_t huge = MUGBS_PATH_MAX * 3;
    char *text = malloc(huge + 256);
    CHECK(text != NULL);

    int n = snprintf(text, huge + 256, "[ui]\nlast_path = ");
    memset(text + n, 'x', huge);
    /* 長すぎる行の直後の行が、正しく次の行として解釈されること。 */
    snprintf(text + n + huge, huge + 256 - (size_t)n - huge, "\nshow_all_files = true\n");

    mugbs_config_t c;
    config_set_defaults(&c);
    int rc = load_str(&c, text);
    free(text);

    CHECK(rc == 0);
    CHECK(c.last_path[0] == 0);   /* 長すぎる行は丸ごと無視される */
    CHECK(c.show_all_files == 1); /* デシンクしていない */
    return 0;
}

/* --- 文字列値がバッファ長を超えたら切り詰めてNUL終端すること ------------ */

static int test_string_truncation(void) {
    /* 行としては収まるが、last_path (MUGBS_PATH_MAX) は超える長さ。 */
    size_t len = MUGBS_PATH_MAX + 100;
    char *text = malloc(len + 64);
    CHECK(text != NULL);
    int n = snprintf(text, len + 64, "[ui]\nlast_path = ");
    memset(text + n, 'y', len);
    text[n + len] = '\n';
    text[n + len + 1] = 0;

    mugbs_config_t c;
    config_set_defaults(&c);
    int rc = load_str(&c, text);
    free(text);

    CHECK(rc == 0);
    CHECK(strlen(c.last_path) == MUGBS_PATH_MAX - 1);
    CHECK(c.last_path[MUGBS_PATH_MAX - 1] == 0);
    return 0;
}

/* --- [input] セクション ------------------------------------------------- */

static int test_input_section(void) {
    mugbs_config_t c;
    config_set_defaults(&c);
    CHECK(load_str(&c,
        "[input]\n"
        "gamecontroller_db = /usr/lib/gamecontrollerdb.txt\n"
        "controller_mapping = 03000000091200000031000011010000,MyPad,a:b1,b:b0,dpup:h0.1,platform:Linux,\n") == 0);
    CHECK_STREQ(c.gamecontroller_db, "/usr/lib/gamecontrollerdb.txt");
    CHECK_STREQ(c.controller_mapping,
                "03000000091200000031000011010000,MyPad,a:b1,b:b0,dpup:h0.1,platform:Linux,");
    return 0;
}

/* --- ラウンドトリップ (save -> load で全フィールドが一致すること) -------- */

static int check_equal(const mugbs_config_t *a, const mugbs_config_t *b) {
    /* padding を含む可能性があるため memcmp ではなくフィールド毎に見る。 */
    CHECK(a->default_length_sec == b->default_length_sec);
    CHECK(a->fade_length_ms == b->fade_length_ms);
    CHECK(a->repeat_mode == b->repeat_mode);
    CHECK(a->shuffle == b->shuffle);
    CHECK(a->sample_rate == b->sample_rate);
    CHECK(near(a->stereo_depth, b->stereo_depth));
    CHECK(a->eq_bass == b->eq_bass);
    CHECK(a->eq_treble == b->eq_treble);
    CHECK(a->show_all_files == b->show_all_files);
    CHECK(a->title_scroll == b->title_scroll);
    CHECK(a->battery_show == b->battery_show);
    CHECK_STREQ(a->last_path, b->last_path);
    CHECK_STREQ(a->gamecontroller_db, b->gamecontroller_db);
    CHECK_STREQ(a->controller_mapping, b->controller_mapping);
    return 0;
}

static int roundtrip(const mugbs_config_t *src) {
    const char *path = "test_config_roundtrip.ini";
    CHECK(config_save(src, path) == 0);

    mugbs_config_t back;
    config_set_defaults(&back);
    CHECK(config_load(&back, path) == 0);
    remove(path);

    return check_equal(src, &back);
}

static int test_roundtrip_defaults(void) {
    mugbs_config_t c;
    config_set_defaults(&c);
    return roundtrip(&c);
}

static int test_roundtrip_mutated(void) {
    mugbs_config_t c;
    config_set_defaults(&c);
    c.default_length_sec = 47;
    c.fade_length_ms = 1500;
    c.repeat_mode = REPEAT_ONE;
    c.shuffle = 1;
    c.sample_rate = 22050;
    c.stereo_depth = 0.625;
    c.eq_bass = -20;
    c.eq_treble = 35;
    c.show_all_files = 1;
    c.title_scroll = 0;
    c.battery_show = BATTERY_SHOW_ALWAYS;
    snprintf(c.last_path, sizeof(c.last_path), "/mnt/mmc/MUSIC/Game.gbs");
    snprintf(c.gamecontroller_db, sizeof(c.gamecontroller_db), "/usr/lib/gamecontrollerdb.txt");
    snprintf(c.controller_mapping, sizeof(c.controller_mapping),
             "03000000091200000031000011010000,MyPad,a:b1,b:b0,platform:Linux,");
    return roundtrip(&c);
}

/* --- 保存できない場所でも -1 を返して .tmp を残さないこと ---------------- */

static int test_save_failure(void) {
    const char *path = "no_such_dir_here/config.ini";
    CHECK(config_save(&(mugbs_config_t){ 0 }, path) == -1);

    /* .tmp が残っていないこと(そもそもディレクトリが無いので開けない)。 */
    FILE *f = fopen("no_such_dir_here/config.ini.tmp", "r");
    CHECK(f == NULL);
    return 0;
}

int main(void) {
    if (test_defaults()) return 1;
    if (test_spec_sample()) return 1;
    if (test_legacy_keys_from_older_version()) return 1;
    if (test_unknown_keys_are_skipped()) return 1;
    if (test_malformed_lines()) return 1;
    if (test_clamping()) return 1;
    if (test_enum_and_bool_forms()) return 1;
    if (test_crlf()) return 1;
    if (test_overlong_line()) return 1;
    if (test_string_truncation()) return 1;
    if (test_input_section()) return 1;
    if (test_roundtrip_defaults()) return 1;
    if (test_roundtrip_mutated()) return 1;
    if (test_save_failure()) return 1;

    printf("test_config: すべて成功\n");
    return 0;
}

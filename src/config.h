/* config.h - 再生・音声設定の共有構造体。
 *
 * INI ファイルの読み書き(config.c)は P6 で追加する。
 * P1〜P4 では config_set_defaults() が返すコンパイル時既定値のみを使う。
 * デフォルト値は SPEC.md 7章の config.ini サンプルに合わせてある。
 */
#ifndef MUGBS_CONFIG_H
#define MUGBS_CONFIG_H

typedef enum {
    REPEAT_NONE = 0,
    REPEAT_ONE,
    REPEAT_ALL,
} repeat_mode_t;

typedef struct {
    /* [playback] */
    int default_length_sec; /* 曲長不明時の再生秒数 (F-08) */
    int fade_length_ms;
    repeat_mode_t repeat_mode;
    int sample_rate;

    /* [audio] */
    double stereo_depth;
    int eq_bass;
    int eq_treble;
    int volume; /* 0-100 */

    /* [voices] */
    int voice_mute_mask;
} mugbs_config_t;

static inline void config_set_defaults(mugbs_config_t *c) {
    c->default_length_sec = 150;
    c->fade_length_ms = 8000;
    c->repeat_mode = REPEAT_ALL;
    c->sample_rate = 44100;

    c->stereo_depth = 0.15;
    c->eq_bass = 0;
    c->eq_treble = 0;
    c->volume = 80;

    c->voice_mute_mask = 0;
}

#endif /* MUGBS_CONFIG_H */

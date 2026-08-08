/* config.h - 再生・音声・UI・入力設定の共有構造体と config.ini の読み書き。
 *
 * 実体は config.c (P6で追加)。SDL には一切依存しない純 libc なので、
 * SDL をリンクしないテスト(tests/test_playlist.c 等)からも使える。
 * デフォルト値と INI のキー名は SPEC.md 7章の config.ini サンプルに合わせてある。
 *
 * 設定の所有権について: 実行中、mugbs_config_t の権威あるインスタンスは
 * プログラム全体で1つだけ(main() のローカル)。app_t / player_t は
 * ポインタで参照するだけで、コピーを持たない。
 */
#ifndef MUGBS_CONFIG_H
#define MUGBS_CONFIG_H

#include <stdio.h>
#include <stddef.h>

#define MUGBS_PATH_MAX     1024
#define MUGBS_MAPPING_MAX   512

typedef enum {
    REPEAT_NONE = 0,
    REPEAT_ONE,
    REPEAT_ALL,
} repeat_mode_t;

/* Issue #7: 画面右上のバッテリー残量表示の条件。順序は Settings 画面の
 * LEFT/RIGHT循環の順でもある(非表示 -> 減ったときだけ -> 常に)。 */
typedef enum {
    BATTERY_SHOW_OFF = 0,
    BATTERY_SHOW_LOW,
    BATTERY_SHOW_ALWAYS,
} battery_show_t;

typedef struct {
    /* [playback] */
    int default_length_sec; /* 曲長不明時の再生秒数 (F-08) */
    int length_override_sec; /* Issue #19: ながさチェンジ。0=auto(m3u/実測優先、
                               * 不明時はdefault_length_secへフォールバック)、
                               * 非0なら全トラックの曲長をこの秒数へ強制上書きする (F-28) */
    int skip_short_sec; /* Issue #21: 極端に短い曲をスキップする。0=off。
                          * 非0なら実測曲長(natural_ms)がこの秒数以下のトラックを
                          * トラックリスト・再生順の双方から隠す (F-29)。
                          * 判定は常に実測長で行い、length_override_sec(Length)による
                          * 見かけの曲長上書きの影響は受けない。曲長不明(length_known==0、
                          * default_length_secへフォールバックする)トラックは対象外
                          * (誤って消してしまわないため)。 */
    int fade_length_ms;
    repeat_mode_t repeat_mode;
    int shuffle;    /* 0/1。エントリの再生順をランダム化する (F-25, P10) */
    int sample_rate;

    /* [audio] */
    double stereo_depth;
    int eq_bass;   /* P8で音に反映する (F-20) */
    int eq_treble; /* 同上 */
    /* ソフトウェア音量(volume)はP9で廃止した。常に最大出力(audio.h参照)。 */

    /* [ui] */
    int show_all_files;
    char last_path[MUGBS_PATH_MAX]; /* F-13: 直近に開いたファイル/ディレクトリ。空=未設定 */
    int title_scroll; /* Issue #8: Player画面の曲名を横スクロール(マーキー)させるか。
                        * 既定on。offなら従来どおり ui_text_clipped() で "..." 省略。 */
    battery_show_t battery_show; /* Issue #7: 画面右上のバッテリー残量ゲージの表示条件。既定low */

    /* [input]
     * muOS実機の物理ボタンは、SDLのゲームコントローラDBが読み込まれていれば
     * SDL_GameController として認識される(muOSは /usr/lib/gamecontrollerdb.txt を
     * 同梱しており、mux_launch.sh が SDL_GAMECONTROLLERCONFIG_FILE を export する)。
     * 絶対パスをコードへ埋めない(SPEC 12)ため、DBの場所は設定で与える。
     * どちらも空文字列なら SDL の既定動作(環境変数)に任せる。 */
    char gamecontroller_db[MUGBS_PATH_MAX];        /* DBファイルのパス */
    char controller_mapping[MUGBS_MAPPING_MAX];    /* 追加マッピング1行(SDL形式) */
} mugbs_config_t;

/* 既定値を書き込む。config_load() より先に必ず呼ぶこと。
 * config_load() はファイルに書かれていたキーだけを上書きするため、
 * 部分的な config.ini でも欠けた項目は既定値のまま残る。 */
void config_set_defaults(mugbs_config_t *c);

/* path の config.ini を読み、該当フィールドだけを上書きする。
 *   0  … 読み込んだ(不正な行があってもWARNを出して継続する)
 *  -1  … ファイルを開けなかった(初回起動。既定値のまま続行してよい)
 * 行単位で独立に適用するため、途中に不正な行があっても c が
 * 中途半端な状態になることはない。 */
int config_load(mugbs_config_t *c, const char *path);

/* 既に開いた FILE* から読む。config_load() の実体。
 * tests/test_config.c が tmpfile() でファイルシステムを触らずに
 * パーサを検証するために公開している。display_name はログ表示用。 */
int config_load_stream(mugbs_config_t *c, FILE *f, const char *display_name);

/* path へ正規形で書き出す。0で成功、-1で失敗。
 * ユーザーが手書きしたコメントや並び順は保存されない(正規形に置き換わる)。
 * "<path>.tmp" へ書いてから rename() するため、書き込み中の電源断で
 * 既存の config.ini が壊れることはない。 */
int config_save(const mugbs_config_t *c, const char *path);

/* 読み書きに使う config.ini のパスを out へ書く。ファイルが存在しなくても
 * 常に1つ返す(初回起動時はそこへ新規作成するため)。優先順:
 *   1. explicit_path (--config PATH)
 *   2. 環境変数 MUCHIP_CONFIG
 *   3. "./config.ini"
 * 3 は muOS の mux_launch.sh が `cd "$APP_DIR"` してから起動するため、
 * SPEC 9.1 の <APP_DIR>/config.ini と一致する。SPEC 7 が例示している
 * 絶対パス /run/muos/storage/application/muChip/config.ini は
 * SPEC 12/13 の「絶対パスのハードコード禁止」に反するため採らない。 */
void config_resolve_path(char *out, size_t out_size, const char *explicit_path);

#endif /* MUGBS_CONFIG_H */

/* app.h - 画面状態機械とメインループ(GUI本体)。 (SPEC 6, P5)
 *
 * SPEC 4.2 のモジュール構成表には無い追加モジュール。ui.c を描画
 * プリミティブに徹させ、Browser/Player/TrackList の画面遷移・入力処理・
 * レイアウトのロジックをここに集約する
 * (docs/design-notes.md「UI・レイアウト」に乖離として記録)。
 */
#ifndef MUGBS_APP_H
#define MUGBS_APP_H

#include "config.h"

typedef struct {
    const char *initial_path; /* 非NULLならまずそのファイルを開いてPlayer画面から始める
                                  (playlist_open()が失敗した場合はBrowserにエラー表示付きで
                                  留まる。T-12)。NULLならBrowser画面から始める
                                  (--start-dir/last_pathの優先順はapp.c参照)。 */
    const char *start_dir;    /* Browserの開始ディレクトリ。NULLならlast_path、
                                  それも無ければ fallback_start_dir、
                                  それも無ければカレントディレクトリ。 */
    const char *fallback_start_dir; /* 環境変数 MUCHIP_START_DIR (P7)。last_path が
                                  まだ無い初回起動時だけ使われる開始ディレクトリ。
                                  実機では mux_launch.sh が音楽フォルダを自動検出して
                                  ここへ渡す。start_dir(--start-dir)と違い last_path
                                  より優先度が低いため、F-13(前回の続きから開く)を
                                  毎回上書きしてしまうことがない。 */

    int window_w, window_h;   /* 両方正なら、その解像度の非フルスクリーンウィンドウで
                                  起動する(--window。ホストでの複数解像度レイアウト
                                  確認用)。どちらか0以下なら、実機と同じく
                                  SDL_GetCurrentDisplayMode() で検出した解像度で
                                  フルスクリーン起動する。 */

    const char *ui_script_path; /* 非公開のヘッドレステスト用オプション(--ui-script)。
                                    通常の対話的起動では常にNULLを渡すこと。 */

    const char *screenshot_path; /* 非公開の開発用オプション(--screenshot)。非NULLなら
                                     メインループを抜ける直前の1フレームをBMPで書き出す。
                                     ホストには実機の /dev/fb0 に相当するものが無いため、
                                     --ui-script で目的の画面まで進めてからレイアウトを
                                     目視確認するために使う。通常の起動では常にNULL。 */

    const char *config_path;    /* 終了時・Settings画面を抜けるときの自動保存先。
                                    NULLなら保存しない(CLIオーバーライド指定時 等)。 */

    int battery_low_pct; /* Issue #7: [ui] battery_show = low での「低い」しきい値(%)。
                             main.c が battery_low_threshold_from_env() で決める
                             (muOS の mux_launch.sh が export する
                             MUCHIP_BATTERY_LOW_PCT があればそれ、無ければ既定10%)。 */
} app_options_t;

/* cfg は呼び出し側(main())が所有する唯一の権威あるインスタンスへのポインタ。
 * app_run() はこれを直接書き換える(コピーを持たない)。呼び出し側は
 * app_run() から戻った後、必要ならこれを自分で保存してよい
 * (実際には app_run() 内部で opt->config_path があれば既に保存済み)。
 *
 * 戻り値: 0で正常終了、非0でSDL初期化等の失敗。 */
int app_run(mugbs_config_t *cfg, const app_options_t *opt);

#endif /* MUGBS_APP_H */

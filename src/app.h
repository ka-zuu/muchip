/* app.h - 画面状態機械とメインループ(GUI本体)。 (SPEC 6, P5)
 *
 * SPEC 4.2 のモジュール構成表には無い追加モジュール。ui.c を描画
 * プリミティブに徹させ、Browser/Player/TrackList の画面遷移・入力処理・
 * レイアウトのロジックをここに集約する(PLAN.mdに乖離として記録)。
 */
#ifndef MUGBS_APP_H
#define MUGBS_APP_H

#include "config.h"

/* initial_path が非NULLならまずそのファイルを開いてPlayer画面から始める
 * (playlist_open()が失敗した場合はBrowserにエラー表示付きで留まる。T-12)。
 * NULLならBrowser画面から始める。
 *
 * start_dir はBrowserの開始ディレクトリ(NULLならカレントディレクトリ)。
 *
 * window_w/window_h が両方正なら、その解像度の非フルスクリーンウィンドウで
 * 起動する(--window。ホストでの複数解像度レイアウト確認用)。
 * どちらか0以下なら、実機と同じく SDL_GetCurrentDisplayMode() で検出した
 * 解像度でフルスクリーン起動する。
 *
 * ui_script_path は非公開のヘッドレステスト用オプション(--ui-script)。
 * 通常の対話的起動では常にNULLを渡すこと。
 *
 * 戻り値: 0で正常終了、非0でSDL初期化等の失敗。 */
int app_run(const mugbs_config_t *cfg, const char *initial_path, const char *start_dir,
            int window_w, int window_h, const char *ui_script_path);

#endif /* MUGBS_APP_H */

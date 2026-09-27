#include "app.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "battery.h"
#include "browser.h"
#include "input.h"
#include "log.h"
#include "player.h"
#include "playctx.h"
#include "playlist.h"
#include "ui.h"

/* main.c と同じ既定値付きフォールバック(CMakeLists.txt の project() が
 * 唯一のバージョン情報源。tests/ から app.c を単体ビルドすることは
 * 無いが保険として持たせておく)。Settingsのヘッダサブタイトルに使う。 */
#ifndef MUCHIP_VERSION
#define MUCHIP_VERSION "0.0.0-dev"
#endif

typedef enum {
    SCREEN_BROWSER,
    SCREEN_PLAYER,
    SCREEN_TRACKLIST,
    SCREEN_SETTINGS, /* P6: SPEC 6.1。START で Browser/Player どちらからも開く */
    SCREEN_THEME_EDIT, /* Issue #27: Settings画面の"Edit theme"(A)で開くサブ画面 */
    SCREEN_FOLDER_PICK, /* Issue #47: Settings画面の"Start folder"(A)で開くサブ画面 */
} app_screen_t;

typedef struct {
    ui_t ui;
    input_t input;
    player_t player;
    playlist_t *pl; /* 現在のプレイリスト。NULL=何も開いていない */
    browser_t browser;
    app_screen_t screen;

    /* Issue #47: Player画面のUP/DOWN(同ディレクトリの前/次ファイルへ送る)
     * を支える再生コンテキスト。P9はこれをPlayer画面内の一覧UI(2つ目の
     * browser_t)として持っていたが、Issue #47で一覧UI自体を撤去したため、
     * カーソル/スクロールを持たない薄いモデル(playctx.h参照)へ置き換えた。
     * 将来のプレイリスト機能(Issue #18)は playctx_t の kind を増やす形で
     * ここに乗せる想定。 */
    playctx_t playctx;

    /* Issue #47: Settings画面の"Start folder"(A)で開くディレクトリ専用の
     * フォルダ選択サブ画面(SCREEN_FOLDER_PICK)用。app->browser とは別の
     * 3つ目のインスタンス(Browser/Player中の状態を壊さないため、
     * かつてのplayer_listと同じ理由)。 */
    browser_t folder_pick;
    char player_path[MUGBS_PATH_MAX]; /* app_open_path() に渡されたパス。
                                          show_all_files 変更時の再構築、
                                          およびResume(Issue #47)の保存に使う。
                                          空文字列=まだ何も開いていない */

    mugbs_config_t *cfg; /* 参照のみ。所有権は呼び出し側(main())。
                            プログラム全体で権威あるインスタンスはこれ1つだけ
                            (player_t は const mugbs_config_t* で同じものを見る)。
                            show_all_files はここに統合済み(cfg->show_all_files)。 */
    const char *config_path; /* Settings退出時・終了時の自動保存先。NULL=保存しない */

    int tracklist_sel;
    int tracklist_scroll;

    int settings_sel;
    int settings_scroll;
    app_screen_t settings_return; /* Settingsを抜けたら戻る画面(Browser/Player) */
    int settings_confirm_reset; /* P10: Xで開くリセット確認ダイアログの表示中フラグ */

    /* Issue #27: テーマエディタ(SCREEN_THEME_EDIT)の状態。
     * theme_edit_working は表示・編集中の9色そのもの(app_enter_theme_edit()
     * で実効テーマへ初期化し、以後はLEFT/RIGHTで直接書き換える)。
     * theme_edit_entry_id/theme_edit_entry_custom は入室時の
     * cfg->theme_id/cfg->theme_custom の生の値(copy-on-first-editで
     * theme_idをCUSTOMへ切り替える前の状態。Xでの完全な取り消しに使う。
     * 「resolveした表示色」ではなく生の値を保持するのは、入室時点で
     * theme_idがCUSTOMでなかった場合、cfg->theme_customには無関係な
     * 過去のcustomパレットが残っている可能性があり、Xで戻すときに
     * それを実効色で上書きして消してしまわないため)。
     * theme_edit_dirty は「このセッションでcfg->theme_id/theme_customに
     * 実際に書き込んだか」(=すでにCUSTOMとして編集中か)。 */
    theme_t theme_edit_working;
    theme_id_t theme_edit_entry_id;
    theme_t theme_edit_entry_custom;
    int theme_edit_dirty;
    int theme_edit_slot;    /* 0..THEME_SLOT_COUNT-1 */
    int theme_edit_channel; /* 0=R, 1=G, 2=B */
    int theme_edit_scroll;

    /* F-14 ビジュアライザ。app_update_scope() が1フレームに1回だけ
     * player から取り込み、トリガ(立ち上がりゼロ交差)を合わせたもの。
     * draw_player() は描くだけ。 */
    short scope[AUDIO_SCOPE_SAMPLES];
    int scope_len;

    /* Issue #7: バッテリー残量。app_update_battery() が1フレームに1回だけ
     * battery_poll()(内部で throttle 済み)から取り込み、draw_*() は
     * battery_status/battery_visible を見るだけ(SDL_GetPowerInfoを
     * draw_*から呼ばないことで、1フレーム内の4画面が同じ値を見る)。 */
    battery_t battery;
    battery_status_t battery_status;
    int battery_visible;
    int battery_low_pct; /* app_options_tから受け取る (main.c) */

    char status[256];   /* Browserのフッタ/Playerに一時表示するエラー等 */
    Uint32 status_until; /* SDL_GetTicks()がこれを超えたら消える */

    int running;
} app_t;

/* ---- ヘッドレステスト用の入力スクリプト(--ui-script、非公開オプション) ---
 * SDLイベントを経由せず、テキストファイルに列挙したアクション名を
 * そのまま app_dispatch() へ注入する。SDL_VIDEODRIVER=dummy /
 * SDL_AUDIODRIVER=dummy と組み合わせ、画面遷移のクラッシュ回帰をCIで
 * 検出するためのもの(表示や実際の操作感は見られない)。 */
typedef struct {
    input_action_t *actions;
    int count;
    int pos;
} ui_script_t;

static input_action_t parse_action_name(const char *name) {
    static const struct { const char *name; input_action_t action; } table[] = {
        { "UP", INPUT_UP }, { "DOWN", INPUT_DOWN }, { "LEFT", INPUT_LEFT }, { "RIGHT", INPUT_RIGHT },
        { "A", INPUT_A }, { "B", INPUT_B }, { "X", INPUT_X }, { "Y", INPUT_Y },
        { "L1", INPUT_L1 }, { "R1", INPUT_R1 }, { "L2", INPUT_L2 }, { "R2", INPUT_R2 },
        { "START", INPUT_START }, { "SELECT", INPUT_SELECT }, { "QUIT", INPUT_QUIT },
        { "Y_LEFT", INPUT_Y_LEFT }, { "Y_RIGHT", INPUT_Y_RIGHT },
        { "Y_UP", INPUT_Y_UP }, { "Y_DOWN", INPUT_Y_DOWN },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) == 0) return table[i].action;
    }
    return INPUT_NONE;
}

static int ui_script_load(ui_script_t *script, const char *path) {
    memset(script, 0, sizeof(*script));
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_ERR("--ui-script: ファイルを開けません: %s", path);
        return -1;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* 行頭が '#' なら行全体がコメント。行の途中の "空白 + #" も
         * 行末コメントとして切り落とす(tests/ui_smoke.script は
         * "A       # 説明" のようにアクション名の後ろへ注釈を書く形式を
         * 使っているが、以前はここで切り落としていなかったため
         * parse_action_name() が行全体を渡されて常に不一致になり、
         * QUIT 以外のアクションがP5から一度も実行されていなかった)。 */
        if (p[0] == '#') continue;
        for (char *c = p; *c; c++) {
            if (*c == '#' && c > p && (c[-1] == ' ' || c[-1] == '\t')) {
                *c = 0;
                break;
            }
        }

        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' ||
                            p[len - 1] == ' ' || p[len - 1] == '\t')) {
            p[--len] = 0;
        }
        if (len == 0) continue;

        input_action_t a = parse_action_name(p);
        if (a == INPUT_NONE) {
            LOG_WARN("--ui-script: 不明なアクション名を無視します: %s", p);
            continue;
        }
        input_action_t *grown = realloc(script->actions, sizeof(*grown) * (size_t)(script->count + 1));
        if (!grown) {
            fclose(f);
            free(script->actions);
            memset(script, 0, sizeof(*script));
            return -1;
        }
        script->actions = grown;
        script->actions[script->count++] = a;
    }
    fclose(f);
    return 0;
}

static int ui_script_next(ui_script_t *script, input_action_t *out) {
    if (script->pos >= script->count) return 0;
    *out = script->actions[script->pos++];
    return 1;
}

static void ui_script_free(ui_script_t *script) {
    free(script->actions);
    memset(script, 0, sizeof(*script));
}

/* ---- レイアウト共通ヘルパ ------------------------------------------- */

/* Browser/TrackList で共有するリスト領域(ヘッダ帯とフッタ帯の間)。 */
static ui_rect_t list_rect(app_t *app) {
    ui_rect_t r;
    r.x = app->ui.metrics.pad;
    r.y = app->ui.metrics.header_h;
    r.w = app->ui.screen_w - app->ui.metrics.pad * 2;
    r.h = app->ui.screen_h - app->ui.metrics.header_h - app->ui.metrics.footer_h;
    /* ui_metrics_compute() 側でクランプしているため通常は起きないが、
     * 呼び出し側の防御としても負値を許さない。 */
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

static int list_visible_rows(app_t *app) {
    ui_rect_t r = list_rect(app);
    int rows = r.h / app->ui.metrics.line_h;
    return rows < 1 ? 1 : rows;
}

/* Issue #41: パスの末尾要素(フォルダ名/ファイル名)をヘッダのタイトルに
 * 使うためのヘルパ。フルパスはサブタイトル側に出すので、ここでは
 * ディレクトリ区切りの最後だけ返す。戻り値は path 内を指すポインタ
 * (所有権を持たない)。browser.c の parent_dir()/join_path() が作る
 * cwd は末尾に'/'を付けない(ルート"/"だけの例外)ので、末尾スラッシュの
 * トリムはルート判定にだけ使い、それ以外は素直に最後の'/'の次から
 * 文字列終端まで返す。 */
static const char *path_leaf(const char *path) {
    if (!path || !path[0]) return "";
    size_t len = strlen(path);
    size_t end = len;
    while (end > 0 && path[end - 1] == '/') end--;
    if (end == 0) return "/";
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    return path + start;
}

/* ---- ビジュアライザ (F-14) ---------------------------------------------
 *
 * 画面に出す点数。リング(AUDIO_SCOPE_SAMPLES)の半分だけを使い、残り半分は
 * トリガ位置を探すための余白にする。こうすると、どこにトリガが見つかっても
 * 表示する時間窓の長さが常に一定になる(窓長が毎フレーム変わると、
 * 波形が横に伸び縮みして読めない)。 */
#define APP_SCOPE_DRAW_SAMPLES (AUDIO_SCOPE_SAMPLES / 2)

/* Player画面のステータス行より下の帯は、Issue #47でファイル一覧UIを
 * 撤去して以降、丸ごとビジュアライザ(F-14)になる(draw_player()参照)。
 * この行数を切れない極端な低解像度では波形自体を描かない
 * (フッタへはみ出させない。SPEC 6.2)。 */
#define PLAYER_WAVE_MIN_ROWS 2

/* 1フレームに1回だけ波形を取り込む。
 *
 * トリガについて: 単に直近N点を並べるだけだと、オーディオコールバックと
 * 描画フレームの位相が毎回ずれるため、波形が横に流れて何も読めない。
 * オシロスコープと同じく「立ち上がりのゼロ交差」を探してそこを左端に
 * 揃えると、同じ音が鳴っている間は静止して見える。 */
static void app_update_scope(app_t *app) {
    short raw[AUDIO_SCOPE_SAMPLES];
    player_snapshot_scope(&app->player, raw, AUDIO_SCOPE_SAMPLES);

    int trigger = 0;
    for (int i = 0; i < APP_SCOPE_DRAW_SAMPLES; i++) {
        if (raw[i] <= 0 && raw[i + 1] > 0) {
            trigger = i;
            break;
        }
    }
    /* 見つからなければ trigger=0 のまま(無音や直流成分だけのとき)。 */
    memcpy(app->scope, raw + trigger, sizeof(short) * APP_SCOPE_DRAW_SAMPLES);
    app->scope_len = APP_SCOPE_DRAW_SAMPLES;
}

/* Issue #7: 1フレームに1回だけ呼ぶ。battery_poll() 自体は内部で
 * throttle されている(BATTERY_POLL_INTERVAL_MS)ので、ここは軽い。
 * draw_*() からは SDL_GetPowerInfo を直接呼ばせないことで、1フレーム内の
 * 4画面が必ず同じ値を見るようにしている(app_update_scope()と同じ方針)。 */
static void app_update_battery(app_t *app) {
    const battery_status_t *st = battery_poll(&app->battery, SDL_GetTicks());
    app->battery_status = *st;
    app->battery_visible = battery_should_show(app->cfg->battery_show, st, app->battery_low_pct);
}

/* Issue #27: 実効テーマ(プリセットまたはcustom)を確定してui_tへ反映する。
 * app_update_scope()/app_update_battery()と同じく1フレームに1回、描画
 * dispatchの直前で無条件に呼ぶ。Settings画面での変更・Edit theme画面での
 * ライブプレビュー・起動直後の反映のすべてをこの1箇所に集約できる
 * (呼び出し箇所ごとに反映を忘れる事故を構造的に防ぐ。コストはswitch1回+
 * 27バイトコピーで無視できる)。 */
static void app_apply_theme(app_t *app) {
    theme_t t;
    theme_resolve(app->cfg->theme_id, &app->cfg->theme_custom, &t);
    ui_set_theme(&app->ui, &t);
}

static void set_status(app_t *app, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->status_until = SDL_GetTicks() + 4000;
}

/* ---- last_path (F-13) --------------------------------------------------- */

/* cfg->last_path へ書く。バッファ長を超える場合は警告して何もしない
 * (中途半端に切り詰めたパスを次回起動時の開始位置として使うのは
 * 誤動作の元になるため、載せないほうが安全)。 */
static void set_last_path(app_t *app, const char *path) {
    if (!path) return;
    size_t n = strlen(path);
    if (n >= sizeof(app->cfg->last_path)) {
        LOG_WARN("last_path が長すぎるため記憶しません: %s", path);
        return;
    }
    memcpy(app->cfg->last_path, path, n + 1);
}

/* ---- Player画面の再生コンテキスト (Issue #47) ---------------------------
 *
 * opened_path は app_open_path() に渡されたパスそのもの(.gbs/.gb/.m3u/.zip)。
 * playlist_source_t.fs_path ではなくこれを使う理由:
 *   - zip内ソースの fs_path は NULL である (playlist.h)
 *   - .zip を開いたときに送り先の基準にしたいのは「zipを含むディレクトリ」
 *     であり、いま鳴っているのはその .zip 自身である
 *   - .gbs/.m3u/.zip のどれでも同じ扱いで済む唯一の値である
 *
 * force_rescan=0 のとき、既に同じディレクトリを開いていれば readdir を
 * やり直さない(playctx_open_dir()内部の判定。SDカード上での無駄な
 * 再走査を避ける)。show_all_files が変わったときだけ force_rescan=1 で
 * 呼ぶ。 */
static void app_sync_playctx(app_t *app, const char *opened_path, int force_rescan) {
    if (!opened_path || !opened_path[0]) return;

    if (playctx_open_dir(&app->playctx, opened_path, app->cfg->show_all_files,
                          force_rescan) != 0) {
        /* 読めないディレクトリ。コンテキストは空にしておく
         * (Up/Downでのファイル送りは何もしなくなるだけで、再生自体は
         * 継続する)。 */
        playctx_free(&app->playctx);
        app->player_path[0] = 0;
        return;
    }

    size_t n = strlen(opened_path);
    if (n >= sizeof(app->player_path)) {
        LOG_WARN("パスが長すぎるため記憶しません: %s", opened_path);
        app->player_path[0] = 0;
        return;
    }
    memcpy(app->player_path, opened_path, n + 1);
}

/* ---- ファイルを開く ---------------------------------------------------- */

/* playlist_open() が失敗しても現在の再生・プレイリストには一切触れない
 * (Browserに留まりエラー表示するだけ。T-12: 壊れたファイルでクラッシュしない)。
 * 成功した場合のみ、既存の再生を止めてから(SPEC 13チェックリスト:
 * gme_open_data の所有権/ロック順序に注意)差し替える。 */
static void app_open_path(app_t *app, const char *path) {
    playlist_t *new_pl = NULL;
    if (playlist_open(path, app->cfg, &new_pl) != 0) {
        set_status(app, "Failed to open: %s", path);
        return;
    }

    player_stop(&app->player);
    if (app->pl) playlist_free(app->pl);
    app->pl = new_pl;

    player_load_playlist(&app->player, app->pl);
    if (player_play_entry(&app->player, 0) != 0) {
        set_status(app, "Failed to play: %s", path);
        playlist_free(app->pl);
        app->pl = NULL;
        return;
    }

    set_last_path(app, path); /* F-13: 直近に開いたファイルを記憶する */
    /* Player画面のUP/DOWN用の再生コンテキストを、いま開いたファイルへ
     * 合わせる (Issue #47)。app_open_path() はGUI側で playlist_open() を
     * 呼ぶ唯一の関数なので、Browserの決定・argvのinitial_path・
     * Player画面でのUP/DOWNによる送り・Resumeの4経路すべてがここを通る。 */
    app_sync_playctx(app, path, 0);

    /* Issue #47: Browserのカーソルを、いま開いたファイルへ合わせる
     * (Player画面の一覧UIを廃したため、Up/Downで送った後にBでBrowserへ
     * 戻ったときの手がかりをBrowserのカーソル位置に一本化する)。
     * Browserがいま別のディレクトリを見ている場合は何もしない
     * (無関係な項目へカーソルを動かしてしまわないため)。 */
    if (app->browser.cwd && app->playctx.dir && strcmp(app->browser.cwd, app->playctx.dir) == 0) {
        browser_select_by_name(&app->browser, path_leaf(path));
    }

    app->tracklist_sel = 0;
    app->tracklist_scroll = 0;
    app->screen = SCREEN_PLAYER;
}

/* Player画面のUP/DOWN: 同ディレクトリの前/次ファイルを開いて再生する
 * (Issue #47。旧player_list_move()+player_list_open_selected()相当)。
 * 一覧が1件だけ(自分自身に折り返す)場合は同じファイルの再オープンで
 * 再生位置が0へ戻ってしまうのを避けるため、何もしない。 */
static void app_player_step_file(app_t *app, int delta) {
    if (app->playctx.count <= 0) return;

    char path[MUGBS_PATH_MAX];
    if (playctx_step_path(&app->playctx, delta, path, sizeof(path)) != 0) return;
    if (app->player_path[0] && strcmp(path, app->player_path) == 0) return;

    app_open_path(app, path);
}

/* L2/R2 (前/次ファイル, SPEC 6.3): 現エントリの source を跨ぐ最初の
 * エントリへジャンプする。m3uが複数ファイルを参照する場合(P3)や
 * zip内に複数ファイルがある場合(P4)に、ファイル単位で送るための機能。 */
static void app_prev_source(app_t *app) {
    if (!app->pl || app->player.current_entry < 0) return;
    int cur_source = app->pl->entries[app->player.current_entry].source_index;

    int i = app->player.current_entry;
    while (i > 0 && app->pl->entries[i - 1].source_index == cur_source) i--;

    if (i > 0) {
        int prev_source = app->pl->entries[i - 1].source_index;
        int j = i - 1;
        while (j > 0 && app->pl->entries[j - 1].source_index == prev_source) j--;
        player_play_entry(&app->player, j);
    } else if (app->cfg->repeat_mode == REPEAT_ALL) {
        int last = app->pl->entry_count - 1;
        int src = app->pl->entries[last].source_index;
        int j = last;
        while (j > 0 && app->pl->entries[j - 1].source_index == src) j--;
        player_play_entry(&app->player, j);
    }
}

static void app_next_source(app_t *app) {
    if (!app->pl || app->player.current_entry < 0) return;
    int cur_source = app->pl->entries[app->player.current_entry].source_index;

    int i = app->player.current_entry;
    while (i < app->pl->entry_count && app->pl->entries[i].source_index == cur_source) i++;

    if (i < app->pl->entry_count) {
        player_play_entry(&app->player, i);
    } else if (app->cfg->repeat_mode == REPEAT_ALL) {
        player_play_entry(&app->player, 0);
    }
}

/* ---- 入力ハンドラ ------------------------------------------------------ */

static void handle_browser_input(app_t *app, input_action_t a) {
    switch (a) {
        /* 1ステップのカーソル移動は端で折り返す。ページ送り(LEFT/RIGHT)は
         * クランプのまま(SPEC 6.3)。 */
        case INPUT_UP:    browser_move_wrap(&app->browser, -1); break;
        case INPUT_DOWN:  browser_move_wrap(&app->browser, 1); break;
        case INPUT_LEFT:  browser_page(&app->browser, -list_visible_rows(app)); break;
        case INPUT_RIGHT: browser_page(&app->browser, list_visible_rows(app)); break;
        case INPUT_A: {
            if (browser_enter(&app->browser, app->cfg->show_all_files)) {
                set_last_path(app, app->browser.cwd); /* F-13 */
            } else {
                char path[4096];
                if (browser_selected_path(&app->browser, path, sizeof(path)) == 0) {
                    app_open_path(app, path);
                }
            }
            break;
        }
        case INPUT_B:
            if (browser_up(&app->browser, app->cfg->show_all_files)) {
                set_last_path(app, app->browser.cwd); /* F-13 */
            }
            break;
        case INPUT_START:
            /* SPEC 6.3 の表はStartをPlayer画面専用としているが、ファイルを
             * 開くまでSettingsへ入れないのは初回体験として悪いため、
             * Browserからも開けるようにした
             * (docs/design-notes.md「設定(config.ini)」に乖離として記録)。 */
            app->settings_return = SCREEN_BROWSER;
            app->settings_sel = 0;
            app->settings_scroll = 0;
            app->screen = SCREEN_SETTINGS;
            break;
        default:
            break;
    }
}

/* Y+LEFT/RIGHT (P11): Settings画面に入らずRepeatモードを直接変える
 * ショートカット。Settingsの並び(none/one/all)と同じ順に±1周させる。 */
static void app_step_repeat_mode(app_t *app, int direction) {
    const int n = 3; /* REPEAT_NONE, REPEAT_ONE, REPEAT_ALL */
    int v = ((int)app->cfg->repeat_mode + direction) % n;
    if (v < 0) v += n;
    app->cfg->repeat_mode = (repeat_mode_t)v;
    /* Issue #15: Settings画面のadjust_setting()と同じく、one への出入りを
     * いま鳴っている曲へ即時反映する(player_apply_config()参照)。 */
    player_apply_config(&app->player);
}

/* Y+UP/DOWN (P11): Shuffleを明示的にon/offする(トグルにしない)。
 * D-padの長押しリピートでUP/DOWNが連射されても、同じ値を再代入するだけに
 * なるようにするため(トグルだと押しっぱなしでちらつく)。 */
static void app_set_shuffle(app_t *app, int on) {
    app->cfg->shuffle = on ? 1 : 0;
}

/* repeat が非0なら、input_last_was_repeat()が直前のUP/DOWNをD-pad長押し
 * (またはキーボードのOSキーリピート)由来だと報告したことを示す
 * (Issue #47)。押しっぱなしで曲を連打で切り替えてしまわないよう、
 * Player画面のUP/DOWNだけこれを無視する(1回押すごとに1回だけ送る)。
 * 他の画面のカーソル移動はリピートを許可したまま(app_dispatch()参照)。 */
static void handle_player_input(app_t *app, input_action_t a, int repeat) {
    switch (a) {
        /* UP/DOWN: 同ディレクトリの前/次ファイルへ送る(Issue #47。
         * P9で音量調整を廃止して空いたスロットを、一覧UI撤去後も
         * 「ファイル送り」用途で再利用している)。 */
        case INPUT_UP:
            if (!repeat) app_player_step_file(app, -1);
            break;
        case INPUT_DOWN:
            if (!repeat) app_player_step_file(app, 1);
            break;
        case INPUT_LEFT:
            player_prev_track(&app->player);
            break;
        case INPUT_RIGHT:
            player_next_track(&app->player);
            break;
        case INPUT_SELECT:
            player_toggle_pause(&app->player);
            break;
        case INPUT_B:
            app->screen = SCREEN_BROWSER;
            break;
        case INPUT_X:
            app->tracklist_sel = app->player.current_entry;
            app->screen = SCREEN_TRACKLIST;
            break;
        /* Y単体(押して離すだけ)はもう何もしない。Yを押しながらの
         * LEFT/RIGHT/UP/DOWNだけが下記のコンボとして意味を持つ(P11)。
         * config はポインタで player と共有しているため、ここへの代入だけで
         * 次の player_next_track()/player_prev_track() から反映される。 */
        case INPUT_Y_LEFT:
            app_step_repeat_mode(app, -1);
            break;
        case INPUT_Y_RIGHT:
            app_step_repeat_mode(app, 1);
            break;
        case INPUT_Y_UP:
            app_set_shuffle(app, 1);
            break;
        case INPUT_Y_DOWN:
            app_set_shuffle(app, 0);
            break;
        case INPUT_L1: {
            int pos = player_tell_ms(&app->player) - 5000;
            if (pos < 0) pos = 0;
            player_seek(&app->player, pos);
            break;
        }
        case INPUT_R1: {
            int dur = player_current_duration_ms(&app->player);
            int pos = player_tell_ms(&app->player) + 5000;
            if (dur > 0 && pos > dur) pos = dur;
            player_seek(&app->player, pos);
            break;
        }
        case INPUT_L2:
            app_prev_source(app);
            break;
        case INPUT_R2:
            app_next_source(app);
            break;
        case INPUT_START:
            app->settings_return = SCREEN_PLAYER;
            app->settings_sel = 0;
            app->settings_scroll = 0;
            app->screen = SCREEN_SETTINGS;
            break;
        default:
            break;
    }
}

static void handle_tracklist_input(app_t *app, input_action_t a) {
    int n = app->pl ? app->pl->entry_count : 0;
    switch (a) {
        /* 端で折り返す(P9)。n==0 のときは何もしない(n-1 が負になるため)。 */
        case INPUT_UP:
            if (n > 0) app->tracklist_sel = (app->tracklist_sel + n - 1) % n;
            break;
        case INPUT_DOWN:
            if (n > 0) app->tracklist_sel = (app->tracklist_sel + 1) % n;
            break;
        case INPUT_LEFT:
            app->tracklist_sel -= list_visible_rows(app);
            if (app->tracklist_sel < 0) app->tracklist_sel = 0;
            break;
        case INPUT_RIGHT:
            app->tracklist_sel += list_visible_rows(app);
            if (app->tracklist_sel > n - 1) app->tracklist_sel = n - 1;
            break;
        case INPUT_A:
            if (n > 0) player_play_entry(&app->player, app->tracklist_sel);
            app->screen = SCREEN_PLAYER;
            break;
        case INPUT_B:
        case INPUT_X:
            app->screen = SCREEN_PLAYER;
            break;
        default:
            break;
    }
}

/* ---- Settings画面 (P6, SPEC 6.1) --------------------------------------
 *
 * mugbs_config_t のフィールドをoffsetofで指す1枚の表で駆動する。
 * SET_INT/SET_DOUBLE/SET_BOOL/SET_ENUM/SET_MINUTES/SET_LENGTH/SET_SECONDSの
 * 7種のみをここで扱う。sample_rate はオーディオデバイスの再オープンが必要なため含めていない
 * (P6以降も非対応)。eq_bass/eq_treble は P8 で音へ反映されるようになった
 * ので表に入れてある。 */

typedef enum {
    SET_INT,
    SET_DOUBLE,
    SET_BOOL,
    SET_ENUM,
    /* Issue #16: config上は従来どおり秒(int)で保持しつつ、表示と
     * 刻み幅(min/max/stepは「秒」のまま。60単位で書く)だけを分単位に
     * 見せる。setting_get/setting_setはSET_INTと同じintアクセスでよい。
     * 専用なのはsettings_item_text()の表示とadjust_setting()の目盛り
     * 吸着(config.iniに残っている端数秒からの初回操作対応)だけ。 */
    SET_MINUTES,
    /* Issue #19: ながさチェンジ。SET_MINUTESと同じく秒(int)で保持し、
     * 表示・目盛り吸着(60単位ではなく300=5分単位)だけ専用にする。
     * 0は特別扱いで"auto"と表示する(SET_MINUTESには無い分岐)。 */
    SET_LENGTH,
    /* Issue #21: 極端に短い曲のスキップしきい値。秒(int)を1秒刻みで保持する
     * (SET_MINUTES/SET_LENGTHと違い秒単位のまま表示する。刻みが1なので
     * adjust_setting()の目盛り吸着(端数からの復帰)自体が不要=SET_INTと
     * 同じ else 分岐で動く)。0は特別扱いで"off"と表示する。 */
    SET_SECONDS,
    /* Issue #27: 値を持たない行。LEFT/RIGHTでは何も変わらず、Aでサブ画面
     * (Edit theme)を開く。setting_get/setting_setは0を返す/no-opにする
     * ことで、setSETTINGS[]を全走査するapp_reset_settings()から見ても
     * 自動的に無害になる(何も読まない・書かない)。 */
    SET_ACTION,
    /* Issue #47: SET_ACTIONと同じく値を持たない行だが、Aで開くサブ画面が
     * SCREEN_FOLDER_PICK(Edit themeではない)固定になる点だけ区別する
     * (handle_settings_input()参照)。表示・reset対象への算入は
     * SET_ACTIONと同じ扱い。 */
    SET_FOLDER,
} setting_kind_t;

typedef struct {
    const char *label;
    setting_kind_t kind;
    size_t offset;    /* offsetof(mugbs_config_t, ...) */
    double min, max, step; /* SET_ENUM/SET_BOOL は 0..(選択肢数-1)、step=1 */
    const char *const *enum_names; /* SET_ENUMのみ非NULL */
    int enum_count;
    /* Issue #43: 非0なら、この項目はlibgmeのEQ/ステレオ深度に相当し、
     * 現在再生中のソースがSPC(playlist_effects_supported()==0)のときは
     * 行をグレーアウトする(settings_item_dim()参照)。値の編集自体は
     * 禁止しない(形式非依存のグローバル設定であり、GBS/NSFへ戻れば
     * 効くため)。 */
    int needs_effects;
} setting_def_t;

static const char *const REPEAT_MODE_NAMES[] = { "none", "one", "all" };
/* Issue #7。表示名はconfig.iniのトークン("off"/"low"/"always")と字面が
 * 一致しなくてよい。setting_get/setting_set が扱うのは添字だけで、
 * 順序さえ config.h の battery_show_t と一致していればよい。 */
static const char *const BATTERY_SHOW_NAMES[] = { "off", "when low", "always" };
/* Issue #27。theme.cのTHEME_ID_LABELS[]と表示文字列は同じにしてある
 * (config.iniのトークン(theme_id_name())は小文字・空白無しで別。
 * BATTERY_SHOW_NAMESと同じ「表示名とトークンは字面が違ってよい」方針)。
 * theme.cの配列を直接使わないのは、SETTINGS[]がstatic constの
 * const char*const*配列を要求するため(関数呼び出しは初期化子にできない)。 */
static const char *const THEME_NAMES[] = {
    "Midnight", "Game Boy", "Mono", "Amber", "Synthwave", "Custom",
};
/* Issue #47。config.iniのトークン(start_mode_name())は"folder"/"resume"で
 * 別(他のSET_ENUM表示名と同じ「字面が違ってよい」方針)。 */
static const char *const START_MODE_NAMES[] = { "Folder", "Last played" };

/* Default length/Fade が「次のトラックから反映される」ことを示す
 * フッタの "(next track)" 注記はP10で削除した(ユーザー判断。
 * app_apply_settings()のコメントに反映タイミングの説明は残してある)。
 * Issue #16: Default length は曲長不明ファイルを扱う際に最も触る項目
 * なので先頭に置く(以前は6番目)。1..10分・1分刻み(=60..600秒・step 60)。
 * Issue #19: Length(ながさチェンジ)はそれより優先して触る項目なので
 * さらに先頭へ置く。auto(0)..30分・5分刻み(=0..1800秒・step 300)。
 * Issue #21: Skip short(短い曲のスキップ)はDefault lengthのすぐ後ろに置く
 * (どちらも「曲長に関する項目」でまとまりが良い)。off(0)..30秒・1秒刻み。 */
static const setting_def_t SETTINGS[] = {
    { "Length",              SET_LENGTH,  offsetof(mugbs_config_t, length_override_sec), 0,   1800, 300, NULL, 0, 0 },
    { "Default length",     SET_MINUTES, offsetof(mugbs_config_t, default_length_sec), 60,  600,  60, NULL, 0, 0 },
    { "Skip short",       SET_SECONDS, offsetof(mugbs_config_t, skip_short_sec),      0,    30,   1, NULL, 0, 0 },
    { "Repeat",           SET_ENUM,   offsetof(mugbs_config_t, repeat_mode),        0,     2,   1, REPEAT_MODE_NAMES, 3, 0 },
    { "Shuffle",            SET_BOOL,   offsetof(mugbs_config_t, shuffle),            0,     1,   1, NULL, 0, 0 },
    /* Issue #43: 末尾の1はneeds_effects。SPC再生中はEQ/ステレオ深度が
     * 効かないためグレーアウトの対象(settings_item_dim()参照)。 */
    { "Stereo depth",      SET_DOUBLE, offsetof(mugbs_config_t, stereo_depth),       0.0,   1.0, 0.05, NULL, 0, 1 },
    { "EQ bass",            SET_INT,    offsetof(mugbs_config_t, eq_bass),         -100,   100,   5, NULL, 0, 1 },
    { "EQ treble",           SET_INT,    offsetof(mugbs_config_t, eq_treble),       -100,   100,   5, NULL, 0, 1 },
    { "Fade",                 SET_INT,    offsetof(mugbs_config_t, fade_length_ms),   0, 20000, 500, NULL, 0, 0 },
    { "Show all files",         SET_BOOL,   offsetof(mugbs_config_t, show_all_files),   0,     1,   1, NULL, 0, 0 },
    { "Scroll title",           SET_BOOL,   offsetof(mugbs_config_t, title_scroll),     0,     1,   1, NULL, 0, 0 },
    { "Show battery",           SET_ENUM,   offsetof(mugbs_config_t, battery_show),     0,     2,   1, BATTERY_SHOW_NAMES, 3, 0 },
    /* Issue #27: 末尾に追加(既存のtests/ui_smoke.scriptの並びを崩さず
     * 追記だけで済ませるため)。customも循環に含める
     * (含めないとadjust_setting()の%nから抜けられない片道になる)。 */
    { "Theme",                  SET_ENUM,   offsetof(mugbs_config_t, theme_id),         0,     5,   1, THEME_NAMES, 6, 0 },
    /* Issue #27: customパレットを画面から編集するサブ画面への入口。
     * SET_ACTIONはoffset/min/max/step/enum_namesを一切使わないが、
     * offsetには意味的に近いフィールドを入れておく(将来offset比較の
     * コードが増えたときの誤爆を避けるための保険。現状は使われない)。 */
    { "Edit theme",             SET_ACTION, offsetof(mugbs_config_t, theme_id),         0,     0,   0, NULL, 0, 0 },
    /* Issue #47: 起動モード。folder(既定)/resume(前回の再生状態)。 */
    { "Start with",             SET_ENUM,   offsetof(mugbs_config_t, start_mode),       0,     1,   1, START_MODE_NAMES, 2, 0 },
    /* Issue #47: start_mode=folderのときに開始するディレクトリを選ぶ
     * サブ画面(SCREEN_FOLDER_PICK)への入口。SET_ACTIONと同じく値を持たない。 */
    { "Start folder",           SET_FOLDER, offsetof(mugbs_config_t, start_folder),     0,     0,   0, NULL, 0, 0 },
};
#define SETTINGS_COUNT ((int)(sizeof(SETTINGS) / sizeof(SETTINGS[0])))

/* SET_ENUMは repeat_mode_t と battery_show_t(Issue #7) の2つの enum 型を
 * 跨いで扱うため、特定の enum 型のポインタでは読み書きできない。
 * GCC/Clangはどちらの enum も int と同じ大きさ・表現になる(値域 0..2 の
 * 符号なし enum は unsigned int)ので int としてアクセスするが、その前提が
 * 崩れたらビルドを止める。 */
_Static_assert(sizeof(repeat_mode_t) == sizeof(int), "SET_ENUMはint幅のenumを前提にしている");
_Static_assert(sizeof(battery_show_t) == sizeof(int), "SET_ENUMはint幅のenumを前提にしている");
_Static_assert(sizeof(theme_id_t) == sizeof(int), "SET_ENUMはint幅のenumを前提にしている");
_Static_assert(sizeof(start_mode_t) == sizeof(int), "SET_ENUMはint幅のenumを前提にしている"); /* Issue #47 */

static double setting_get(const mugbs_config_t *cfg, const setting_def_t *s) {
    const void *field = (const char *)cfg + s->offset;
    switch (s->kind) {
        case SET_INT:     return *(const int *)field;
        case SET_DOUBLE:  return *(const double *)field;
        case SET_BOOL:    return *(const int *)field;
        case SET_ENUM:    return *(const int *)field;
        case SET_MINUTES: return *(const int *)field; /* 秒のまま保持(表示側で分に換算) */
        case SET_LENGTH:  return *(const int *)field; /* 同上。0=auto */
        case SET_SECONDS: return *(const int *)field; /* 秒のまま保持。0=off */
        case SET_ACTION:  return 0; /* 値を持たない */
        case SET_FOLDER:  return 0; /* 値を持たない(Issue #47) */
    }
    return 0;
}

static void setting_set(mugbs_config_t *cfg, const setting_def_t *s, double v) {
    void *field = (char *)cfg + s->offset;
    switch (s->kind) {
        case SET_INT:     *(int *)field = (int)v; break;
        case SET_DOUBLE:  *(double *)field = v; break;
        case SET_BOOL:    *(int *)field = (int)v; break;
        case SET_ENUM:    *(int *)field = (int)v; break;
        case SET_MINUTES: *(int *)field = (int)v; break;
        case SET_LENGTH:  *(int *)field = (int)v; break;
        case SET_SECONDS: *(int *)field = (int)v; break;
        case SET_ACTION:  break; /* no-op */
        case SET_FOLDER:  break; /* no-op(Issue #47) */
    }
}

/* Settingsで変更した値を、いま反映できる範囲で反映する。呼び出し側
 * (adjust_setting)が値変更のたびに呼ぶ。stereo_depth/eq_*は
 * player_apply_config()経由で即時、default_length_sec/length_override_sec
 * は全エントリのduration_msをplaylist_apply_config()で再計算する
 * (次トラックからフェード自体が新しい値になるのはplayer.cの
 * start_track_at()が毎回p->configを読むため。詳細はplayer.h)。
 * Issue #21: skip_short_secが変わったときはduration_msの再計算に加えて
 * entries[](可視ビュー)自体の件数が変わりうる。「いま鳴っている曲」を
 * source_index/track_indexで控えてplaylist_apply_config()のkeep_source/
 * keep_trackへ渡し(=しきい値に関わらずその曲だけは可視に残す)、
 * ビュー再構築後にplaylist_find_entry()で新しい添字を引いて
 * player_reanchor_entry()でplayer側のcurrent_entryを追随させる
 * (emuには触れないので音は途切れない)。TrackList画面のカーソル/スクロール
 * も新しいentry_countへクランプしておく(先頭画面に戻ってから件数が
 * 減っていて配列外を指す、という事故を防ぐ)。
 * Issue #19: length_override_secが変わったときも同じ経路でduration_msを
 * 更新する必要があるため、playlist_apply_config()を先に呼んで
 * playlist_entry_t.duration_msを確定させてから player_apply_config() を
 * 呼ぶ(順序が逆だと、いま再生中の曲のフェードが古いduration_msの
 * ままになる。player_apply_config()参照)。 */
static void app_apply_settings(app_t *app) {
    if (app->pl) {
        int keep_source = -1, keep_track = -1;
        int cur = app->player.current_entry;
        if (cur >= 0 && cur < app->pl->entry_count) {
            keep_source = app->pl->entries[cur].source_index;
            keep_track = app->pl->entries[cur].track_index;
        }

        playlist_apply_config(app->pl, app->cfg, keep_source, keep_track);

        if (keep_source >= 0) {
            int reanchored = playlist_find_entry(app->pl, keep_source, keep_track);
            if (reanchored >= 0) player_reanchor_entry(&app->player, reanchored);
        }

        int n = app->pl->entry_count;
        if (app->tracklist_sel > n - 1) app->tracklist_sel = n > 0 ? n - 1 : 0;
        if (app->tracklist_scroll > app->tracklist_sel) app->tracklist_scroll = app->tracklist_sel;
    }
    player_apply_config(&app->player);
}

static void adjust_setting(app_t *app, int direction) {
    const setting_def_t *s = &SETTINGS[app->settings_sel];
    /* Issue #27/#47: SET_ACTION/SET_FOLDER行はLEFT/RIGHTでは何も変わらない
     * (Aでサブ画面を開く方はhandle_settings_input()側で分岐する)。 */
    if (s->kind == SET_ACTION || s->kind == SET_FOLDER) return;

    double before = setting_get(app->cfg, s);
    double v;

    if (s->kind == SET_MINUTES || s->kind == SET_LENGTH) {
        /* Issue #16: config.iniに残っている端数秒(旧既定の150秒等)から
         * 操作を始めても、まず分の目盛りへ吸着させてから1段動かす。
         * 例: 150秒(2.5分)で右へ -> floor(150/60)+1 = 3分(180秒)。
         * 左へ -> floor(150/60)-1 = 1分(60秒)。既に整数分ならただの
         * ±1分になる。Issue #19: SET_LENGTHはstepが300(5分)なので同じ
         * 式のまま5分刻みに吸着する(0=autoもこの刻みの1つとして扱う)。 */
        int step = (int)s->step;
        int iv = (int)before / step + direction;
        v = (double)(iv * step);
    } else {
        v = before + (double)direction * s->step;
    }

    if (s->kind == SET_ENUM || s->kind == SET_BOOL) {
        /* enum/boolは端で折り返す(SPEC 6.3: LEFT/RIGHTでの循環に馴染む)。 */
        int n = (s->kind == SET_BOOL) ? 2 : s->enum_count;
        int iv = ((int)v % n + n) % n;
        v = iv;
    } else {
        if (v < s->min) v = s->min;
        if (v > s->max) v = s->max;
    }

    setting_set(app->cfg, s, v);
    app_apply_settings(app);

    /* show_all_filesが実際に変わった場合のみBrowserを再走査する。
     * 無条件に呼ぶと他の項目を弄るたびに毎回ディレクトリを読み直し、
     * カーソル位置(selected/scroll)がbrowser_open_dir()によって
     * 0へリセットされてしまう(browser.cの契約)。 */
    if (s->offset == offsetof(mugbs_config_t, show_all_files) && v != before) {
        browser_open_dir(&app->browser, app->browser.cwd, app->cfg->show_all_files);
        /* Player画面の再生コンテキストも同じフィルタで作り直す
         * (Issue #47。旧・P9)。ディレクトリは変わらないので
         * force_rescan=1 が必須。 */
        if (app->player_path[0]) app_sync_playctx(app, app->player_path, 1);
    }
}

/* Issue #47: いま鳴っている曲の位置をcfg->resume_*へ書く(start_mode=resume
 * での復元用)。何も再生していない・player_pathが空(zip内から開いた等で
 * app_sync_playctx()が記録できなかった)場合は前の値を残す(何もしない)。
 * app_save_config()から呼ぶことで、Settings退出時・Theme Editor退出時・
 * 終了時のすべてで自動的に最新化される(1箇所に集約し、呼び忘れの事故を
 * 構造的に無くす。app_apply_theme()等と同じ方針)。 */
static void app_capture_resume(app_t *app) {
    if (!app->pl || app->player.current_entry < 0 || !app->player_path[0]) return;

    size_t n = strlen(app->player_path);
    if (n >= sizeof(app->cfg->resume_path)) {
        LOG_WARN("resume_path が長すぎるため記憶しません: %s", app->player_path);
        return;
    }
    const playlist_entry_t *e = &app->pl->entries[app->player.current_entry];
    memcpy(app->cfg->resume_path, app->player_path, n + 1);
    app->cfg->resume_source = e->source_index;
    app->cfg->resume_track = e->track_index;
    app->cfg->resume_position_ms = player_tell_ms(&app->player);
}

/* config_pathが設定されていれば保存する(終了を待たず、変更のたびに
 * 実機の電源断耐性を持たせる)。Settings退出時とテーマエディタ退出時の
 * 両方から呼ぶ共通ヘルパ (Issue #27)。Issue #47: app_capture_resume()も
 * ここに集約し、保存する箇所すべてでresume_*が最新化されるようにした。 */
static void app_save_config(app_t *app) {
    app_capture_resume(app); /* Issue #47 */
    if (app->config_path) {
        config_save(app->cfg, app->config_path);
    }
}

/* Settingsを抜けて呼び出し元の画面へ戻る。 */
static void app_leave_settings(app_t *app) {
    app->screen = app->settings_return;
    app_save_config(app);
}

/* Settings画面に出ている項目(SETTINGS[])だけを既定値に戻す (P10)。
 * last_path/gamecontroller_db/controller_mapping/sample_rateはSettings画面に
 * 出てこない値なので対象外にする(ユーザーの操作履歴やコントローラ設定を
 * 巻き込んで消してしまわないため)。config_set_defaults()で作った一時値から
 * SETTINGS[]に載っている分だけコピーする。
 * Issue #27: theme_customはSETTINGS[]に無い(Edit theme画面からしか
 * 触れない)が、Themeそのものが「全項目を既定値に戻す」対象である以上、
 * リセット後にThemeがcustomへ戻ったときだけ編集済みパレットが復活するのは
 * 驚きになる。ここで明示的にmidnightのコピーへ戻す。
 * Issue #47: start_folderも同じ理由でSETTINGS[]の一般巡回(SET_FOLDERは
 * 値を持たないので何もしない)には乗らないため、ここで明示的に空へ戻す。 */
static void app_reset_settings(app_t *app) {
    mugbs_config_t defaults;
    config_set_defaults(&defaults);
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        const setting_def_t *s = &SETTINGS[i];
        setting_set(app->cfg, s, setting_get(&defaults, s));
    }
    app->cfg->theme_custom = defaults.theme_custom;
    app->cfg->start_folder[0] = 0;
    app_apply_settings(app);
    /* show_all_filesも既定値に戻り得るので、adjust_setting()と同様に
     * Browser/再生コンテキストを作り直す。頻繁な操作ではないので、実際に
     * 変わったかどうかは問わず常に再走査する。 */
    browser_open_dir(&app->browser, app->browser.cwd, app->cfg->show_all_files);
    if (app->player_path[0]) app_sync_playctx(app, app->player_path, 1);
    set_status(app, "Settings reset to defaults");
}

/* Settings画面の"Edit theme"(A)で呼ばれる。実効テーマ(プリセットまたは
 * customパレット)をtheme_edit_workingへコピーして編集を始める。この時点
 * ではcfg->theme_id/theme_customには一切触れない(眺めるだけで設定が
 * 変わる副作用を避ける。copy-on-first-edit。docs/design-notes.md参照)。 */
static void app_enter_theme_edit(app_t *app) {
    app->theme_edit_entry_id = app->cfg->theme_id;
    app->theme_edit_entry_custom = app->cfg->theme_custom;
    theme_resolve(app->theme_edit_entry_id, &app->theme_edit_entry_custom, &app->theme_edit_working);
    app->theme_edit_dirty = (app->theme_edit_entry_id == THEME_CUSTOM);
    app->theme_edit_slot = 0;
    app->theme_edit_channel = 0;
    app->theme_edit_scroll = 0;
    app->screen = SCREEN_THEME_EDIT;
}

/* theme_edit_workingの変更をcfgへ反映する。初回だけtheme_idをCUSTOMへ
 * 切り替える(以後はdirty済みなので毎回上書きするだけ)。app_apply_theme()
 * が毎フレーム呼ばれているため、ここで書けば次フレームから即座に
 * 画面全体へライブプレビューされる。 */
static void theme_edit_commit(app_t *app) {
    if (!app->theme_edit_dirty) {
        app->cfg->theme_id = THEME_CUSTOM;
        app->theme_edit_dirty = 1;
    }
    app->cfg->theme_custom = app->theme_edit_working;
}

static void theme_edit_adjust_channel(app_t *app, int direction) {
    theme_color_t *c = &app->theme_edit_working.slot[app->theme_edit_slot];
    unsigned char *const channels[3] = { &c->r, &c->g, &c->b };
    unsigned char *ch = channels[app->theme_edit_channel];
    *ch = (unsigned char)theme_channel_step(*ch, direction);
    theme_edit_commit(app);
}

/* テーマエディタを抜けてSettingsへ戻る。config_pathがあればここで保存する
 * (app_leave_settings()と同じ理由。編集作業は長くなりがちなので、退出まで
 * 保存を待たせない)。 */
static void app_leave_theme_edit(app_t *app) {
    app->screen = SCREEN_SETTINGS;
    app_save_config(app);
}

static void handle_theme_edit_input(app_t *app, input_action_t a) {
    switch (a) {
        /* 端で折り返す(P9と同じ規約)。THEME_SLOT_COUNTはコンパイル時定数。 */
        case INPUT_UP:
            app->theme_edit_slot = (app->theme_edit_slot + THEME_SLOT_COUNT - 1) % THEME_SLOT_COUNT;
            break;
        case INPUT_DOWN:
            app->theme_edit_slot = (app->theme_edit_slot + 1) % THEME_SLOT_COUNT;
            break;
        case INPUT_L1:
            app->theme_edit_channel = (app->theme_edit_channel + 2) % 3; /* -1 mod 3 */
            break;
        case INPUT_R1:
            app->theme_edit_channel = (app->theme_edit_channel + 1) % 3;
            break;
        case INPUT_LEFT:
            theme_edit_adjust_channel(app, -1);
            break;
        case INPUT_RIGHT:
            theme_edit_adjust_channel(app, 1);
            break;
        case INPUT_X:
            /* 入室時の状態(theme_id/theme_customとも)へ完全に戻す。パレット
             * 1枚だけの機能なので、これがそのままundoとして機能する
             * (確認ダイアログは無し)。theme_edit_entry_customは「実効色」
             * ではなく生の値を保持しているので、入室時にtheme_idがCUSTOM
             * でなかった場合でも、無関係な過去のcustomパレットを実効色で
             * 上書きして消してしまうことがない。 */
            app->cfg->theme_id = app->theme_edit_entry_id;
            app->cfg->theme_custom = app->theme_edit_entry_custom;
            theme_resolve(app->theme_edit_entry_id, &app->theme_edit_entry_custom, &app->theme_edit_working);
            app->theme_edit_dirty = (app->theme_edit_entry_id == THEME_CUSTOM);
            break;
        case INPUT_B:
        case INPUT_START:
            app_leave_theme_edit(app);
            break;
        default:
            break;
    }
}

/* ---- フォルダ選択サブ画面 (SCREEN_FOLDER_PICK, Issue #47) --------------
 *
 * Settings画面の"Start folder"(A)で開く、ディレクトリのみを列挙する
 * (BROWSER_FILTER_DIRS)専用ブラウザ。app->browser/player_listとは別の
 * 3つ目のbrowser_tインスタンス(folder_pick)を使う理由は、Browser画面の
 * カーソル位置をこのサブ画面の探索で壊さないため(P9のplayer_listと同じ
 * 発想)。 */

/* 開始位置: 現在のstart_folder(設定済みなら)、無ければBrowserがいま見て
 * いるディレクトリ、それも開けなければカレントディレクトリ。 */
static void app_enter_folder_pick(app_t *app) {
    if (app->cfg->start_folder[0] &&
        browser_open_dir(&app->folder_pick, app->cfg->start_folder, BROWSER_FILTER_DIRS) == 0) {
        app->screen = SCREEN_FOLDER_PICK;
        return;
    }
    if (app->browser.cwd &&
        browser_open_dir(&app->folder_pick, app->browser.cwd, BROWSER_FILTER_DIRS) == 0) {
        app->screen = SCREEN_FOLDER_PICK;
        return;
    }
    if (browser_open_dir(&app->folder_pick, ".", BROWSER_FILTER_DIRS) == 0) {
        app->screen = SCREEN_FOLDER_PICK;
        return;
    }
    set_status(app, "Could not open folder picker");
}

static void handle_folder_pick_input(app_t *app, input_action_t a) {
    switch (a) {
        case INPUT_UP:    browser_move_wrap(&app->folder_pick, -1); break;
        case INPUT_DOWN:  browser_move_wrap(&app->folder_pick, 1); break;
        case INPUT_LEFT:  browser_page(&app->folder_pick, -list_visible_rows(app)); break;
        case INPUT_RIGHT: browser_page(&app->folder_pick, list_visible_rows(app)); break;
        case INPUT_A:
            browser_enter(&app->folder_pick, BROWSER_FILTER_DIRS);
            break;
        case INPUT_B:
            browser_up(&app->folder_pick, BROWSER_FILTER_DIRS);
            break;
        case INPUT_X: {
            /* いまのフォルダをstart_folderへ確定してSettingsへ戻る。 */
            size_t n = strlen(app->folder_pick.cwd);
            if (n >= sizeof(app->cfg->start_folder)) {
                set_status(app, "Path too long");
                break;
            }
            memcpy(app->cfg->start_folder, app->folder_pick.cwd, n + 1);
            app->screen = SCREEN_SETTINGS;
            app_save_config(app);
            break;
        }
        case INPUT_Y:
            /* start_folderを未設定に戻してSettingsへ戻る。 */
            app->cfg->start_folder[0] = 0;
            app->screen = SCREEN_SETTINGS;
            app_save_config(app);
            break;
        case INPUT_START:
            /* キャンセル。start_folderは変えずSettingsへ戻る。 */
            app->screen = SCREEN_SETTINGS;
            break;
        default:
            break;
    }
}

static void handle_settings_input(app_t *app, input_action_t a) {
    if (app->settings_confirm_reset) {
        /* リセット確認ダイアログの表示中。Yes/Noのみ受け付ける。 */
        switch (a) {
            case INPUT_A:
                app_reset_settings(app);
                app->settings_confirm_reset = 0;
                break;
            case INPUT_B:
            case INPUT_X:
            case INPUT_START:
                app->settings_confirm_reset = 0;
                break;
            default:
                break;
        }
        return;
    }

    switch (a) {
        /* 端で折り返す(P9)。SETTINGS_COUNT はコンパイル時定数なので0にならない。 */
        case INPUT_UP:
            app->settings_sel = (app->settings_sel + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
            break;
        case INPUT_DOWN:
            app->settings_sel = (app->settings_sel + 1) % SETTINGS_COUNT;
            break;
        case INPUT_LEFT:
            adjust_setting(app, -1);
            break;
        case INPUT_RIGHT:
            adjust_setting(app, 1);
            break;
        case INPUT_A: /* 親指1本で右方向へ回せるようにする。ただし
                       * SET_ACTION行(Edit theme)・SET_FOLDER行(Start folder)
                       * ではサブ画面を開く方に意味が変わる (Issue #27, #47)。 */
            if (SETTINGS[app->settings_sel].kind == SET_ACTION) {
                app_enter_theme_edit(app);
            } else if (SETTINGS[app->settings_sel].kind == SET_FOLDER) {
                app_enter_folder_pick(app);
            } else {
                adjust_setting(app, 1);
            }
            break;
        case INPUT_X:
            app->settings_confirm_reset = 1;
            break;
        case INPUT_B:
        case INPUT_START:
            app_leave_settings(app);
            break;
        default:
            break;
    }
}

/* repeat: 直前に input_poll() が返した a が長押しリピート由来だったか
 * (input_last_was_repeat()。--ui-script経由では常に0)。Player画面の
 * UP/DOWNだけがこれを見る(handle_player_input()参照)。 */
static void app_dispatch(app_t *app, input_action_t a, int repeat) {
    if (a == INPUT_NONE) return;
    if (a == INPUT_QUIT) {
        app->running = 0;
        return;
    }
    switch (app->screen) {
        case SCREEN_BROWSER:     handle_browser_input(app, a); break;
        case SCREEN_PLAYER:      handle_player_input(app, a, repeat); break;
        case SCREEN_TRACKLIST:   handle_tracklist_input(app, a); break;
        case SCREEN_SETTINGS:    handle_settings_input(app, a); break;
        case SCREEN_THEME_EDIT:  handle_theme_edit_input(app, a); break;
        case SCREEN_FOLDER_PICK: handle_folder_pick_input(app, a); break;
    }
}

/* ---- 描画 --------------------------------------------------------------- */

static const char *browser_item_text(void *ctx, int index) {
    app_t *app = (app_t *)ctx;
    static char buf[300];
    const browser_item_t *it = &app->browser.items[index];
    if (it->is_dir) {
        snprintf(buf, sizeof(buf), "[%s]", it->name);
    } else {
        snprintf(buf, sizeof(buf), "%s", it->name);
    }
    return buf;
}

/* フォルダ選択サブ画面(SCREEN_FOLDER_PICK, Issue #47)。BROWSER_FILTER_DIRS
 * で開いているためitems[]はすべてディレクトリだが、browser_item_text()と
 * 表示を揃えるため同じ"[name]"角括弧にする。 */
static const char *folder_pick_item_text(void *ctx, int index) {
    app_t *app = (app_t *)ctx;
    static char buf[300];
    snprintf(buf, sizeof(buf), "[%s]", app->folder_pick.items[index].name);
    return buf;
}

static const char *tracklist_item_text(void *ctx, int index) {
    app_t *app = (app_t *)ctx;
    static char buf[300];
    const playlist_entry_t *e = &app->pl->entries[index];
    snprintf(buf, sizeof(buf), "%3d. %s", index + 1, e->title);
    return buf;
}

/* Issue #7: タイトル行の右端に残量ゲージを描き、確保した幅(px)を返す。
 * 0なら何も描いていない(呼び出し側は幅を削らなくてよい)。
 * right_x は行の右端(exclusive。4画面とも ui->screen_w - pad で揃える)、
 * row_y/row_h はその行の矩形(縦センタリングに使う)。
 * ユーザー確認済み: ゲージのみを描き、数値("85%"等)は出さない
 * (8x8フォントはASCIIのみで絵文字が無く、数値を出すなら桁数で幅が
 * 揺れないよう固定幅確保が要るが、ゲージのみならその心配が無い)。
 * 幅は screen_w/4 を超える極端に狭い解像度では0を返し、
 * バッテリーよりパス/曲名等の本文を優先する。 */
static int draw_battery(app_t *app, int right_x, int row_y, int row_h) {
    if (!app->battery_visible) return 0;

    ui_t *ui = &app->ui;
    const int g = ui_glyph_size(ui, UI_TEXT_BODY);
    const int pad = ui->metrics.pad;
    int gauge_w = g * 2;
    if (gauge_w > ui->screen_w / 4) return 0;

    const SDL_Color c_ok  = ui_color(ui, THEME_ROLE_DIM);  /* 通常。他画面のdimと同ロール */
    const SDL_Color c_low = ui_color(ui, THEME_ROLE_WARN); /* 低下。他画面のwarnと同ロール */
    const SDL_Color c_chg = ui_color(ui, THEME_ROLE_OK);   /* 充電中 */
    SDL_Color c = app->battery_status.charging ? c_chg
                 : battery_is_low(&app->battery_status, app->battery_low_pct) ? c_low
                 : c_ok;

    ui_rect_t box = { right_x - gauge_w, row_y + (row_h - g) / 2, gauge_w, g };
    ui_draw_rect(ui, box, c);

    int inset = pad / 2;
    if (inset < 1) inset = 1;
    int fill_w = (box.w - inset * 2) * app->battery_status.percent / 100;
    if (fill_w > 0) {
        ui_rect_t f = { box.x + inset, box.y + inset, fill_w, box.h - inset * 2 };
        ui_fill_rect(ui, f, c);
    }

    return gauge_w;
}

static void draw_browser(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = ui_color(ui, THEME_ROLE_BG);
    const SDL_Color bar_bg = ui_color(ui, THEME_ROLE_PANEL);
    const SDL_Color fg = ui_color(ui, THEME_ROLE_FG);
    const SDL_Color dim = ui_color(ui, THEME_ROLE_DIM);
    const SDL_Color accent = ui_color(ui, THEME_ROLE_ACCENT);
    const SDL_Color err = ui_color(ui, THEME_ROLE_WARN);

    ui_clear(ui, bg);

    ui_rect_t title_row = ui_header_title_row(ui);
    int bat_w = draw_battery(app, ui->screen_w - ui->metrics.pad, title_row.y, title_row.h);

    char counter[32];
    if (app->browser.count > 0) {
        snprintf(counter, sizeof(counter), "%d / %d", app->browser.selected + 1, app->browser.count);
    } else {
        counter[0] = 0;
    }
    ui_header_t hdr = {
        .title = app->browser.cwd ? path_leaf(app->browser.cwd) : "",
        .subtitle = app->browser.cwd ? app->browser.cwd : "",
        .counter = counter,
        .right_reserve = bat_w,
        .title_color = fg, .sub_color = dim, .counter_color = accent, .bar_color = bar_bg,
    };
    ui_draw_header(ui, &hdr);

    ui_rect_t list = list_rect(app);
    ui_draw_list(ui, list, app->browser.count, app->browser.selected, -1,
                 &app->browser.scroll, browser_item_text, NULL, app);

    ui_footer_t ftr = {
        .line1 = "A:Open  B:Up  Start:Menu",
        .line2 = "<>:Page  Start+Select:Quit",
        .line1_color = fg, .line2_color = accent, .bar_color = bar_bg,
    };
    if (app->status[0] && SDL_GetTicks() < app->status_until) {
        ftr.line1 = app->status;
        ftr.line1_color = err;
    }
    ui_draw_footer(ui, &ftr);
}

static void draw_player(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = ui_color(ui, THEME_ROLE_BG);
    const SDL_Color fg = ui_color(ui, THEME_ROLE_FG);
    const SDL_Color dim = ui_color(ui, THEME_ROLE_DIM);
    const SDL_Color accent = ui_color(ui, THEME_ROLE_ACCENT);
    const SDL_Color err = ui_color(ui, THEME_ROLE_WARN);

    ui_clear(ui, bg);

    int x = ui->metrics.pad;
    int y = ui->metrics.pad;
    int content_w = ui->screen_w - ui->metrics.pad * 2;

    const playlist_entry_t *e = NULL;
    const playlist_source_t *src = NULL;
    if (app->pl && app->player.current_entry >= 0) {
        e = &app->pl->entries[app->player.current_entry];
        src = &app->pl->sources[e->source_index];
    }

    /* Issue #3: 情報ブロックの文字サイズ。実機(640x480, scale=1.0)で
     * UI_TEXT_SMALL は 8x8フォントの等倍=8pxで、トラック番号まで
     * それに載っていて読めなかった。曲名は TITLE(3x) のまま、
     * その他はリスト行と同じ BODY(2x) へ上げてある。SMALL のままにしたのは
     * フッタだけ。8x8フォントなのでサイズ段階はどれも8の整数倍で、
     * ドットが均一に拡大される。送り量は各行が実際に使うサイズ段階から
     * 導出する(SPEC 6.2)。
     * Issue #8: 再生位置はTITLEから曲名と同格ではなくBODYへ落とし、
     * シークバーと同じ行に収めた(下記)。曲名は見切れやすいという
     * フィードバックを受け、[ui] title_scroll(既定on)なら横スクロール、
     * offなら従来どおり "..." 省略で表示する。 */
    const char *title = e ? e->title : "(no track)";
    /* Issue #7: バッテリーは曲名の行にだけ食い込ませる(以降の行は
     * タイトル行より下から始まるので content_w のまま)。 */
    int bat_w = draw_battery(app, x + content_w, y, ui_glyph_size(ui, UI_TEXT_TITLE));
    int title_w = content_w - (bat_w ? bat_w + ui->metrics.pad : 0);
    if (app->cfg->title_scroll) {
        ui_text_scroll(ui, x, y, title_w, UI_TEXT_TITLE, fg, title, SDL_GetTicks());
    } else {
        ui_text_clipped(ui, x, y, title_w, UI_TEXT_TITLE, fg, title);
    }
    y += ui_glyph_size(ui, UI_TEXT_TITLE) + ui->metrics.pad;

    /* Issue #47: ゲーム名・作者/著作権のどちらか(または両方)が空の
     * ヘッダしか持たないファイル(素のGBS/NSF等)で、空行が居座って
     * 波形側の余白を無駄に食わないよう、無い行は描かず詰める。 */
    if (app->pl && app->pl->game && app->pl->game[0]) {
        ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, dim, app->pl->game);
        y += ui->metrics.line_h;
    }

    int has_author = src && src->author[0];
    int has_copyright = src && src->copyright[0];
    if (has_author || has_copyright) {
        char meta[256];
        if (has_author && has_copyright) {
            snprintf(meta, sizeof(meta), "%s  %s", src->author, src->copyright);
        } else {
            snprintf(meta, sizeof(meta), "%s", has_author ? src->author : src->copyright);
        }
        ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, dim, meta);
        y += ui->metrics.line_h;
    }
    y += ui->metrics.pad;

    char trackno[32];
    snprintf(trackno, sizeof(trackno), "Track %d/%d",
             app->pl ? app->player.current_entry + 1 : 0, app->pl ? app->pl->entry_count : 0);
    ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, dim, trackno);
    y += ui->metrics.line_h;

    int pos_ms = player_tell_ms(&app->player);
    int dur_ms = player_current_duration_ms(&app->player);
    char timebuf[64];
    /* Issue #15: dur_ms==0 は「未再生」だけでなく「REPEAT_ONEでフェードが
     * 無効(エンドレス)」も表す(player_current_duration_ms()参照)。
     * どちらも長さが定まらないので合計側を "--:--" にし、以下でシーク
     * バー自体を描かない。 */
    if (dur_ms > 0) {
        snprintf(timebuf, sizeof(timebuf), "%d:%02d / %d:%02d",
                 pos_ms / 60000, (pos_ms / 1000) % 60, dur_ms / 60000, (dur_ms / 1000) % 60);
    } else {
        snprintf(timebuf, sizeof(timebuf), "%d:%02d / --:--",
                 pos_ms / 60000, (pos_ms / 1000) % 60);
    }

    /* Issue #8: 再生位置とシークバーを同じ行に収める(時間が左、バーが
     * 残り幅いっぱい)。長尺のm3u(時間が3桁分)で時間の幅が伸びても、
     * バーはその分だけ縮むので画面外へは出ない。バーの残り幅が
     * 極端に狭くなる解像度では2行構成へ戻し、フッタへはみ出させない
     * (SPEC 6.2)。 */
    int time_glyph = ui_glyph_size(ui, UI_TEXT_BODY);
    int bar_h = ui->metrics.pad * 2;
    const SDL_Color bar_bg = ui_color(ui, THEME_ROLE_GUTTER);

    if (dur_ms <= 0) {
        /* Issue #15: 長さが無いのでバーは描かず、時間表示だけの1行にする。 */
        ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, fg, timebuf);
        y += ui->metrics.line_h;
    } else {
        int time_w = ui_text_width(ui, UI_TEXT_BODY, timebuf);
        float ratio = (float)pos_ms / (float)dur_ms;
        int bar_w = content_w - time_w - ui->metrics.pad * 2;

        if (bar_w >= ui->metrics.pad * 4) {
            int row_h = ui->metrics.line_h;
            int text_y = y + (row_h - time_glyph) / 2;
            ui_text(ui, x, text_y, UI_TEXT_BODY, fg, timebuf);
            ui_rect_t bar = { x + time_w + ui->metrics.pad * 2, y + (row_h - bar_h) / 2, bar_w, bar_h };
            ui_draw_progress(ui, bar, ratio, accent, bar_bg);
            y += row_h + ui->metrics.pad;
        } else {
            ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, fg, timebuf);
            y += ui->metrics.line_h;
            ui_rect_t bar = { x, y, content_w, bar_h };
            ui_draw_progress(ui, bar, ratio, accent, bar_bg);
            y += bar.h + ui->metrics.pad;
        }
    }

    const char *repeat_label = app->cfg->repeat_mode == REPEAT_ONE ? "one" :
                                app->cfg->repeat_mode == REPEAT_ALL ? "all" : "none";
    const char *state_label = app->player.state == PLAYER_PAUSED ? "PAUSED" :
                               app->player.state == PLAYER_PLAYING ? "PLAYING" : "STOPPED";
    char status_line[128];
    int len_written = snprintf(status_line, sizeof(status_line), "%s  repeat:%s  shuffle:%s",
             state_label, repeat_label, app->cfg->shuffle ? "on" : "off");
    /* Issue #19: Lengthがauto(0)のときは今までどおり何も足さない
     * (この行の文字数を常時食わないため)。上書き中だけ末尾へ足す。
     * len_written は snprintf が「切り詰めなければ書いていたはずの長さ」を
     * 返す(truncateされていれば sizeof(status_line) 以上になり得る)ため、
     * バッファ内に収まっている場合だけ追記する(収まらなければ何もしない
     * -- サイズ引き算のアンダーフローを避ける安全側の判断)。 */
    if (len_written > 0 && (size_t)len_written < sizeof(status_line) &&
        app->cfg->length_override_sec > 0) {
        snprintf(status_line + len_written, sizeof(status_line) - (size_t)len_written,
                 "  len:%dm", app->cfg->length_override_sec / 60);
    }
    ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, dim, status_line);
    y += ui->metrics.line_h;

    /* Issue #47: ステータス行の下からフッタ帯の上までを丸ごとビジュア
     * ライザ(F-14)に使う。P9〜P10で共有していた同ディレクトリのファイル
     * 一覧UIは撤去した(同じ操作はUP/DOWNでのファイル送りに一本化。
     * app_player_step_file()参照)。一時ステータスメッセージ用の行は
     * 固定確保せず、下記でフッタ直上へのオーバーレイとして描く。 */
    const int row_h = ui->metrics.line_h;
    int band_h = (ui->screen_h - ui->metrics.footer_h) - y - ui->metrics.pad;
    if (band_h < 0) band_h = 0;
    int wave_rows = band_h / row_h;

    /* F-14 簡易ビジュアライザ。低解像度で最低限の高さも取れない場合は
     * 描かない(フッタへはみ出させない。SPEC 6.2)。 */
    if (wave_rows >= PLAYER_WAVE_MIN_ROWS) {
        ui_rect_t wave = { x, y, content_w, wave_rows * row_h };
        const SDL_Color wave_bg = ui_color(ui, THEME_ROLE_SUNKEN);
        ui_draw_waveform(ui, wave, app->scope, app->scope_len, accent, wave_bg);
    }

    /* 一時ステータスメッセージはフッタ帯の直上にオーバーレイとして描く
     * (P10: 波形が下まで伸びるようになったぶん、専用の行はもう確保しない)。
     * 波形の上に重なっても読めるよう、背景を塗ってから文字を出す。 */
    if (app->status[0] && SDL_GetTicks() < app->status_until) {
        ui_rect_t msg_bg_rect = { x, (ui->screen_h - ui->metrics.footer_h) - row_h,
                                   content_w, row_h };
        const SDL_Color msg_bg = ui_color(ui, THEME_ROLE_PANEL);
        ui_fill_rect(ui, msg_bg_rect, msg_bg);
        int msg_y = msg_bg_rect.y + (row_h - ui_glyph_size(ui, UI_TEXT_BODY)) / 2;
        ui_text_clipped(ui, x, msg_y, content_w, UI_TEXT_BODY, err, app->status);
    }

    /* Issue #41: 他4画面と同じ2行フッタへ揃える。終了操作
     * (SPEC 6.3「Menu長押し=終了」+ P6で追加したStart+Select代替)は
     * GUIDEボタンがmuOS側のオーバーレイに吸われて効かない可能性がある
     * ため、常に見える2行目に明記する(以前は1行に相乗りさせていた
     * display_pathとの共存で、狭い解像度だと後ろのpathだけが削れて
     * いたが、専用の行になったことでどちらも収まりやすくなった)。 */
    char footer_line2[512];
    snprintf(footer_line2, sizeof(footer_line2), "Start+Select:Quit  %s",
             src ? src->display_path : "");
    ui_footer_t ftr = {
        /* Issue #47: Up/Downはファイル一覧UIの撤去に伴い「同ディレクトリの
         * 前/次ファイル」へ転用した(app_player_step_file())。 */
        .line1 = "<>:Track  ^v:File  Select:Pause  X:Tracks  Start:Settings",
        .line2 = footer_line2,
        .line1_color = fg, .line2_color = accent, .bar_color = ui_color(ui, THEME_ROLE_PANEL),
    };
    ui_draw_footer(ui, &ftr);
}

static void draw_tracklist(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = ui_color(ui, THEME_ROLE_BG);
    const SDL_Color bar_bg = ui_color(ui, THEME_ROLE_PANEL);
    const SDL_Color fg = ui_color(ui, THEME_ROLE_FG);
    const SDL_Color dim = ui_color(ui, THEME_ROLE_DIM);
    const SDL_Color accent = ui_color(ui, THEME_ROLE_ACCENT);

    ui_clear(ui, bg);

    ui_rect_t title_row = ui_header_title_row(ui);
    int bat_w = draw_battery(app, ui->screen_w - ui->metrics.pad, title_row.y, title_row.h);

    const char *title = "Tracks";
    const char *subtitle = "";
    if (app->pl) {
        if (app->pl->game && app->pl->game[0]) {
            title = app->pl->game;
        } else if (app->pl->source_count > 0) {
            title = path_leaf(app->pl->sources[0].display_path);
        }
        if (app->pl->source_count > 0) subtitle = app->pl->sources[0].display_path;
    }
    char counter[32];
    if (app->pl && app->pl->entry_count > 0) {
        snprintf(counter, sizeof(counter), "%d / %d", app->tracklist_sel + 1, app->pl->entry_count);
    } else {
        counter[0] = 0;
    }
    ui_header_t hdr = {
        .title = title, .subtitle = subtitle, .counter = counter, .right_reserve = bat_w,
        .title_color = fg, .sub_color = dim, .counter_color = accent, .bar_color = bar_bg,
    };
    ui_draw_header(ui, &hdr);

    if (app->pl) {
        ui_rect_t list = list_rect(app);
        ui_draw_list(ui, list, app->pl->entry_count, app->tracklist_sel,
                     app->player.current_entry, &app->tracklist_scroll,
                     tracklist_item_text, NULL, app);
    }

    ui_footer_t ftr = {
        .line1 = "A:Play  B/X:Back", .line2 = "Start+Select:Quit",
        .line1_color = fg, .line2_color = accent, .bar_color = bar_bg,
    };
    ui_draw_footer(ui, &ftr);
}

static const char *settings_item_text(void *ctx, int index) {
    app_t *app = (app_t *)ctx;
    static char buf[300];
    const setting_def_t *s = &SETTINGS[index];
    double v = setting_get(app->cfg, s);

    char valbuf[64];
    switch (s->kind) {
        case SET_INT:
            snprintf(valbuf, sizeof(valbuf), "%d", (int)v);
            break;
        case SET_DOUBLE:
            snprintf(valbuf, sizeof(valbuf), "%.2f", v);
            break;
        case SET_BOOL:
            snprintf(valbuf, sizeof(valbuf), "%s", ((int)v) ? "on" : "off");
            break;
        case SET_ENUM: {
            int iv = (int)v;
            const char *name = (iv >= 0 && iv < s->enum_count) ? s->enum_names[iv] : "?";
            snprintf(valbuf, sizeof(valbuf), "%s", name);
            break;
        }
        case SET_MINUTES: {
            /* Issue #16: 秒→分。整数分ならそのまま("3 min")、config.iniに
             * 残っている端数秒(旧既定の150秒等)は小数第1位まで出す
             * ("2.5 min")。adjust_setting()を1回でも通せば以後は必ず
             * 整数分になる。 */
            int sec = (int)v;
            if (sec % 60 == 0) {
                snprintf(valbuf, sizeof(valbuf), "%d min", sec / 60);
            } else {
                snprintf(valbuf, sizeof(valbuf), "%.1f min", sec / 60.0);
            }
            break;
        }
        case SET_LENGTH: {
            /* Issue #19: 0は「上書きしない」= auto。それ以外はSET_MINUTES
             * と同じ表示ルール(常に5分刻みなので端数は理論上出ないが、
             * config.iniを手で書き換えた場合の保険として同じ分岐にしておく)。 */
            int sec = (int)v;
            if (sec == 0) {
                snprintf(valbuf, sizeof(valbuf), "auto");
            } else if (sec % 60 == 0) {
                snprintf(valbuf, sizeof(valbuf), "%d min", sec / 60);
            } else {
                snprintf(valbuf, sizeof(valbuf), "%.1f min", sec / 60.0);
            }
            break;
        }
        case SET_SECONDS: {
            /* Issue #21: 0は「隠さない」= off。それ以外は秒のまま表示する
             * (1秒刻みなので分表示にする意味が薄い。3秒/5秒のようなIssueに
             * 挙がった具体値がそのまま読める方が分かりやすい)。 */
            int sec = (int)v;
            if (sec == 0) {
                snprintf(valbuf, sizeof(valbuf), "off");
            } else {
                snprintf(valbuf, sizeof(valbuf), "%ds", sec);
            }
            break;
        }
        case SET_ACTION:
            /* Issue #27: 値欄ではなく「開ける」ことを示す記号だけ出す。 */
            snprintf(valbuf, sizeof(valbuf), ">");
            break;
        case SET_FOLDER:
            /* Issue #47: 未設定なら"(not set)"、設定済みならフォルダ名
             * (フルパスはヘッダのサブタイトルに出すPlayer/Browserと違い、
             * この1行に収める都合上末尾要素だけにする)+開けることを示す
             * ">" を付ける。 */
            snprintf(valbuf, sizeof(valbuf), "%s >",
                     app->cfg->start_folder[0] ? path_leaf(app->cfg->start_folder) : "(not set)");
            break;
    }
    /* 等幅8x8フォントなので固定幅のラベル列が ui.c を触らずに揃う。 */
    snprintf(buf, sizeof(buf), "%-18s %s", s->label, valbuf);
    return buf;
}

/* Issue #43: needs_effects項目(Stereo depth/EQ bass/EQ treble)は、現在
 * 再生中のソースがSPC(playlist_effects_supported()==0)のとき値が
 * 効かない。settings_item_text()の値そのものは変えない
 * (LEFT/RIGHTでの編集はGBS/NSFへ戻れば効くグローバル設定なので禁止
 * しない)。ui_draw_list()のdim_fnへ渡し、行の文字色をTHEME_ROLE_DIMへ
 * 差し替えて示す(停止中・SPC以外は従来どおり通常色)。 */
static int settings_item_dim(void *ctx, int index) {
    app_t *app = (app_t *)ctx;
    const setting_def_t *s = &SETTINGS[index];
    if (!s->needs_effects || !app->pl || app->player.current_entry < 0) return 0;
    int src_idx = app->pl->entries[app->player.current_entry].source_index;
    return !playlist_effects_supported(app->pl, src_idx);
}

/* Xで開くリセット確認ダイアログ (P10)。draw_settings()の最後、通常の
 * リスト/フッタの上に重ねて描く。 */
static void draw_settings_confirm_reset(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color box_bg = ui_color(ui, THEME_ROLE_BOX_BG);
    const SDL_Color box_border = ui_color(ui, THEME_ROLE_WARN);
    const SDL_Color fg = ui_color(ui, THEME_ROLE_FG);
    const SDL_Color dim = ui_color(ui, THEME_ROLE_DIM);
    const char *line1 = "Reset all settings to defaults?";
    const char *line2 = "A: Yes      B: No";

    int pad = ui->metrics.pad;
    int box_w = ui_text_width(ui, UI_TEXT_BODY, line1) + pad * 4;
    int min_w = ui_text_width(ui, UI_TEXT_SMALL, line2) + pad * 4;
    if (min_w > box_w) box_w = min_w;
    if (box_w > ui->screen_w - pad * 2) box_w = ui->screen_w - pad * 2;
    int box_h = ui->metrics.line_h * 2 + pad * 4;
    ui_rect_t box = { (ui->screen_w - box_w) / 2, (ui->screen_h - box_h) / 2, box_w, box_h };

    ui_fill_rect(ui, box, box_bg);
    ui_draw_rect(ui, box, box_border);

    int ty = box.y + pad * 2;
    ui_text_clipped(ui, box.x + pad * 2, ty, box.w - pad * 4, UI_TEXT_BODY, fg, line1);
    ty += ui->metrics.line_h + pad;
    ui_text_clipped(ui, box.x + pad * 2, ty, box.w - pad * 4, UI_TEXT_SMALL, dim, line2);
}

static void draw_settings(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = ui_color(ui, THEME_ROLE_BG);
    const SDL_Color bar_bg = ui_color(ui, THEME_ROLE_PANEL);
    const SDL_Color fg = ui_color(ui, THEME_ROLE_FG);
    const SDL_Color dim = ui_color(ui, THEME_ROLE_DIM);
    const SDL_Color accent = ui_color(ui, THEME_ROLE_ACCENT);
    const SDL_Color err = ui_color(ui, THEME_ROLE_WARN);

    ui_clear(ui, bg);

    ui_rect_t title_row = ui_header_title_row(ui);
    int bat_w = draw_battery(app, ui->screen_w - ui->metrics.pad, title_row.y, title_row.h);

    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "muChip %s", MUCHIP_VERSION);
    char counter[32];
    snprintf(counter, sizeof(counter), "%d / %d", app->settings_sel + 1, SETTINGS_COUNT);
    ui_header_t hdr = {
        .title = "Settings", .subtitle = subtitle, .counter = counter, .right_reserve = bat_w,
        .title_color = fg, .sub_color = dim, .counter_color = accent, .bar_color = bar_bg,
    };
    ui_draw_header(ui, &hdr);

    ui_rect_t list = list_rect(app);
    ui_draw_list(ui, list, SETTINGS_COUNT, app->settings_sel, -1,
                 &app->settings_scroll, settings_item_text, settings_item_dim, app);

    /* Issue #27/#47: SET_ACTION行(Edit theme)・SET_FOLDER行(Start folder)
     * ではLEFT/RIGHTが無反応なので "L/R:Adjust" は誤解を招く。選択行に
     * 応じてフッタを差し替える。 */
    int is_open_row = SETTINGS[app->settings_sel].kind == SET_ACTION ||
                       SETTINGS[app->settings_sel].kind == SET_FOLDER;
    ui_footer_t ftr = {
        .line1 = is_open_row ? "A:Open  B/START:Back" : "L/R:Adjust  B/START:Back",
        .line2 = "X:Reset all",
        .line1_color = fg, .line2_color = accent, .bar_color = bar_bg,
    };
    if (app->status[0] && SDL_GetTicks() < app->status_until) {
        ftr.line1 = app->status;
        ftr.line1_color = err;
    }
    ui_draw_footer(ui, &ftr);

    if (app->settings_confirm_reset) {
        draw_settings_confirm_reset(app);
    }
}

/* Issue #27: 9スロットをラベル+R/G/B値+スウォッチで並べる。ui_draw_list()
 * ではなく専用ループを使う理由: 1行の中で選択中チャンネルだけ色や記号を
 * 変えたい・可視幅からスウォッチ分を除きたい、がui_list_item_fnの
 * 「1行=1文字列+1色」という契約では表現できないため(docs/design-notes.md
 * 参照)。ただし可視行数とスクロールクランプはui_draw_list()と同じ
 * ui_list_visible_rows()/ui_list_clamp_scroll()を再利用し、挙動を揃える。 */
static void draw_theme_edit(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = ui_color(ui, THEME_ROLE_BG);
    const SDL_Color panel = ui_color(ui, THEME_ROLE_PANEL);
    const SDL_Color sel_bg = ui_color(ui, THEME_ROLE_SEL);

    /* bg/fgそのものが編集対象になりうる画面なので、行の文字とフッタだけは
     * theme_best_on(bg, 黒, 白)で必ず読める色を選ぶ(theme.h参照。
     * ハイライト矩形の背景色(sel_bg)は通常どおりテーマ由来のままにする
     * ―― 万一selがbgに溶けても、文字自体は読めるしB/Xで必ず抜けられる)。 */
    theme_color_t safe_c = theme_best_on(theme_role_color(&ui->theme, THEME_ROLE_BG),
                                          (theme_color_t){ 0, 0, 0 }, (theme_color_t){ 255, 255, 255 });
    const SDL_Color safe_fg = { safe_c.r, safe_c.g, safe_c.b, 255 };

    ui_clear(ui, bg);

    char counter[32];
    snprintf(counter, sizeof(counter), "%d / %d", app->theme_edit_slot + 1, THEME_SLOT_COUNT);
    ui_header_t hdr = {
        .title = "Edit theme", .subtitle = "custom palette", .counter = counter, .right_reserve = 0,
        .title_color = safe_fg, .sub_color = safe_fg, .counter_color = safe_fg, .bar_color = panel,
    };
    ui_draw_header(ui, &hdr);

    ui_rect_t list = list_rect(app);
    int visible = ui_list_visible_rows(ui, list);
    ui_list_clamp_scroll(ui, list, THEME_SLOT_COUNT, app->theme_edit_slot, &app->theme_edit_scroll);

    int row_h = ui->metrics.line_h;
    int glyph = ui_glyph_size(ui, UI_TEXT_BODY);
    int pad = ui->metrics.pad;
    int swatch_w = glyph * 2;

    for (int row = 0; row < visible; row++) {
        int idx = app->theme_edit_scroll + row;
        if (idx >= THEME_SLOT_COUNT) break;
        int y = list.y + row * row_h;
        theme_color_t c = app->theme_edit_working.slot[idx];

        if (idx == app->theme_edit_slot) {
            ui_rect_t hi = { list.x, y, list.w, row_h };
            ui_fill_rect(ui, hi, sel_bg);
        }

        /* 選択中チャンネルの文字の前にだけ'>'を付けて示す(色そのものでの
         * 強調はスウォッチが担う)。等幅フォントで揃うよう固定幅で書く。 */
        int on_row = (idx == app->theme_edit_slot);
        char rc = (on_row && app->theme_edit_channel == 0) ? '>' : ' ';
        char gc = (on_row && app->theme_edit_channel == 1) ? '>' : ' ';
        char bc = (on_row && app->theme_edit_channel == 2) ? '>' : ' ';
        char buf[64];
        snprintf(buf, sizeof(buf), "%-11s%cR%3d %cG%3d %cB%3d",
                 theme_slot_label((theme_slot_t)idx), rc, c.r, gc, c.g, bc, c.b);

        int text_max_w = list.w - pad - swatch_w - pad;
        ui_text_clipped(ui, list.x + pad, y + (row_h - glyph) / 2, text_max_w,
                         UI_TEXT_BODY, safe_fg, buf);

        /* スウォッチ: draw_battery()と同じ「矩形を塗って枠を描く」レシピ。
         * 枠はtheme_best_on(スロット色, 黒, 白)で選ぶ(bgスロット自身の
         * スウォッチが背景に溶けて見えなくなるのを防ぐ)。 */
        SDL_Color swatch_fill = { c.r, c.g, c.b, 255 };
        theme_color_t border_c = theme_best_on(c, (theme_color_t){ 0, 0, 0 }, (theme_color_t){ 255, 255, 255 });
        SDL_Color swatch_border = { border_c.r, border_c.g, border_c.b, 255 };
        ui_rect_t swatch = { list.x + list.w - pad - swatch_w, y + (row_h - glyph) / 2, swatch_w, glyph };
        ui_fill_rect(ui, swatch, swatch_fill);
        ui_draw_rect(ui, swatch, swatch_border);
    }

    ui_footer_t ftr = {
        .line1 = "<>:Adjust  L/R:Channel", .line2 = "X:Revert  B:Back",
        .line1_color = safe_fg, .line2_color = safe_fg, .bar_color = panel,
    };
    ui_draw_footer(ui, &ftr);
}

/* フォルダ選択サブ画面(SCREEN_FOLDER_PICK, Issue #47)。Browser画面
 * (draw_browser())と同じ構図(ヘッダ2段組+リスト+フッタ2行)だが、
 * BROWSER_FILTER_DIRSで開いているためディレクトリしか出ない。 */
static void draw_folder_pick(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = ui_color(ui, THEME_ROLE_BG);
    const SDL_Color bar_bg = ui_color(ui, THEME_ROLE_PANEL);
    const SDL_Color fg = ui_color(ui, THEME_ROLE_FG);
    const SDL_Color dim = ui_color(ui, THEME_ROLE_DIM);
    const SDL_Color accent = ui_color(ui, THEME_ROLE_ACCENT);
    const SDL_Color err = ui_color(ui, THEME_ROLE_WARN);

    ui_clear(ui, bg);

    ui_rect_t title_row = ui_header_title_row(ui);
    int bat_w = draw_battery(app, ui->screen_w - ui->metrics.pad, title_row.y, title_row.h);

    char counter[32];
    if (app->folder_pick.count > 0) {
        snprintf(counter, sizeof(counter), "%d / %d",
                  app->folder_pick.selected + 1, app->folder_pick.count);
    } else {
        counter[0] = 0;
    }
    ui_header_t hdr = {
        .title = "Start folder",
        .subtitle = app->folder_pick.cwd ? app->folder_pick.cwd : "",
        .counter = counter, .right_reserve = bat_w,
        .title_color = fg, .sub_color = dim, .counter_color = accent, .bar_color = bar_bg,
    };
    ui_draw_header(ui, &hdr);

    ui_rect_t list = list_rect(app);
    ui_draw_list(ui, list, app->folder_pick.count, app->folder_pick.selected, -1,
                 &app->folder_pick.scroll, folder_pick_item_text, NULL, app);

    ui_footer_t ftr = {
        .line1 = "A:Open  B:Up  X:Use this folder",
        .line2 = "Y:Clear  Start:Cancel",
        .line1_color = fg, .line2_color = accent, .bar_color = bar_bg,
    };
    if (app->status[0] && SDL_GetTicks() < app->status_until) {
        ftr.line1 = app->status;
        ftr.line1_color = err;
    }
    ui_draw_footer(ui, &ftr);
}

/* ---- 起動時のBrowser開始位置 (F-13) ------------------------------------- */

/* last_path をまずディレクトリとして開いてみて、失敗したら「ファイルの
 * パスだった」とみなし、親ディレクトリを開いてそのファイルへカーソルを
 * 合わせる(自動再生はしない -- 起動と同時に音が出る驚きを避けるため、
 * SPEC 7 のlast_pathサンプル値もディレクトリである)。
 * それも失敗する場合(削除された等)はカレントディレクトリにフォールバック
 * する。 */
static void restore_last_path(app_t *app, const char *last_path) {
    if (browser_open_dir(&app->browser, last_path, app->cfg->show_all_files) == 0) {
        return;
    }

    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", last_path);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        /* スラッシュを含まない相対パス。カレントディレクトリを親とみなす。 */
        if (browser_open_dir(&app->browser, ".", app->cfg->show_all_files) == 0) {
            browser_select_by_name(&app->browser, last_path);
        }
        return;
    }
    const char *base = slash + 1;
    if (slash == dir) dir[1] = 0; /* "/foo.gbs" -> "/" */
    else *slash = 0;

    if (browser_open_dir(&app->browser, dir, app->cfg->show_all_files) == 0) {
        browser_select_by_name(&app->browser, base);
        return;
    }

    LOG_WARN("last_path を復元できません: %s。カレントディレクトリで開始します", last_path);
    browser_open_dir(&app->browser, ".", app->cfg->show_all_files);
}

/* Issue #47: start_mode=resumeでの起動。cfg->resume_pathを開き、
 * resume_source/resume_trackのトラックへジャンプしてresume_position_msへ
 * シークする。0を返す(何も変えない)のは resume_path が空のとき、または
 * playlist_open()自体が失敗したとき(app_open_path()が画面をPlayerへ
 * 進めなかったことで判定する)。呼び出し側(app_run())はこのときfolderへの
 * フォールバック列を続ける。
 * トラックが見つからない(resume_source/trackが古い等)場合はエントリ0の
 * まま(app_open_path()が既に再生している)にする。 */
static int app_try_resume(app_t *app) {
    if (!app->cfg->resume_path[0]) return 0;

    /* Browserをファイルの場所へ合わせておく(Bで戻ったときの一貫性。
     * restore_last_path()自体はディレクトリ/ファイルの両対応で、
     * 失敗してもここでは無視してよい: 次のapp_open_path()の成否だけを見る)。 */
    restore_last_path(app, app->cfg->resume_path);

    app_open_path(app, app->cfg->resume_path);
    if (app->screen != SCREEN_PLAYER || !app->pl) return 0; /* 失敗 */

    int idx = playlist_find_entry(app->pl, app->cfg->resume_source, app->cfg->resume_track);
    if (idx >= 0 && idx != app->player.current_entry) {
        player_play_entry(&app->player, idx);
    }

    int dur = player_current_duration_ms(&app->player);
    int pos = app->cfg->resume_position_ms;
    if (pos < 0) pos = 0;
    if (dur > 0 && pos > dur) pos = dur;
    if (pos > 0) player_seek(&app->player, pos);

    return 1;
}

/* ---- メインループ ------------------------------------------------------- */

int app_run(mugbs_config_t *cfg, const app_options_t *opt) {
    app_t app;
    memset(&app, 0, sizeof(app));
    app.cfg = cfg; /* コピーしない。app_t/player_t は常にこの1つを参照する (P6) */
    app.config_path = opt->config_path;
    app.running = 1;
    app.screen = SCREEN_BROWSER;
    app.battery_low_pct = opt->battery_low_pct;
    battery_init(&app.battery);

    int fullscreen = (opt->window_w <= 0 || opt->window_h <= 0);
    if (ui_init(&app.ui, opt->window_w, opt->window_h, fullscreen) != 0) {
        return 1;
    }
    input_init(&app.input, app.cfg);

    if (player_init(&app.player, app.cfg) != 0) {
        input_shutdown(&app.input);
        ui_shutdown(&app.ui);
        return 1;
    }

    /* 起動時の開始位置。優先順(Issue #47でstart_mode/start_folderを追加):
     *   --start-dir > (start_mode=resumeならResume) > start_folder >
     *   last_path(F-13) > MUCHIP_START_DIR > カレントディレクトリ。
     * --start-dirが明示されたときは、以後それが記憶される対象になる
     * (次回起動時にlast_pathとして使われる)。resumeとstart_folderは
     * --start-dirより優先度が低い: --start-dirはホストでの複数解像度
     * レイアウト確認用の明示的な上書きなので、config.iniの起動モードより
     * 常に勝つ。
     * MUCHIP_START_DIR(P7) は last_path より後に置くのが肝で、実機の
     * mux_launch.sh は毎回これを渡してくるため、--start-dir と同じ優先度に
     * すると last_path が毎回上書きされ F-13 が永久に発火しなくなる。
     * start_folder はlast_pathより前: ユーザーが明示的に選んだ固定の
     * 開始位置なので、F-13の「前回どこを見ていたか」の自動追随より
     * 優先する。resumeが失敗した場合(ファイルが削除された等)はここへ
     * フォールバックする。 */
    if (opt->start_dir && opt->start_dir[0]) {
        if (browser_open_dir(&app.browser, opt->start_dir, app.cfg->show_all_files) != 0) {
            LOG_WARN("開始ディレクトリを開けません: %s。カレントディレクトリで再試行します",
                      opt->start_dir);
            browser_open_dir(&app.browser, ".", app.cfg->show_all_files);
        }
    } else if (app.cfg->start_mode == START_MODE_RESUME && app_try_resume(&app)) {
        /* 成功。app_try_resume()がBrowser・Player両方の状態を作った。 */
    } else if (app.cfg->start_folder[0] &&
               browser_open_dir(&app.browser, app.cfg->start_folder, app.cfg->show_all_files) == 0) {
        LOG_INFO("start_folder から開始します: %s", app.cfg->start_folder);
    } else if (app.cfg->last_path[0]) {
        restore_last_path(&app, app.cfg->last_path);
    } else if (opt->fallback_start_dir && opt->fallback_start_dir[0] &&
               browser_open_dir(&app.browser, opt->fallback_start_dir,
                                 app.cfg->show_all_files) == 0) {
        LOG_INFO("MUCHIP_START_DIR から開始します: %s", opt->fallback_start_dir);
    } else {
        browser_open_dir(&app.browser, ".", app.cfg->show_all_files);
    }
    if (app.browser.cwd) set_last_path(&app, app.browser.cwd);

    if (opt->initial_path) {
        app_open_path(&app, opt->initial_path);
    }

    int use_script = 0;
    ui_script_t script;
    if (opt->ui_script_path) {
        use_script = (ui_script_load(&script, opt->ui_script_path) == 0);
        if (!use_script) {
            player_shutdown(&app.player);
            browser_free(&app.browser);
            playctx_free(&app.playctx);
            browser_free(&app.folder_pick);
            input_shutdown(&app.input);
            ui_shutdown(&app.ui);
            return 1;
        }
    }

    while (app.running) {
        if (use_script) {
            input_action_t a;
            if (!ui_script_next(&script, &a)) {
                app.running = 0;
            } else {
                /* --ui-scriptはリピートを合成しないため常に0(Issue #47)。 */
                app_dispatch(&app, a, 0);
            }
        } else {
            input_action_t a;
            while (input_poll(&app.input, &a)) {
                app_dispatch(&app, a, input_last_was_repeat(&app.input));
                if (!app.running) break;
            }
        }

        /* --window で起動した場合、ウィンドウをドラッグして伸縮できる
         * (ui.cのSDL_WINDOW_RESIZABLE)。--ui-scriptはinput_pollを経由しない
         * ためこのフラグは立たないが、ヘッドレステストはウィンドウ操作を
         * 行わないので実害は無い。 */
        if (input_take_window_resized(&app.input)) {
            ui_handle_resize(&app.ui);
        }

        if (app.pl && player_is_track_ended(&app.player)) {
            LOG_INFO("トラック終端検出 -> 次トラックへ");
            player_next_track(&app.player);
        }

        app_update_scope(&app); /* F-14: 1フレーム1回だけ取り込む */
        app_update_battery(&app); /* Issue #7: 同上。実際のsysfs読みは内部で2秒に1回 */
        app_apply_theme(&app); /* Issue #27: 同上 */

        switch (app.screen) {
            case SCREEN_BROWSER:     draw_browser(&app); break;
            case SCREEN_PLAYER:      draw_player(&app); break;
            case SCREEN_TRACKLIST:   draw_tracklist(&app); break;
            case SCREEN_SETTINGS:    draw_settings(&app); break;
            case SCREEN_THEME_EDIT:  draw_theme_edit(&app); break;
            case SCREEN_FOLDER_PICK: draw_folder_pick(&app); break;
        }
        /* --screenshot(開発用): 毎フレーム上書きすることで、ループを抜けた
         * 時点のファイルが「最後に描かれた画面」になる。ui_present() の後だと
         * バックバッファの内容が未定義になるため、必ずその前に読み戻す。
         * 対話起動では使わない想定なので、毎フレームのコストは問わない。 */
        if (opt->screenshot_path) {
            ui_save_screenshot(&app.ui, opt->screenshot_path);
        }
        ui_present(&app.ui);

        if (!use_script) SDL_Delay(16);
    }

    /* 終了時の自動保存。Settings画面を一度も開かずに終了した場合でも
     * last_path(F-13)・resume_*(Issue #47)は最新化されているため、
     * config_pathがあればここでも保存する(app_save_config()がここでも
     * app_capture_resume()を呼ぶ。Settings退出時の保存(app_leave_settings)
     * と合わせて二重に保存されることがあるが、config_save()は冪等なので
     * 無害)。 */
    app_save_config(&app);

    if (use_script) ui_script_free(&script);
    if (app.pl) playlist_free(app.pl);
    browser_free(&app.browser);
    playctx_free(&app.playctx);
    browser_free(&app.folder_pick);
    player_shutdown(&app.player);
    input_shutdown(&app.input);
    ui_shutdown(&app.ui);
    return 0;
}

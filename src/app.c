#include "app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "browser.h"
#include "input.h"
#include "log.h"
#include "player.h"
#include "playlist.h"
#include "ui.h"

typedef enum {
    SCREEN_BROWSER,
    SCREEN_PLAYER,
    SCREEN_TRACKLIST,
} app_screen_t;

typedef struct {
    ui_t ui;
    input_t input;
    player_t player;
    playlist_t *pl; /* 現在のプレイリスト。NULL=何も開いていない */
    browser_t browser;
    app_screen_t screen;

    mugbs_config_t *cfg; /* 参照のみ。所有権は呼び出し側(main())。
                            プログラム全体で権威あるインスタンスはこれ1つだけ
                            (player_t は const mugbs_config_t* で同じものを見る)。
                            show_all_files はここに統合済み(cfg->show_all_files)。 */

    int tracklist_sel;
    int tracklist_scroll;

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

static void set_status(app_t *app, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->status_until = SDL_GetTicks() + 4000;
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

    app->tracklist_sel = 0;
    app->tracklist_scroll = 0;
    app->screen = SCREEN_PLAYER;
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
        case INPUT_UP:    browser_move(&app->browser, -1); break;
        case INPUT_DOWN:  browser_move(&app->browser, 1); break;
        case INPUT_LEFT:  browser_page(&app->browser, -list_visible_rows(app)); break;
        case INPUT_RIGHT: browser_page(&app->browser, list_visible_rows(app)); break;
        case INPUT_A: {
            if (!browser_enter(&app->browser, app->cfg->show_all_files)) {
                char path[4096];
                if (browser_selected_path(&app->browser, path, sizeof(path)) == 0) {
                    app_open_path(app, path);
                }
            }
            break;
        }
        case INPUT_B:
            browser_up(&app->browser, app->cfg->show_all_files);
            break;
        default:
            break;
    }
}

static void handle_player_input(app_t *app, input_action_t a) {
    switch (a) {
        case INPUT_UP:
            app->cfg->volume += 5;
            if (app->cfg->volume > 100) app->cfg->volume = 100;
            player_apply_config(&app->player);
            break;
        case INPUT_DOWN:
            app->cfg->volume -= 5;
            if (app->cfg->volume < 0) app->cfg->volume = 0;
            player_apply_config(&app->player);
            break;
        case INPUT_LEFT: {
            int pos = player_tell_ms(&app->player) - 5000;
            if (pos < 0) pos = 0;
            player_seek(&app->player, pos);
            break;
        }
        case INPUT_RIGHT: {
            int dur = player_current_duration_ms(&app->player);
            int pos = player_tell_ms(&app->player) + 5000;
            if (dur > 0 && pos > dur) pos = dur;
            player_seek(&app->player, pos);
            break;
        }
        case INPUT_A:
            player_toggle_pause(&app->player);
            break;
        case INPUT_B:
            app->screen = SCREEN_BROWSER;
            break;
        case INPUT_X:
            app->tracklist_sel = app->player.current_entry;
            app->screen = SCREEN_TRACKLIST;
            break;
        case INPUT_Y: {
            repeat_mode_t next = app->cfg->repeat_mode == REPEAT_NONE ? REPEAT_ONE :
                                  app->cfg->repeat_mode == REPEAT_ONE ? REPEAT_ALL : REPEAT_NONE;
            /* config はポインタで player と共有しているため、ここへの代入だけで
             * 次の player_next_track()/player_prev_track() から反映される。 */
            app->cfg->repeat_mode = next;
            break;
        }
        case INPUT_L1:
            player_prev_track(&app->player);
            break;
        case INPUT_R1:
            player_next_track(&app->player);
            break;
        case INPUT_L2:
            app_prev_source(app);
            break;
        case INPUT_R2:
            app_next_source(app);
            break;
        default:
            break;
    }
}

static void handle_tracklist_input(app_t *app, input_action_t a) {
    int n = app->pl ? app->pl->entry_count : 0;
    switch (a) {
        case INPUT_UP:
            if (app->tracklist_sel > 0) app->tracklist_sel--;
            break;
        case INPUT_DOWN:
            if (app->tracklist_sel < n - 1) app->tracklist_sel++;
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

static void app_dispatch(app_t *app, input_action_t a) {
    if (a == INPUT_NONE) return;
    if (a == INPUT_QUIT) {
        app->running = 0;
        return;
    }
    switch (app->screen) {
        case SCREEN_BROWSER:    handle_browser_input(app, a); break;
        case SCREEN_PLAYER:     handle_player_input(app, a); break;
        case SCREEN_TRACKLIST:  handle_tracklist_input(app, a); break;
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

static const char *tracklist_item_text(void *ctx, int index) {
    app_t *app = (app_t *)ctx;
    static char buf[300];
    const playlist_entry_t *e = &app->pl->entries[index];
    snprintf(buf, sizeof(buf), "%3d. %s", index + 1, e->title);
    return buf;
}

static void draw_browser(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = { 18, 18, 26, 255 };
    const SDL_Color bar_bg = { 30, 30, 42, 255 };
    const SDL_Color fg = { 230, 230, 230, 255 };
    const SDL_Color dim = { 150, 150, 160, 255 };
    const SDL_Color err = { 255, 120, 90, 255 };

    ui_clear(ui, bg);

    ui_rect_t header = { 0, 0, ui->screen_w, ui->metrics.header_h };
    ui_fill_rect(ui, header, bar_bg);
    int hy = (header.h - ui_glyph_size(ui, UI_TEXT_BODY)) / 2;
    ui_text_clipped(ui, ui->metrics.pad, hy, ui->screen_w - ui->metrics.pad * 2,
                     UI_TEXT_BODY, fg, app->browser.cwd ? app->browser.cwd : "");

    ui_rect_t list = list_rect(app);
    ui_draw_list(ui, list, app->browser.count, app->browser.selected, -1,
                 &app->browser.scroll, browser_item_text, app);

    ui_rect_t footer = { 0, ui->screen_h - ui->metrics.footer_h, ui->screen_w, ui->metrics.footer_h };
    ui_fill_rect(ui, footer, bar_bg);
    int fy = footer.y + (footer.h - ui_glyph_size(ui, UI_TEXT_SMALL)) / 2;
    if (app->status[0] && SDL_GetTicks() < app->status_until) {
        ui_text_clipped(ui, ui->metrics.pad, fy, ui->screen_w - ui->metrics.pad * 2,
                         UI_TEXT_SMALL, err, app->status);
    } else {
        ui_text(ui, ui->metrics.pad, fy, UI_TEXT_SMALL, dim, "A:Open  B:Up  Esc:Quit");
    }
}

static void draw_player(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = { 18, 18, 26, 255 };
    const SDL_Color fg = { 230, 230, 230, 255 };
    const SDL_Color dim = { 150, 150, 160, 255 };
    const SDL_Color accent = { 120, 180, 255, 255 };
    const SDL_Color err = { 255, 120, 90, 255 };

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

    ui_text_clipped(ui, x, y, content_w, UI_TEXT_TITLE, fg, e ? e->title : "(no track)");
    y += ui_glyph_size(ui, UI_TEXT_TITLE) + ui->metrics.pad;

    ui_text_clipped(ui, x, y, content_w, UI_TEXT_BODY, dim,
                     (app->pl && app->pl->game && app->pl->game[0]) ? app->pl->game : "");
    y += ui->metrics.line_h;

    char meta[256];
    snprintf(meta, sizeof(meta), "%s  %s",
             (src && src->author[0]) ? src->author : "",
             (src && src->copyright[0]) ? src->copyright : "");
    ui_text_clipped(ui, x, y, content_w, UI_TEXT_SMALL, dim, meta);
    y += ui->metrics.line_h + ui->metrics.pad;

    char trackno[32];
    snprintf(trackno, sizeof(trackno), "Track %d/%d",
             app->pl ? app->player.current_entry + 1 : 0, app->pl ? app->pl->entry_count : 0);
    ui_text(ui, x, y, UI_TEXT_SMALL, dim, trackno);
    y += ui->metrics.line_h + ui->metrics.pad;

    int pos_ms = player_tell_ms(&app->player);
    int dur_ms = player_current_duration_ms(&app->player);
    char timebuf[64];
    snprintf(timebuf, sizeof(timebuf), "%d:%02d / %d:%02d",
             pos_ms / 60000, (pos_ms / 1000) % 60, dur_ms / 60000, (dur_ms / 1000) % 60);
    ui_text(ui, x, y, UI_TEXT_SMALL, fg, timebuf);
    y += ui->metrics.line_h;

    ui_rect_t bar = { x, y, content_w, ui->metrics.pad * 2 };
    float ratio = dur_ms > 0 ? (float)pos_ms / (float)dur_ms : 0.0f;
    const SDL_Color bar_bg = { 50, 50, 60, 255 };
    ui_draw_progress(ui, bar, ratio, accent, bar_bg);
    y += bar.h + ui->metrics.pad * 2;

    const char *repeat_label = app->cfg->repeat_mode == REPEAT_ONE ? "one" :
                                app->cfg->repeat_mode == REPEAT_ALL ? "all" : "none";
    const char *state_label = app->player.state == PLAYER_PAUSED ? "PAUSED" :
                               app->player.state == PLAYER_PLAYING ? "PLAYING" : "STOPPED";
    char status_line[128];
    snprintf(status_line, sizeof(status_line), "%s  repeat:%s  vol:%d",
             state_label, repeat_label, app->cfg->volume);
    ui_text(ui, x, y, UI_TEXT_SMALL, dim, status_line);
    y += ui->metrics.line_h;

    if (app->status[0] && SDL_GetTicks() < app->status_until) {
        ui_text_clipped(ui, x, y, content_w, UI_TEXT_SMALL, err, app->status);
    }

    int footer_y = ui->screen_h - ui->metrics.footer_h + (ui->metrics.footer_h - ui->metrics.glyph) / 2;
    ui_text_clipped(ui, x, footer_y, content_w, UI_TEXT_SMALL, dim,
                     src ? src->display_path : "");
}

static void draw_tracklist(app_t *app) {
    ui_t *ui = &app->ui;
    const SDL_Color bg = { 18, 18, 26, 255 };
    const SDL_Color bar_bg = { 30, 30, 42, 255 };
    const SDL_Color fg = { 230, 230, 230, 255 };
    const SDL_Color dim = { 150, 150, 160, 255 };

    ui_clear(ui, bg);

    ui_rect_t header = { 0, 0, ui->screen_w, ui->metrics.header_h };
    ui_fill_rect(ui, header, bar_bg);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "Tracks (%d)", app->pl ? app->pl->entry_count : 0);
    ui_text(ui, ui->metrics.pad, (header.h - ui_glyph_size(ui, UI_TEXT_BODY)) / 2,
            UI_TEXT_BODY, fg, hdr);

    if (app->pl) {
        ui_rect_t list = list_rect(app);
        ui_draw_list(ui, list, app->pl->entry_count, app->tracklist_sel,
                     app->player.current_entry, &app->tracklist_scroll,
                     tracklist_item_text, app);
    }

    ui_rect_t footer = { 0, ui->screen_h - ui->metrics.footer_h, ui->screen_w, ui->metrics.footer_h };
    ui_fill_rect(ui, footer, bar_bg);
    ui_text(ui, ui->metrics.pad, footer.y + (footer.h - ui_glyph_size(ui, UI_TEXT_SMALL)) / 2,
            UI_TEXT_SMALL, dim, "A:Play  B/X:Back");
}

/* ---- メインループ ------------------------------------------------------- */

int app_run(mugbs_config_t *cfg, const app_options_t *opt) {
    app_t app;
    memset(&app, 0, sizeof(app));
    app.cfg = cfg; /* コピーしない。app_t/player_t は常にこの1つを参照する (P6) */
    app.running = 1;
    app.screen = SCREEN_BROWSER;

    int fullscreen = (opt->window_w <= 0 || opt->window_h <= 0);
    if (ui_init(&app.ui, opt->window_w, opt->window_h, fullscreen) != 0) {
        return 1;
    }
    input_init(&app.input);

    if (player_init(&app.player, app.cfg) != 0) {
        input_shutdown(&app.input);
        ui_shutdown(&app.ui);
        return 1;
    }

    /* last_path(F-13) を使った開始位置の復元は C4 (Settings画面) で追加する。 */
    const char *dir = (opt->start_dir && opt->start_dir[0]) ? opt->start_dir : ".";
    if (browser_open_dir(&app.browser, dir, app.cfg->show_all_files) != 0) {
        LOG_WARN("開始ディレクトリを開けません: %s。カレントディレクトリで再試行します", dir);
        browser_open_dir(&app.browser, ".", app.cfg->show_all_files);
    }

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
                app_dispatch(&app, a);
            }
        } else {
            input_action_t a;
            while (input_poll(&app.input, &a)) {
                app_dispatch(&app, a);
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

        switch (app.screen) {
            case SCREEN_BROWSER:   draw_browser(&app); break;
            case SCREEN_PLAYER:    draw_player(&app); break;
            case SCREEN_TRACKLIST: draw_tracklist(&app); break;
        }
        ui_present(&app.ui);

        if (!use_script) SDL_Delay(16);
    }

    if (use_script) ui_script_free(&script);
    if (app.pl) playlist_free(app.pl);
    browser_free(&app.browser);
    player_shutdown(&app.player);
    input_shutdown(&app.input);
    ui_shutdown(&app.ui);
    return 0;
}

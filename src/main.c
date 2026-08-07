/* main.c - エントリポイント。
 *
 * 引数を解釈し、--list / --cli (CLIハーネス、CI・自動検証用) と、
 * それ以外(GUI本体。app.c の Browser/Player/TrackList)に分岐する。
 * P0 時点にあった run_blank_window() は app_run() に置き換わったため削除した。
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "battery.h"
#include "config.h"
#include "log.h"
#include "player.h"
#include "playlist.h"

/* CMakeLists.txt の project(mugbs VERSION ...) から
 * target_compile_definitions() で渡される。tests/ から main.c を単体ビルドする
 * ことは無いが、保険として既定値を持たせておく。 */
#ifndef MUGBS_VERSION
#define MUGBS_VERSION "0.0.0-dev"
#endif

typedef struct {
    const char *path;      /* 開く対象 (.gbs/.m3u/.zip)。GUIモードでは省略可(Browserから開始) */
    int list_only;         /* --list: プレイリストを列挙して終了 */
    int cli_mode;          /* --cli: SDL ウィンドウを開かずコンソールのみで動作 */
    int track;             /* --track N: 開始トラック (0始まり内部表現へは後段で変換) */

    /* 以下は config.ini ([playback]) の値をCLIから上書きするテスト用フラグ。
     * 既定値 -> config.ini -> CLI引数 の順に上書きし、CLI指定が最優先。
     * -1 は「上書きしない(config.ini/既定値をそのまま使う)」を表す。 */
    int default_length_sec; /* --duration SEC */
    int fade_length_ms;      /* --fade-ms MS */
    int repeat_mode;         /* --repeat none|one|all (repeat_mode_t にキャスト) */

    const char *config_path; /* --config PATH: 省略時は config_resolve_path() が決める */

    /* P5: GUI(app.c)向けのオプション。 */
    const char *start_dir;   /* --start-dir DIR: Browserの開始ディレクトリ */
    int window_w, window_h;   /* --window WxH: ホストでのレイアウト確認用。0以下=実機と同じく検出解像度でフルスクリーン */
    const char *ui_script;     /* --ui-script FILE: 非公開。ヘッドレスUIスモークテスト用 */
    const char *screenshot;     /* --screenshot FILE: 非公開。終了直前の1フレームをBMPで書き出す */
} mugbs_args_t;

static void print_usage(const char *prog) {
    fprintf(stderr,
        "使い方: %s [オプション] [ファイル]\n"
        "  --list             プレイリストを列挙して終了する\n"
        "  --track N          トラック番号 N (1始まり) から開始する\n"
        "  --duration SEC     曲長不明時の既定再生秒数を上書きする (config.default_length_sec)\n"
        "  --fade-ms MS        フェード長を上書きする (config.fade_length_ms)\n"
        "  --repeat MODE      none|one|all でリピートモードを上書きする\n"
        "  --config PATH      設定ファイルの場所を指定する\n"
        "                     (既定: 環境変数 MUGBS_CONFIG、無ければ ./config.ini)\n"
        "  --cli              SDL ウィンドウを開かずコンソールのみで動作する\n"
        "  --start-dir DIR    GUI起動時、Browserの開始ディレクトリを指定する\n"
        "                     (環境変数 MUGBS_START_DIR でも指定できるが、そちらは\n"
        "                      config.ini の last_path が無いときだけ使われる)\n"
        "  --window WxH       GUIをこのウィンドウサイズで起動する(未指定なら検出した解像度でフルスクリーン)\n"
        "  -h, --help         このヘルプを表示する\n"
        "  --version          バージョンを表示して終了する\n",
        prog);
}

static int parse_args(int argc, char **argv, mugbs_args_t *out) {
    memset(out, 0, sizeof(*out));
    out->track = 1;
    out->default_length_sec = -1;
    out->fade_length_ms = -1;
    out->repeat_mode = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--list") == 0) {
            out->list_only = 1;
        } else if (strcmp(a, "--cli") == 0) {
            out->cli_mode = 1;
        } else if (strcmp(a, "--start-dir") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--start-dir にはディレクトリ引数が必要です"); return -1; }
            out->start_dir = argv[++i];
        } else if (strcmp(a, "--window") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--window には WxH 形式の引数が必要です"); return -1; }
            const char *wh = argv[++i];
            if (sscanf(wh, "%dx%d", &out->window_w, &out->window_h) != 2 ||
                out->window_w <= 0 || out->window_h <= 0) {
                LOG_ERR("--window の値が不正です: %s (例: 720x720)", wh);
                return -1;
            }
        } else if (strcmp(a, "--ui-script") == 0) {
            /* 非公開オプション: ヘッドレスUIスモークテスト用(tests/参照)。usageには出さない。 */
            if (i + 1 >= argc) { LOG_ERR("--ui-script にはファイル引数が必要です"); return -1; }
            out->ui_script = argv[++i];
        } else if (strcmp(a, "--screenshot") == 0) {
            /* 非公開オプション: 終了直前の1フレームをBMPへ書き出す。usageには出さない。
             * ホストには実機の /dev/fb0 に当たるものが無いため、--ui-script で
             * 目的の画面まで遷移させてからレイアウトを目視確認するための開発用。
             * SDL_VIDEODRIVER=offscreen でも動く。 */
            if (i + 1 >= argc) { LOG_ERR("--screenshot にはファイル引数が必要です"); return -1; }
            out->screenshot = argv[++i];
        } else if (strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--config にはファイル引数が必要です"); return -1; }
            out->config_path = argv[++i];
        } else if (strcmp(a, "--track") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--track には数値引数が必要です"); return -1; }
            out->track = atoi(argv[++i]);
        } else if (strcmp(a, "--duration") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--duration には数値引数が必要です"); return -1; }
            out->default_length_sec = atoi(argv[++i]);
        } else if (strcmp(a, "--fade-ms") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--fade-ms には数値引数が必要です"); return -1; }
            out->fade_length_ms = atoi(argv[++i]);
        } else if (strcmp(a, "--repeat") == 0) {
            if (i + 1 >= argc) { LOG_ERR("--repeat には none|one|all が必要です"); return -1; }
            const char *m = argv[++i];
            if (strcmp(m, "none") == 0) out->repeat_mode = REPEAT_NONE;
            else if (strcmp(m, "one") == 0) out->repeat_mode = REPEAT_ONE;
            else if (strcmp(m, "all") == 0) out->repeat_mode = REPEAT_ALL;
            else { LOG_ERR("--repeat の値が不正です: %s (none|one|all)", m); return -1; }
        } else if (strcmp(a, "--version") == 0) {
            printf("muGBS %s\n", MUGBS_VERSION);
            exit(0);
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (a[0] == '-') {
            LOG_ERR("不明なオプション: %s", a);
            return -1;
        } else {
            out->path = a;
        }
    }
    return 0;
}

/* 既定値 -> config.ini -> CLI引数 の順に上書きして設定を組み立てる。
 * CLI指定が最優先(-1 は「指定なし」)。使用した設定ファイルのパスを
 * path_out へ返す(GUI側が終了時の保存先として使う)。 */
static void build_config(const mugbs_args_t *args, mugbs_config_t *cfg,
                          char *path_out, size_t path_size) {
    config_set_defaults(cfg);

    config_resolve_path(path_out, path_size, args->config_path);
    config_load(cfg, path_out);

    if (args->default_length_sec >= 0) cfg->default_length_sec = args->default_length_sec;
    if (args->fade_length_ms >= 0) cfg->fade_length_ms = args->fade_length_ms;
    if (args->repeat_mode >= 0) cfg->repeat_mode = (repeat_mode_t)args->repeat_mode;
}

/* playlist_open() が構築したプレイリストを人間可読な形で列挙する (--list)。
 * 音は一切出さない。 */
static void print_playlist(const playlist_t *pl, const char *path) {
    printf("%s: 全%dトラック%s%s\n", path, pl->entry_count,
           pl->game && pl->game[0] ? " - " : "", pl->game && pl->game[0] ? pl->game : "");
    for (int i = 0; i < pl->entry_count; i++) {
        const playlist_entry_t *e = &pl->entries[i];
        const playlist_source_t *src = &pl->sources[e->source_index];
        printf("  %3d. %s  [%s]\n", i + 1, e->title, src->display_path);
    }
}

/* ファイルを開いて再生するCLIハーネス(--list/--cli)。CI・自動検証や
 * SDLウィンドウ無しでの動作確認用に、GUI(app.c)とは独立に維持している。 */
static int run_player_cli(const mugbs_args_t *args) {
    mugbs_config_t cfg;
    char config_path[MUGBS_PATH_MAX];
    /* CLIハーネスは config.ini を読むだけで、保存は一切しない
     * (--list/--cli を回すたびに設定が書き換わると自動検証の再現性が落ちる)。 */
    build_config(args, &cfg, config_path, sizeof(config_path));

    playlist_t *pl = NULL;
    if (playlist_open(args->path, &cfg, &pl) != 0) {
        return 1;
    }

    if (args->list_only) {
        print_playlist(pl, args->path);
        playlist_free(pl);
        return 0;
    }

    player_t player;
    if (player_init(&player, &cfg) != 0) {
        playlist_free(pl);
        return 1;
    }
    player_load_playlist(&player, pl);

    int entry_index = args->track - 1; /* CLI引数は1始まり、内部は0始まり */
    if (player_play_entry(&player, entry_index) != 0) {
        player_shutdown(&player);
        playlist_free(pl);
        return 1;
    }

    /* トラック終端を検出したら player_next_track() で自動的に送る (F-07/F-08/F-11)。
     * REPEAT_NONE で最終トラックまで再生し終えると player_next_track() が
     * STOPPED 相当(-1)を返すので、そこでループを抜ける。
     * REPEAT_ALL/ONE(既定はALL)は自然には終わらないため、CLIでの動作確認は
     * `--repeat none` と組み合わせて全トラックを一巡させて止める運用を想定する。 */
    int running = 1;
    while (running) {
        if (player_is_track_ended(&player)) {
            LOG_INFO("トラック終端検出 -> 次トラックへ");
            if (player_next_track(&player) != 0) {
                LOG_INFO("再生する曲がなくなりました。停止します");
                running = 0;
            }
        }
        SDL_Delay(50);
    }

    player_shutdown(&player);
    playlist_free(pl);
    return 0;
}

int main(int argc, char **argv) {
    srand((unsigned)time(NULL)); /* F-25: シャッフル再生のFisher-Yatesが使う */

    mugbs_args_t args;
    if (parse_args(argc, argv, &args) != 0) {
        print_usage(argv[0]);
        return 2;
    }

    /* 実機では mux_launch.sh が出力を log.txt へリダイレクトするため、
     * この1行がユーザーから送られてきたログのビルド特定に使える。 */
    LOG_INFO("muGBS %s", MUGBS_VERSION);

    if (args.list_only || args.cli_mode) {
        /* CLIハーネス(CI・自動検証用)。P1〜P4から変わらず、必ずファイル指定が要る。 */
        if (!args.path) {
            LOG_ERR("--list/--cli にはファイルを指定してください");
            print_usage(argv[0]);
            return 2;
        }
        if (SDL_Init(SDL_INIT_AUDIO) != 0) {
            LOG_ERR("SDL_Init(AUDIO) failed: %s", SDL_GetError());
            return 1;
        }
        int rc = run_player_cli(&args);
        SDL_Quit();
        return rc;
    }

    /* GUI本体 (P5): Browser/Player/TrackList。ファイル指定が無ければ
     * Browserから始める(app_run()内部でplaylist_openの成否を処理する)。 */
    mugbs_config_t cfg;
    char config_path[MUGBS_PATH_MAX];
    build_config(&args, &cfg, config_path, sizeof(config_path));

    /* --duration/--fade-ms/--repeat のいずれかが指定されている場合、
     * 一回きりのテスト用オーバーライドを config.ini へ永続化しては
     * ならないため、終了時のオートセーブ(C4で実装)を無効化する。 */
    int cli_override = (args.default_length_sec >= 0 || args.fade_length_ms >= 0 ||
                         args.repeat_mode >= 0);
    if (cli_override) {
        LOG_INFO("CLI引数で設定を上書きしているため、終了時の自動保存を無効化します");
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        LOG_ERR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    /* 実機の mux_launch.sh は音楽フォルダを自動検出して MUGBS_START_DIR で渡してくる
     * (P7)。--start-dir と違い last_path より優先度が低いので、F-13(前回の続きから
     * 開く)を毎回潰さずに「初回起動時だけ音楽フォルダから始める」を実現できる。 */
    app_options_t opt = {
        .initial_path = args.path,
        .start_dir = args.start_dir,
        .fallback_start_dir = getenv("MUGBS_START_DIR"),
        .window_w = args.window_w,
        .window_h = args.window_h,
        .ui_script_path = args.ui_script,
        .screenshot_path = args.screenshot,
        .config_path = cli_override ? NULL : config_path,
        /* Issue #7: muOS実機は mux_launch.sh が GET_VAR でしきい値を探し、
         * 見つかれば MUGBS_BATTERY_LOW_PCT を export する。無ければ
         * battery_low_threshold_from_env(NULL) が既定の10%を返す。 */
        .battery_low_pct = battery_low_threshold_from_env(getenv("MUGBS_BATTERY_LOW_PCT")),
    };
    int rc = app_run(&cfg, &opt);
    SDL_Quit();
    return rc;
}

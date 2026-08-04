/* main.c - エントリポイント。
 *
 * P0 時点では引数パースと SDL の起動確認のみを行う。
 * 実際の再生ロジック（audio.c / player.c 経由）は P1 以降で追加する。
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "player.h"

typedef struct {
    const char *path;      /* 開く対象 (.gbs/.m3u/.zip) */
    int list_only;         /* --list: プレイリストを列挙して終了 */
    int cli_mode;          /* --cli: SDL ウィンドウを開かずコンソールのみで動作 */
    int track;             /* --track N: 開始トラック (0始まり内部表現へは後段で変換) */

    /* 以下は config.ini ([playback]) の値をCLIから上書きするテスト用フラグ。
     * P6 でINI読み込みが入った後も、CLI指定があればそちらを優先する想定。
     * -1 は「上書きしない(config_set_defaultsの値をそのまま使う)」を表す。 */
    int default_length_sec; /* --duration SEC */
    int fade_length_ms;      /* --fade-ms MS */
    int repeat_mode;         /* --repeat none|one|all (repeat_mode_t にキャスト) */
} mugbs_args_t;

static void print_usage(const char *prog) {
    fprintf(stderr,
        "使い方: %s [オプション] [ファイル]\n"
        "  --list             プレイリストを列挙して終了する\n"
        "  --track N          トラック番号 N (1始まり) から開始する\n"
        "  --duration SEC     曲長不明時の既定再生秒数を上書きする (config.default_length_sec)\n"
        "  --fade-ms MS        フェード長を上書きする (config.fade_length_ms)\n"
        "  --repeat MODE      none|one|all でリピートモードを上書きする\n"
        "  --cli              SDL ウィンドウを開かずコンソールのみで動作する\n"
        "  -h, --help         このヘルプを表示する\n",
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

/* 空ウィンドウを表示し、閉じるイベントか Esc/Q で終了する。
 * P0 の完了条件（空ウィンドウが出る）を満たすための最小実装。
 * P5 で ui.c による本格描画に置き換える。 */
static int run_blank_window(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LOG_ERR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
        LOG_WARN("SDL_GetCurrentDisplayMode failed: %s (640x480にフォールバック)", SDL_GetError());
        mode.w = 640;
        mode.h = 480;
    }
    LOG_INFO("検出した解像度: %dx%d", mode.w, mode.h);

    SDL_Window *win = SDL_CreateWindow(
        "muGBS",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        mode.w, mode.h,
        SDL_WINDOW_SHOWN);
    if (!win) {
        LOG_ERR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q)) {
                running = 0;
            }
        }
        if (ren) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderPresent(ren);
        }
        SDL_Delay(16);
    }

    if (ren) SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* ファイルを開いて再生するCLIハーネス。
 * P1時点では単体 .gbs 等のみを直接 player に渡す（m3u/zip対応はP3/P4）。
 * GUI(P5)がまだ無いため、--cli の有無に関わらずこのループで再生する。 */
static int run_player_cli(const mugbs_args_t *args) {
    if (args->list_only) {
        LOG_ERR("--list はまだ実装されていません（P3のplaylist.cで対応予定）");
        return 1;
    }

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    if (args->default_length_sec >= 0) cfg.default_length_sec = args->default_length_sec;
    if (args->fade_length_ms >= 0) cfg.fade_length_ms = args->fade_length_ms;
    if (args->repeat_mode >= 0) cfg.repeat_mode = (repeat_mode_t)args->repeat_mode;

    player_t player;
    if (player_init(&player, &cfg) != 0) {
        return 1;
    }

    int track_index = args->track - 1; /* CLI引数は1始まり、gmeは0始まり */
    if (player_open_and_play(&player, args->path, track_index) != 0) {
        player_shutdown(&player);
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
    return 0;
}

int main(int argc, char **argv) {
    mugbs_args_t args;
    if (parse_args(argc, argv, &args) != 0) {
        print_usage(argv[0]);
        return 2;
    }

    if (!args.path) {
        /* ファイル指定なし: P0 の疎通確認用に空ウィンドウを出す。
         * --cli 指定時はウィンドウなしで即終了する。 */
        if (args.cli_mode) {
            LOG_INFO("muGBS --cli: ファイル未指定のため何もせず終了します");
            return 0;
        }
        return run_blank_window();
    }

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        LOG_ERR("SDL_Init(AUDIO) failed: %s", SDL_GetError());
        return 1;
    }
    int rc = run_player_cli(&args);
    SDL_Quit();
    return rc;
}

/* test_playctx.c - playctx.c の単体テスト。 (Issue #47)
 *
 * SDLに依存しない純粋なディレクトリ走査ロジックなので、一時ディレクトリを
 * 実際に作ってCTestから素の実行ファイルとして検証する
 * (tests/test_browser.c と同じ流儀)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "playctx.h"
#include "test_util.h"

static void make_dir(const char *path) {
    if (mkdir(path, 0755) != 0) {
        fprintf(stderr, "mkdir(%s) failed\n", path);
        exit(1);
    }
}

static void make_file(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "fopen(%s) failed\n", path);
        exit(1);
    }
    fputs("x", f);
    fclose(f);
}

/* ディレクトリは一覧に出さない。拡張子フィルタ・隠しファイル除外は
 * browser.cと同じ(has_music_ext()相当)。名前順は大小文字無視。 */
static int test_open_dir_filters_and_sorts(void) {
    make_dir(path_in("sub"));
    make_file(path_in("Zoo.gbs"));
    make_file(path_in("apple.m3u"));
    make_file(path_in("readme.txt"));
    make_file(path_in(".hidden.gbs"));

    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK(playctx_open_dir(&ctx, path_in("Zoo.gbs"), 0, 0) == 0);
    CHECK(ctx.kind == PLAYCTX_DIRECTORY);
    CHECK_STREQ(ctx.dir, g_tmpdir);
    /* sub(ディレクトリ)・readme.txt(拡張子外)・.hidden.gbs(隠しファイル)は
     * 除外され、apple.m3u と Zoo.gbs だけが残る。 */
    CHECK(ctx.count == 2);
    CHECK_STREQ(ctx.names[0], "apple.m3u");
    CHECK_STREQ(ctx.names[1], "Zoo.gbs");
    CHECK(ctx.current == 1); /* Zoo.gbsを開いたのでその添字 */

    playctx_free(&ctx);
    CHECK(ctx.count == 0);
    CHECK(ctx.dir == NULL);
    return 0;
}

/* show_all!=0なら拡張子フィルタ無しですべてのファイルを列挙する
 * (browser.cのBROWSER_FILTER_ALLと同じ意味)。 */
static int test_open_dir_show_all(void) {
    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK(playctx_open_dir(&ctx, path_in("Zoo.gbs"), 1, 0) == 0);
    CHECK(ctx.count == 3); /* apple.m3u, readme.txt, Zoo.gbs */
    CHECK_STREQ(ctx.names[1], "readme.txt");
    playctx_free(&ctx);
    return 0;
}

/* playctx_step_path(): 端は反対側へ折り返る。currentが一覧の中にある
 * ときは、そこからの相対移動になる。 */
static int test_step_path_wraps(void) {
    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK(playctx_open_dir(&ctx, path_in("apple.m3u"), 0, 0) == 0);
    CHECK(ctx.count == 2);
    CHECK(ctx.current == 0); /* apple.m3u */

    char out[512];
    CHECK(playctx_step_path(&ctx, -1, out, sizeof(out)) == 0);
    CHECK_STREQ(out, path_in("Zoo.gbs")); /* 先頭からdelta=-1 -> 末尾へ折り返る */

    CHECK(playctx_step_path(&ctx, 1, out, sizeof(out)) == 0);
    CHECK_STREQ(out, path_in("Zoo.gbs")); /* currentはapple.m3u(0)のまま。0+1=1 */

    playctx_free(&ctx);
    return 0;
}

/* current==-1(一覧に無い場所から開いた。zip内等)からのstepは、
 * delta>0なら先頭、delta<0なら末尾を返す(browser_move_wrap()には無い
 * 分岐。playctx.h参照)。 */
static int test_step_path_no_current(void) {
    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    /* g_tmpdir直下に無い名前(zip内エントリ相当)を渡す。 */
    char fake[512];
    snprintf(fake, sizeof(fake), "%s/inside.zip:Song.gbs", g_tmpdir);
    CHECK(playctx_open_dir(&ctx, fake, 0, 0) == 0);
    CHECK(ctx.current == -1);

    char out[512];
    CHECK(playctx_step_path(&ctx, 1, out, sizeof(out)) == 0);
    CHECK_STREQ(out, path_in("apple.m3u")); /* delta>0 -> 先頭 */
    CHECK(playctx_step_path(&ctx, -1, out, sizeof(out)) == 0);
    CHECK_STREQ(out, path_in("Zoo.gbs")); /* delta<0 -> 末尾 */

    playctx_free(&ctx);
    return 0;
}

/* force_rescan=0のとき、既に同じディレクトリを開いていればreaddirを
 * やり直さない(ファイルを1つ増やしても見えないことで確認する)。
 * force_rescan=1なら見える。 */
static int test_force_rescan(void) {
    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK(playctx_open_dir(&ctx, path_in("apple.m3u"), 0, 0) == 0);
    CHECK(ctx.count == 2);

    make_file(path_in("New.gbs"));

    CHECK(playctx_open_dir(&ctx, path_in("apple.m3u"), 0, 0) == 0);
    CHECK(ctx.count == 2); /* 再走査していないので増えて見えない */

    CHECK(playctx_open_dir(&ctx, path_in("apple.m3u"), 0, 1) == 0);
    CHECK(ctx.count == 3); /* force_rescan=1で見える */

    playctx_free(&ctx);
    return 0;
}

/* 開けないディレクトリを渡すと-1を返し、*ctxは変更しない
 * (browser_open_dir()と同じ契約)。 */
static int test_open_dir_failure_keeps_state(void) {
    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK(playctx_open_dir(&ctx, path_in("apple.m3u"), 0, 0) == 0);
    int before_count = ctx.count;

    CHECK(playctx_open_dir(&ctx, "/no/such/dir/song.gbs", 0, 0) == -1);
    CHECK(ctx.count == before_count); /* 直前の状態のまま */

    playctx_free(&ctx);
    return 0;
}

/* countが0(空一覧)のときはstep_pathも失敗する。 */
static int test_step_path_empty(void) {
    playctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    char out[512];
    out[0] = 'x';
    CHECK(playctx_step_path(&ctx, 1, out, sizeof(out)) == -1);
    CHECK(out[0] == 0);
    return 0;
}

int main(void) {
    setup_tmpdir("playctx");

    if (test_open_dir_filters_and_sorts()) return 1;
    if (test_open_dir_show_all()) return 1;
    if (test_step_path_wraps()) return 1;
    if (test_step_path_no_current()) return 1;
    if (test_force_rescan()) return 1;
    if (test_open_dir_failure_keeps_state()) return 1;
    if (test_step_path_empty()) return 1;

    printf("test_playctx: すべて成功\n");
    return 0;
}

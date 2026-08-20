/* test_browser.c - browser.c の単体テスト。 (P5)
 *
 * SDLに依存しない純粋なディレクトリ走査ロジックなので、一時ディレクトリを
 * 実際に作ってCTestから素の実行ファイルとして検証する
 * (tests/test_playlist.c と同じ流儀)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "browser.h"
#include "test_util.h"

/* g_tmpdir / setup_tmpdir() / path_in() は test_util.h にある。 */

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

/* ディレクトリ優先 -> 大小文字無視の名前順(browser.hの契約)。
 * 拡張子フィルタ(.gbs/.gb/.nsf/.nsfe/.spc/.m3u/.zip)と隠しファイル除外も
 * 確認する(Issue #2: .nsf/.nsfeをGBSと同格の一級市民として扱う。
 * Issue #43: .spcも同様)。 */
static int test_sort_and_filter(void) {
    make_dir(path_in("sub"));
    make_file(path_in("Zoo.gbs"));
    make_file(path_in("apple.m3u"));
    make_file(path_in("Archive.zip"));
    make_file(path_in("Song.nsf"));
    make_file(path_in("Song.nsfe"));
    make_file(path_in("Song.spc"));
    make_file(path_in("readme.txt"));
    make_file(path_in(".hidden.gbs"));

    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 0) == 0);
    /* .hidden.gbs は常に除外。readme.txt は拡張子フィルタで除外(show_all=0)。
     * 残り: sub(dir), apple.m3u, Archive.zip, Song.nsf, Song.nsfe, Song.spc,
     * Zoo.gbs ("apple" < "archive" < "song.nsf" < "song.nsfe" < "song.spc" <
     * "zoo" の大小文字無視の辞書順。"song.nsf"は"song.nsfe"の前方一致なので
     * 短い方が先に来る) */
    CHECK(b.count == 7);
    CHECK(b.items[0].is_dir);
    CHECK_STREQ(b.items[0].name, "sub");
    CHECK(!b.items[1].is_dir);
    CHECK_STREQ(b.items[1].name, "apple.m3u");
    CHECK_STREQ(b.items[2].name, "Archive.zip");
    CHECK_STREQ(b.items[3].name, "Song.nsf");
    CHECK_STREQ(b.items[4].name, "Song.nsfe");
    CHECK_STREQ(b.items[5].name, "Song.spc");
    CHECK_STREQ(b.items[6].name, "Zoo.gbs");
    browser_free(&b);

    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 1) == 0);
    /* show_all=1: readme.txtも含まれる。隠しファイルは変わらず除外される。 */
    CHECK(b.count == 8);
    browser_free(&b);

    return 0;
}

static int test_move_and_page(void) {
    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 1) == 0);
    CHECK(b.count == 8);

    browser_move(&b, -10);
    CHECK(b.selected == 0);
    browser_move(&b, 2);
    CHECK(b.selected == 2);
    browser_page(&b, 100);
    CHECK(b.selected == b.count - 1);
    browser_page(&b, -100);
    CHECK(b.selected == 0);

    browser_free(&b);
    return 0;
}

/* P9: 1ステップのカーソル移動は端で反対側へ折り返す。
 * ページ送りは従来どおりクランプすること(上の test_move_and_page が担保)。 */
static int test_move_wrap(void) {
    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 1) == 0);
    CHECK(b.count == 8);

    b.selected = 0;
    browser_move_wrap(&b, -1);
    CHECK(b.selected == b.count - 1); /* 先頭からUP -> 末尾へ */
    browser_move_wrap(&b, 1);
    CHECK(b.selected == 0);           /* 末尾からDOWN -> 先頭へ */

    b.selected = 2;
    browser_move_wrap(&b, 1);
    CHECK(b.selected == 3);           /* 途中は普通に動く */

    /* ±1以外の値でも素直に回ること(剰余実装の確認)。 */
    b.selected = 0;
    browser_move_wrap(&b, -9);
    CHECK(b.selected == 7);           /* (0-9) mod 8 == 7 */

    browser_free(&b);

    /* 空のディレクトリでは何もしない(selectedを触らない)。 */
    browser_t empty;
    memset(&empty, 0, sizeof(empty));
    browser_move_wrap(&empty, 1);
    CHECK(empty.selected == 0);
    CHECK(empty.count == 0);

    return 0;
}

static int test_enter_and_up(void) {
    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 0) == 0);
    CHECK(b.count == 7);
    CHECK(b.items[0].is_dir); /* "sub" */

    b.selected = 0;
    CHECK(browser_enter(&b, 0) == 1);
    CHECK_STREQ(b.cwd, path_in("sub"));
    CHECK(b.count == 0); /* subは空ディレクトリ */

    CHECK(browser_up(&b, 0) == 1);
    CHECK_STREQ(b.cwd, g_tmpdir);
    CHECK(b.count == 7); /* 戻ってきたら一覧が再構築されている */

    /* ファイル(apple.m3u)へのenterは何もせず0を返す */
    b.selected = 1;
    CHECK(!b.items[1].is_dir);
    CHECK(browser_enter(&b, 0) == 0);
    CHECK_STREQ(b.cwd, g_tmpdir);

    char path[512];
    CHECK(browser_selected_path(&b, path, sizeof(path)) == 0);
    CHECK_STREQ(path, path_in("apple.m3u"));

    browser_free(&b);
    return 0;
}

/* browser_up() はファイルシステムのルートで止まり、無限に遡らない。 */
static int test_root_boundary(void) {
    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, "/", 1) == 0);
    CHECK(browser_up(&b, 1) == 0);
    CHECK_STREQ(b.cwd, "/");
    browser_free(&b);
    return 0;
}

/* P6: Settings画面が show_all_files を切り替えたとき、呼び出し側は
 * browser_open_dir(&browser, browser.cwd, new_show_all) のように
 * b->cwd 自身を path として渡して同じディレクトリを再走査する
 * (browser_select_by_name 等を挟まない最短経路)。実装は一時
 * free(b->cwd) してから dup_str(path) していたため、path==b->cwd の
 * ときに解放済みメモリを読むuse-after-freeがあった(P6で発見・修正)。
 * ここでは実際に b->cwd を path として渡し、ASan下で回帰しないことを見る。 */
static int test_self_refresh_same_cwd_pointer(void) {
    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 0) == 0);
    CHECK(b.count == 7);

    /* path として b.cwd 自身のポインタを渡す。 */
    CHECK(browser_open_dir(&b, b.cwd, 1) == 0);
    CHECK(b.count == 8); /* show_all=1 で readme.txt が増える */
    CHECK_STREQ(b.cwd, g_tmpdir);

    browser_free(&b);
    return 0;
}

/* P6: F-13(last_pathの復元)がファイルを指していた場合、app.cの
 * restore_last_path()は親ディレクトリを開いてからこの関数でカーソルを
 * そのファイルへ合わせる。 */
static int test_select_by_name(void) {
    browser_t b;
    memset(&b, 0, sizeof(b));
    CHECK(browser_open_dir(&b, g_tmpdir, 0) == 0);
    CHECK(b.count == 7); /* sub, apple.m3u, Archive.zip, Song.nsf, Song.nsfe, Song.spc, Zoo.gbs */

    CHECK(browser_select_by_name(&b, "Archive.zip") == 1);
    CHECK(b.selected == 2);

    /* 見つからない名前ではselectedを変えず0を返す。 */
    b.selected = 0;
    CHECK(browser_select_by_name(&b, "does_not_exist.gbs") == 0);
    CHECK(b.selected == 0);

    browser_free(&b);
    return 0;
}

int main(void) {
    setup_tmpdir("browser");

    if (test_sort_and_filter()) return 1;
    if (test_move_and_page()) return 1;
    if (test_move_wrap()) return 1;
    if (test_enter_and_up()) return 1;
    if (test_root_boundary()) return 1;
    if (test_self_refresh_same_cwd_pointer()) return 1;
    if (test_select_by_name()) return 1;

    printf("test_browser: すべて成功\n");
    return 0;
}

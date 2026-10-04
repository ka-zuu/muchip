/* test_favorites.c - favorites.c の単体テスト。 (Issue #18)
 *
 * SDL/libgmeに依存しない永続化ロジックなので、一時ディレクトリに実際に
 * ファイルを作って検証する(tests/test_playctx.c と同じ流儀)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "favorites.h"
#include "test_util.h"

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "fopen(%s) failed\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static int test_toggle_add_remove(void) {
    favorites_t fav;
    memset(&fav, 0, sizeof(fav));

    CHECK(favorites_toggle(&fav, "/m/a.zip", "a.gbs", 2, "Song A") == 1);
    CHECK(favorites_toggle(&fav, "/m/b.gbs", "/m/b.gbs", 0, "Song B") == 1);
    CHECK(fav.count == 2);
    CHECK(favorites_find(&fav, "/m/a.zip", "a.gbs", 2) == 0);
    CHECK(favorites_find(&fav, "/m/b.gbs", "/m/b.gbs", 0) == 1);
    /* 3つ組のどれが違っても別の曲 */
    CHECK(favorites_find(&fav, "/m/a.zip", "a.gbs", 3) == -1);
    CHECK(favorites_find(&fav, "/m/a.zip", "x.gbs", 2) == -1);
    CHECK(favorites_find(&fav, "/m/c.zip", "a.gbs", 2) == -1);

    /* 同じ曲をもう一度トグルすると削除され、残りの順序は保たれる */
    CHECK(favorites_toggle(&fav, "/m/a.zip", "a.gbs", 2, "Song A") == 0);
    CHECK(fav.count == 1);
    CHECK_STREQ(fav.items[0].title, "Song B");

    /* タブ・改行を含む同定キーは拒否、titleは空白化して保持 */
    CHECK(favorites_toggle(&fav, "/m/x\ty", "k", 0, "t") == -1);
    CHECK(favorites_toggle(&fav, "/m/x", "k\n", 0, "t") == -1);
    CHECK(favorites_toggle(&fav, "/m/x", "k", -1, "t") == -1);
    CHECK(favorites_toggle(&fav, "/m/x", "k", 0, "a\tb\nc") == 1);
    CHECK_STREQ(fav.items[1].title, "a b c");
    CHECK(fav.count == 2);

    favorites_free(&fav);
    CHECK(fav.count == 0 && fav.items == NULL);
    return 0;
}

static int test_save_load_roundtrip(void) {
    const char *path = path_in("fav.txt");
    favorites_t fav;
    memset(&fav, 0, sizeof(fav));
    CHECK(favorites_toggle(&fav, "/m/a.zip", "dir/a.gbs", 5, "日本語 title") == 1);
    CHECK(favorites_toggle(&fav, "/m/b.spc", "/m/b.spc", 0, "") == 1);
    CHECK(favorites_save(&fav, path) == 0);
    /* 一時ファイルが残らない */
    CHECK(fopen(path_in("fav.txt.tmp"), "rb") == NULL);
    favorites_free(&fav);

    favorites_t back;
    memset(&back, 0, sizeof(back));
    CHECK(favorites_load(&back, path) == 0);
    CHECK(back.count == 2);
    CHECK_STREQ(back.items[0].container, "/m/a.zip");
    CHECK_STREQ(back.items[0].source_key, "dir/a.gbs");
    CHECK(back.items[0].track_index == 5);
    CHECK_STREQ(back.items[0].title, "日本語 title");
    CHECK_STREQ(back.items[1].container, "/m/b.spc");
    CHECK_STREQ(back.items[1].title, "");
    favorites_free(&back);
    return 0;
}

static int test_load_missing_file_is_empty(void) {
    favorites_t fav;
    memset(&fav, 0, sizeof(fav));
    CHECK(favorites_toggle(&fav, "/m/a", "/m/a", 0, "x") == 1);
    /* 無いファイルは空リスト(以前の内容は破棄される) */
    CHECK(favorites_load(&fav, path_in("nonexistent.txt")) == 0);
    CHECK(fav.count == 0);
    favorites_free(&fav);
    return 0;
}

static int test_load_skips_bad_lines_and_duplicates(void) {
    const char *path = path_in("bad.txt");
    write_text(path,
               "/m/ok.gbs\t/m/ok.gbs\t1\tOK\n"
               "only-one-field\n"
               "a\tb\n"
               "/m/x\t/m/x\tnotnum\tT\n"
               "/m/x\t/m/x\t-3\tT\n"
               "\t/m/x\t0\tT\n"
               "/m/x\t/m/x\t0\ta\tb\n"
               "/m/ok.gbs\t/m/ok.gbs\t1\tdup\n"
               "\n"
               "/m/crlf.gbs\t/m/crlf.gbs\t0\tCR\r\n"
               "/m/notitle.gbs\t/m/notitle.gbs\t7");
    favorites_t fav;
    memset(&fav, 0, sizeof(fav));
    CHECK(favorites_load(&fav, path) == 0);
    CHECK(fav.count == 3);
    CHECK_STREQ(fav.items[0].title, "OK");
    CHECK_STREQ(fav.items[1].title, "CR");
    CHECK_STREQ(fav.items[2].container, "/m/notitle.gbs");
    CHECK(fav.items[2].track_index == 7);
    CHECK_STREQ(fav.items[2].title, "");
    favorites_free(&fav);
    return 0;
}

/* Issue #51: 履歴は新しい順・先頭移動・重複なし・上限で古い順に捨てる。 */
static int test_record_history(void) {
    favorites_t h;
    memset(&h, 0, sizeof(h));

    CHECK(favorites_record(&h, "/m/a", "/m/a", 0, "A0", 3) == 0);
    CHECK(favorites_record(&h, "/m/a", "/m/a", 1, "A1", 3) == 0);
    CHECK(favorites_record(&h, "/m/b", "/m/b", 0, "B0", 3) == 0);
    CHECK(h.count == 3);
    CHECK_STREQ(h.items[0].title, "B0"); /* 新しいものが先頭 */
    CHECK_STREQ(h.items[1].title, "A1");
    CHECK_STREQ(h.items[2].title, "A0");

    /* 既存の曲は先頭へ移り、重複は増えない */
    CHECK(favorites_record(&h, "/m/a", "/m/a", 0, "A0", 3) == 0);
    CHECK(h.count == 3);
    CHECK_STREQ(h.items[0].title, "A0");
    CHECK_STREQ(h.items[1].title, "B0");
    CHECK_STREQ(h.items[2].title, "A1");
    /* 既に先頭なら位置は変わらず、titleだけ最新になる */
    CHECK(favorites_record(&h, "/m/a", "/m/a", 0, "A0 renamed\t!", 3) == 0);
    CHECK(h.count == 3);
    CHECK_STREQ(h.items[0].title, "A0 renamed !");

    /* 上限を超えたら末尾(最も古い)から捨てる */
    CHECK(favorites_record(&h, "/m/c", "/m/c", 0, "C0", 3) == 0);
    CHECK(h.count == 3);
    CHECK_STREQ(h.items[0].title, "C0");
    CHECK_STREQ(h.items[2].title, "B0");
    CHECK(favorites_find(&h, "/m/a", "/m/a", 1) == -1); /* A1が押し出された */

    /* 不正な同定キー・上限0は拒否し、変更しない */
    CHECK(favorites_record(&h, "/m/x\ty", "k", 0, "t", 3) == -1);
    CHECK(favorites_record(&h, "/m/x", "k", 0, "t", 0) == -1);
    CHECK(h.count == 3);

    favorites_trim(&h, 1);
    CHECK(h.count == 1);
    CHECK_STREQ(h.items[0].title, "C0");
    favorites_trim(&h, 5); /* 増えない */
    CHECK(h.count == 1);

    favorites_free(&h);
    return 0;
}

static int test_resolve_path(void) {
    char out[256];
    unsetenv("MUCHIP_FAVORITES");
    favorites_resolve_path(out, sizeof(out), "/mnt/app/config.ini");
    CHECK_STREQ(out, "/mnt/app/favorites.txt");
    favorites_resolve_path(out, sizeof(out), "./config.ini");
    CHECK_STREQ(out, "./favorites.txt");
    favorites_resolve_path(out, sizeof(out), "config.ini"); /* '/'無し: カレント */
    CHECK_STREQ(out, "favorites.txt");
    favorites_resolve_path(out, sizeof(out), NULL);
    CHECK_STREQ(out, "favorites.txt");
    /* 環境変数が最優先 */
    setenv("MUCHIP_FAVORITES", "/tmp/x/fav.txt", 1);
    favorites_resolve_path(out, sizeof(out), "/mnt/app/config.ini");
    CHECK_STREQ(out, "/tmp/x/fav.txt");
    unsetenv("MUCHIP_FAVORITES");

    /* 履歴(Issue #51): 同じ規則で history.txt / MUCHIP_HISTORY */
    unsetenv("MUCHIP_HISTORY");
    history_resolve_path(out, sizeof(out), "/mnt/app/config.ini");
    CHECK_STREQ(out, "/mnt/app/history.txt");
    setenv("MUCHIP_HISTORY", "/tmp/x/h.txt", 1);
    history_resolve_path(out, sizeof(out), "/mnt/app/config.ini");
    CHECK_STREQ(out, "/tmp/x/h.txt");
    unsetenv("MUCHIP_HISTORY");
    return 0;
}

int main(void) {
    setup_tmpdir("favorites");
    if (test_toggle_add_remove()) return 1;
    if (test_save_load_roundtrip()) return 1;
    if (test_load_missing_file_is_empty()) return 1;
    if (test_load_skips_bad_lines_and_duplicates()) return 1;
    if (test_record_history()) return 1;
    if (test_resolve_path()) return 1;
    printf("test_favorites: OK\n");
    return 0;
}

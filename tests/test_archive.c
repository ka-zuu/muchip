/* test_archive.c - archive.c (miniz) の単体テスト。
 *
 * miniz自身のライタAPIでテスト用zipを実行時に生成し、大文字小文字/
 * パス区切りの揺れ吸収 (archive_find) と展開 (archive_extract) を検証する。
 * 32MB超の展開拒否は、実際に32MBのデータを圧縮すると重いため、
 * MAX_EXTRACT_SIZE相当の閾値をヘッダ経由ではなく、あえて大きい
 * 非圧縮エントリを小さく作れる miniz の STORE (無圧縮) モードで検証する。
 */
#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "miniz.h"
#include "test_util.h"

/* CHECKマクロはmain以外の関数からもreturn 1したいところだが、zip生成の
 * セットアップ部分は失敗したら即座にテスト自体を諦めてよいので、
 * こちらは exit(1) するだけの軽量版にする。 */
#define CHECK_VOID(cond)                                                            \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL(setup) %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                               \
        }                                                                           \
    } while (0)

static char g_tmpdir[256];

static void setup_tmpdir(void) {
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s/mugbs_test_archive_XXXXXX", base);
    if (!mkdtemp(g_tmpdir)) {
        fprintf(stderr, "mkdtemp failed\n");
        exit(1);
    }
}

static char *path_in(const char *name) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", g_tmpdir, name);
    return strdup(buf);
}

/* 指定したエントリ名・内容でzipを1つ書き出す。 */
static void write_test_zip(const char *zip_path, const char *const *names,
                            const char *const *contents, int count) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    mz_bool ok = mz_zip_writer_init_file(&zip, zip_path, 0);
    CHECK_VOID(ok);
    for (int i = 0; i < count; i++) {
        ok = mz_zip_writer_add_mem(&zip, names[i], contents[i], strlen(contents[i]),
                                    MZ_BEST_COMPRESSION);
        CHECK_VOID(ok);
    }
    ok = mz_zip_writer_finalize_archive(&zip);
    CHECK_VOID(ok);
    mz_zip_writer_end(&zip);
}

static int test_list_and_classify(void) {
    char *zip_path = path_in("game.zip");
    const char *names[] = {"Game.gbs", "Game.m3u", "readme.txt", "sub/Other.GBS"};
    const char *contents[] = {"gbsdata", "m3udata", "text", "otherdata"};
    write_test_zip(zip_path, names, contents, 4);

    archive_t *ar = NULL;
    CHECK(archive_open(zip_path, &ar) == 0);

    archive_entry_t *entries = NULL;
    int count = 0;
    CHECK(archive_list(ar, &entries, &count) == 0);
    /* readme.txt は対象外拡張子なので列挙されない -> 3件 */
    CHECK(count == 3);

    int music = 0, m3u = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_music) music++;
        if (entries[i].is_m3u) m3u++;
    }
    CHECK(music == 2); /* Game.gbs, sub/Other.GBS */
    CHECK(m3u == 1);

    archive_free_entries(entries, count);
    archive_close(ar);
    return 0;
}

/* SPEC 5.3: zip内のパス区切り・大文字小文字の揺れを吸収して
 * m3uの参照を解決できること。 */
static int test_find_case_and_path_insensitive(void) {
    char *zip_path = path_in("case.zip");
    const char *names[] = {"Sub/Dir/Game.GBS"};
    const char *contents[] = {"data"};
    write_test_zip(zip_path, names, contents, 1);

    archive_t *ar = NULL;
    CHECK(archive_open(zip_path, &ar) == 0);

    /* m3uが "game.gbs" と書いていても一致すること */
    CHECK(archive_find(ar, "game.gbs") >= 0);
    /* バックスラッシュ区切りで書かれていても一致すること */
    CHECK(archive_find(ar, "Sub\\Dir\\Game.gbs") >= 0);
    /* 存在しない名前は見つからないこと */
    CHECK(archive_find(ar, "nonexistent.gbs") < 0);

    archive_close(ar);
    return 0;
}

static int test_extract_roundtrip(void) {
    char *zip_path = path_in("roundtrip.zip");
    const char *names[] = {"a.gbs"};
    const char *contents[] = {"hello world synthetic gbs content"};
    write_test_zip(zip_path, names, contents, 1);

    archive_t *ar = NULL;
    CHECK(archive_open(zip_path, &ar) == 0);

    int idx = archive_find(ar, "a.gbs");
    CHECK(idx >= 0);

    void *data = NULL;
    size_t size = 0;
    CHECK(archive_extract(ar, idx, &data, &size) == 0);
    CHECK(size == strlen(contents[0]));
    CHECK(memcmp(data, contents[0], size) == 0);
    free(data);

    archive_close(ar);
    return 0;
}

/* SPEC 5.3: 展開後サイズが32MBを超えるエントリは拒否する(メモリ保護)。
 * 全ゼロデータは圧縮率が極めて高いので、圧縮zipのサイズを小さく保ったまま
 * 展開後サイズだけ33MBのエントリを作れる。 */
static int test_reject_oversized_entry(void) {
    const size_t big_size = 33 * 1024 * 1024;
    char *big = calloc(1, big_size);
    CHECK(big != NULL);

    char *zip_path = path_in("big.zip");
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    CHECK_VOID(mz_zip_writer_init_file(&zip, zip_path, 0));
    CHECK_VOID(mz_zip_writer_add_mem(&zip, "huge.gbs", big, big_size, MZ_BEST_COMPRESSION));
    CHECK_VOID(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
    free(big);

    archive_t *ar = NULL;
    CHECK(archive_open(zip_path, &ar) == 0);

    int idx = archive_find(ar, "huge.gbs");
    CHECK(idx >= 0);

    void *data = NULL;
    size_t size = 0;
    CHECK(archive_extract(ar, idx, &data, &size) != 0); /* 32MB超は拒否されること */

    archive_close(ar);
    return 0;
}

int main(void) {
    setup_tmpdir();

    if (test_list_and_classify()) return 1;
    if (test_find_case_and_path_insensitive()) return 1;
    if (test_reject_oversized_entry()) return 1;
    if (test_extract_roundtrip()) return 1;

    printf("test_archive: すべて成功\n");
    return 0;
}

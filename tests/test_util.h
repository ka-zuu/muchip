/* test_util.h - CTestから実行する各テストプログラム用の軽量チェックマクロ。
 *
 * SPEC 12 の「エラー処理: 戻り値でエラーを返す。assertに頼らない」は
 * プロダクションコードの方針であり、テストコード自体は失敗時に
 * メッセージを出して main() から非0を返す方式に統一する(CTestは
 * 終了コードで成否を判定するため、これで十分)。
 */
#ifndef MUGBS_TEST_UTIL_H
#define MUGBS_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                       \
        }                                                                    \
    } while (0)

#define CHECK_STREQ(a, b)                                                     \
    do {                                                                     \
        const char *_a = (a), *_b = (b);                                     \
        if (!_a || !_b || strcmp(_a, _b) != 0) {                             \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,      \
                    __LINE__, _a ? _a : "(null)", _b ? _b : "(null)");       \
            return 1;                                                       \
        }                                                                    \
    } while (0)

/* --- 一時ディレクトリ (test_playlist / test_archive / test_browser 共通) ---
 *
 * 実ファイルを作って検証するテストが3つあり、いずれも同じ
 * 「mkdtemp で作った一時ディレクトリ + その中のパスを組み立てる」コードを
 * 各自で持っていた。P13 でここへ集約した。
 *
 * path_in() が strdup を返すのは、呼び出し側が複数の結果を同時に保持しても
 * 上書きされないようにするため(静的バッファの使い回しだと
 *   char *gbs = path_in("a.gbs"); char *m3u = path_in("a.m3u");
 * が壊れる)。以前は「プロセスが短命なテストだから」と解放していなかったが、
 * それだと LeakSanitizer が毎回このリークを報告してしまい、CI で ASan を
 * 回すのに ASAN_OPTIONS=detect_leaks=0 が必要になる。それをやると
 * 本体(src/ 以下)のリークも同時に見逃すことになるので、ハーネス側で
 * 確保したものを覚えておいて atexit で一括解放する方式に変えた。
 *
 * atexit ハンドラは LIFO で走り、LeakSanitizer の検査は初期化時
 * (main より前)に登録されるので、こちらの解放が必ず先に実行される。
 *
 * これらの変数・関数は「使わないテストからも見える」が、参照している
 * static inline 関数がある限り -Wunused-variable は出ない
 * (逆に、ここを素の static 関数にすると test_m3u などで
 *  「defined but not used」になる)。
 */
#define MUGBS_TEST_PATHS_MAX 256

static char g_tmpdir[256];
static char *g_test_paths[MUGBS_TEST_PATHS_MAX];
static int g_test_path_count;
static int g_test_paths_atexit_done;

static inline void test_free_paths(void) {
    int i;
    for (i = 0; i < g_test_path_count; i++) free(g_test_paths[i]);
    g_test_path_count = 0;
}

/* g_tmpdir に "<TMPDIR>/mugbs_test_<tag>_XXXXXX" を mkdtemp で作る。
 * 各テストの main() の先頭で1度だけ呼ぶ。 */
static inline void setup_tmpdir(const char *tag) {
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s/mugbs_test_%s_XXXXXX", base, tag);
    if (!mkdtemp(g_tmpdir)) {
        fprintf(stderr, "mkdtemp failed\n");
        exit(1);
    }
}

/* g_tmpdir 直下の name のパスを返す。戻り値は atexit で解放されるので
 * 呼び出し側は free しない。 */
static inline char *path_in(const char *name) {
    char buf[512];
    char *p;
    snprintf(buf, sizeof(buf), "%s/%s", g_tmpdir, name);
    p = strdup(buf);
    if (!p) {
        fprintf(stderr, "strdup failed\n");
        exit(1);
    }
    if (g_test_path_count >= MUGBS_TEST_PATHS_MAX) {
        fprintf(stderr, "path_in: MUGBS_TEST_PATHS_MAX (%d) を超えました\n",
                MUGBS_TEST_PATHS_MAX);
        exit(1);
    }
    if (!g_test_paths_atexit_done) {
        atexit(test_free_paths);
        g_test_paths_atexit_done = 1;
    }
    g_test_paths[g_test_path_count++] = p;
    return p;
}

#endif /* MUGBS_TEST_UTIL_H */

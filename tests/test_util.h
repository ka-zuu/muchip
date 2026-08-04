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

#endif /* MUGBS_TEST_UTIL_H */

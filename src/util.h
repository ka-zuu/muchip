/* util.h - 小さな文字列ヘルパ。playlist.c と browser.c の双方が拡張子
 * 判定を必要とするため、ここに切り出して共有する。
 */
#ifndef MUGBS_UTIL_H
#define MUGBS_UTIL_H

#include <ctype.h>
#include <string.h>

static inline int ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    if (xl > sl) return 0;
    for (size_t i = 0; i < xl; i++) {
        if (tolower((unsigned char)s[sl - xl + i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

#endif /* MUGBS_UTIL_H */

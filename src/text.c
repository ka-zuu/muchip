#include "text.h"

#include <stdlib.h>
#include <string.h>

#include "../vendor/cp932/cp932_to_ucs.h"

int text_is_valid_utf8(const char *s, size_t n) {
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char c0 = u[i];
        if (c0 < 0x80) {
            i++;
            continue;
        }

        int len;
        unsigned cp;
        unsigned min_cp;
        if ((c0 & 0xE0) == 0xC0) { len = 2; cp = c0 & 0x1F; min_cp = 0x80; }
        else if ((c0 & 0xF0) == 0xE0) { len = 3; cp = c0 & 0x0F; min_cp = 0x800; }
        else if ((c0 & 0xF8) == 0xF0) { len = 4; cp = c0 & 0x07; min_cp = 0x10000; }
        else return 0; /* 先頭バイトとして不正、または単独の継続バイト */

        if (i + (size_t)len > n) return 0; /* 途中で切れている */

        for (int k = 1; k < len; k++) {
            unsigned char ck = u[i + (size_t)k];
            if ((ck & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (ck & 0x3Fu);
        }

        if (cp < min_cp) return 0;                    /* overlong 符号化 */
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;    /* サロゲート域 */
        if (cp > 0x10FFFF) return 0;

        i += (size_t)len;
    }
    return 1;
}

/* lead/trailからUnicodeコードポイントを引く。未定義なら0。 */
static unsigned cp932_lookup(unsigned char lead, unsigned char trail) {
    int row = cp932_lead_row(lead);
    if (row < 0) return 0;
    if (trail < CP932_TRAIL_MIN || trail > CP932_TRAIL_MAX) return 0;
    return cp932_to_ucs[(size_t)row * CP932_TRAIL_COUNT + (size_t)(trail - CP932_TRAIL_MIN)];
}

/* s[i]から1文字分を読む。成功時 out_cp と out_len をセットして1を返す。
 * 失敗(不正なバイト・末尾で2バイト目が無い等)なら0を返す(呼び出し側の
 * 責務で処理する。out_cp と out_len は変更しない)。 */
static int cp932_decode_one(const unsigned char *s, size_t n, size_t i,
                             unsigned *out_cp, size_t *out_len) {
    unsigned char b0 = s[i];
    if (b0 < 0x80) {
        *out_cp = b0;
        *out_len = 1;
        return 1;
    }
    if (b0 >= 0xA1 && b0 <= 0xDF) {
        /* 半角カナ: JIS X 0201 片仮名。 */
        *out_cp = 0xFF61u + (unsigned)(b0 - 0xA1u);
        *out_len = 1;
        return 1;
    }
    if (i + 1 >= n) return 0; /* lead バイトなのに2バイト目が無い */
    unsigned cp = cp932_lookup(b0, s[i + 1]);
    if (cp == 0) return 0;
    *out_cp = cp;
    *out_len = 2;
    return 1;
}

/* 先頭から厳密にCP932としてデコードできたバイト数を返す(全体が妥当なら
 * n と等しい)。停止位置が「末尾に残ったリードバイト1個」だった場合だけ
 * *out_truncated_tail に1を書く(固定長フィールドの途中切れ。Issue #40。
 * NSF/GBSのゲーム名等は32バイト固定で、日本語の2バイト目が切り捨て
 * られたファイルが実在する)。それ以外の破綻(未定義の符号位置・不正な
 * trailバイト・0x80/0xA0/0xFD-0xFFのようなリードですらないバイト等)
 * では0のままにする。 */
static size_t cp932_scan(const char *s, size_t n, int *out_truncated_tail) {
    const unsigned char *u = (const unsigned char *)s;
    *out_truncated_tail = 0;
    size_t i = 0;
    while (i < n) {
        unsigned cp;
        size_t len;
        if (!cp932_decode_one(u, n, i, &cp, &len)) {
            if (i + 1 == n && cp932_lead_row(u[i]) >= 0) {
                *out_truncated_tail = 1;
            }
            break;
        }
        i += len;
    }
    return i;
}

int text_is_valid_cp932(const char *s, size_t n) {
    int truncated;
    return cp932_scan(s, n, &truncated) == n;
}

/* コードポイントをUTF-8へエンコードする(BMP範囲まで。cp932由来は
 * 最大でも3バイトのUTF-8で表現できる)。戻り値は書き込んだバイト数。 */
static size_t utf8_encode_bmp(unsigned cp, char out[3]) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3Fu));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80 | (cp & 0x3Fu));
    return 3;
}

size_t text_cp932_to_utf8(const char *in, size_t in_len, char *out, size_t out_size) {
    if (out_size == 0) return 0;

    const unsigned char *u = (const unsigned char *)in;
    size_t i = 0, o = 0;
    while (i < in_len) {
        unsigned cp;
        size_t len;
        if (!cp932_decode_one(u, in_len, i, &cp, &len)) {
            cp = '?';
            len = 1;
        }

        char enc[3];
        size_t elen = utf8_encode_bmp(cp, enc);
        if (o + elen > out_size - 1) break; /* NUL終端分を残して打ち切り */
        memcpy(out + o, enc, elen);
        o += elen;
        i += len;
    }
    out[o] = '\0';
    return o;
}

char *text_dup_utf8(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);

    /* 1. 全バイトASCII: 最頻ケースをそのまま複製する。 */
    int all_ascii = 1;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)s[i] >= 0x80) { all_ascii = 0; break; }
    }
    if (all_ascii) {
        char *out = malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, s, n + 1);
        return out;
    }

    /* 2. 妥当なUTF-8ならそのまま複製する(CP932判定より先に試すこと。
     *    text_dup_utf8() コメント参照)。 */
    if (text_is_valid_utf8(s, n)) {
        char *out = malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, s, n + 1);
        return out;
    }

    /* 3. 妥当なCP932ならUTF-8へ変換する。末尾がリードバイト1個だけで
     *    切れている場合(固定長フィールドの途中切れ。Issue #40)は、
     *    その1バイトを黙って落として手前まで変換する。 */
    int truncated;
    size_t k = cp932_scan(s, n, &truncated);
    if (k == n || (truncated && k > 0)) {
        size_t cap = k * 3 + 1; /* 1バイト -> 最大3バイトUTF-8 */
        char *out = malloc(cap);
        if (!out) return NULL;
        text_cp932_to_utf8(s, k, out, cap);
        return out;
    }

    /* 4. どちらでもない: ASCIIバイトは通し、それ以外は '?' へ丸める。 */
    char *out = malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        out[i] = (c < 0x80) ? (char)c : '?';
    }
    out[n] = '\0';
    return out;
}

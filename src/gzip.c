#include "gzip.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "miniz.h" /* tinfl と mz_crc32 */

enum {
    FHCRC = 0x02,
    FEXTRA = 0x04,
    FNAME = 0x08,
    FCOMMENT = 0x10,
    FRESERVED = 0xE0
};

#define GZIP_HEADER_MIN 10
#define GZIP_TRAILER 8

int gzip_is_gzip(const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    return data && size >= 2 && p[0] == 0x1F && p[1] == 0x8B;
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* 0終端文字列(FNAME/FCOMMENT)を読み飛ばす。*pos を終端の次へ進めて0、
 * 終端が見つからなければ-1。 */
static int skip_cstr(const unsigned char *p, size_t size, size_t *pos) {
    while (*pos < size) {
        if (p[(*pos)++] == 0) return 0;
    }
    return -1;
}

int gzip_inflate(const void *in, size_t in_size, void **out, size_t *out_size) {
    const unsigned char *p = (const unsigned char *)in;
    if (!gzip_is_gzip(in, in_size) || in_size < GZIP_HEADER_MIN + GZIP_TRAILER) {
        LOG_WARN("gzip: ヘッダが不正です");
        return -1;
    }
    if (p[2] != 8 || (p[3] & FRESERVED)) {
        LOG_WARN("gzip: 非対応の圧縮方式/フラグです");
        return -1;
    }

    unsigned flags = p[3];
    size_t pos = GZIP_HEADER_MIN;
    if (flags & FEXTRA) {
        if (pos + 2 > in_size) return -1;
        size_t xlen = (size_t)p[pos] | ((size_t)p[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if ((flags & FNAME) && skip_cstr(p, in_size, &pos) != 0) return -1;
    if ((flags & FCOMMENT) && skip_cstr(p, in_size, &pos) != 0) return -1;
    if (flags & FHCRC) pos += 2;
    /* deflate本体はトレーラ(CRC32 + ISIZE)の手前まで */
    if (pos + GZIP_TRAILER > in_size) {
        LOG_WARN("gzip: 切り詰められています");
        return -1;
    }

    size_t deflate_len = in_size - GZIP_TRAILER - pos;
    uint32_t want_crc = read_le32(p + in_size - GZIP_TRAILER);
    uint32_t isize = read_le32(p + in_size - 4);
    /* ISIZEは元サイズのmod 2^32。先に上限で弾き、確保サイズをこれで決める。
     * 実際の展開結果がISIZEと食い違えば(4GB超の偽装を含め)下で失敗する。 */
    if (isize > GZIP_MAX_OUTPUT_SIZE) {
        LOG_WARN("gzip: 展開後サイズが上限(%u)を超えます", GZIP_MAX_OUTPUT_SIZE);
        return -1;
    }

    /* isize==0でもmalloc(0)を避けて1バイト確保する */
    unsigned char *buf = malloc(isize ? isize : 1);
    if (!buf) return -1;

    size_t got = tinfl_decompress_mem_to_mem(buf, isize, p + pos, deflate_len, 0);
    if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || got != isize) {
        LOG_WARN("gzip: 展開に失敗しました");
        free(buf);
        return -1;
    }
    if ((uint32_t)mz_crc32(MZ_CRC32_INIT, buf, got) != want_crc) {
        LOG_WARN("gzip: CRC32が一致しません");
        free(buf);
        return -1;
    }

    *out = buf;
    *out_size = got;
    return 0;
}

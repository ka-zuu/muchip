/* test_gzip.c - src/gzip.c (gzip展開, Issue #55) の単体テスト。
 * minizのtdefで作ったraw deflateにgzipヘッダ/トレーラを付けて往復させる。 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gzip.h"
#include "miniz.h"
#include "test_util.h"

static void put_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* hdr(ヘッダ全体。可変部込み)+ raw deflate + トレーラ を組む。malloc'd。 */
static unsigned char *build_gzip(const unsigned char *hdr, size_t hdr_len,
                                 const void *plain, size_t plain_len, size_t *out_len) {
    size_t dlen = 0;
    void *def = tdefl_compress_mem_to_heap(plain, plain_len, &dlen, TDEFL_DEFAULT_MAX_PROBES);
    unsigned char *buf = malloc(hdr_len + dlen + 8);
    memcpy(buf, hdr, hdr_len);
    memcpy(buf + hdr_len, def, dlen);
    put_le32(buf + hdr_len + dlen, (uint32_t)mz_crc32(MZ_CRC32_INIT, plain, plain_len));
    put_le32(buf + hdr_len + dlen + 4, (uint32_t)plain_len);
    mz_free(def);
    *out_len = hdr_len + dlen + 8;
    return buf;
}

static const unsigned char HDR_PLAIN[10] = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3 };

static int test_roundtrip(void) {
    char plain[4096];
    for (size_t i = 0; i < sizeof plain; i++) plain[i] = (char)("Vgm abc"[i % 7]);
    size_t gl;
    unsigned char *gz = build_gzip(HDR_PLAIN, 10, plain, sizeof plain, &gl);
    void *out = NULL; size_t n = 0;
    CHECK(gzip_is_gzip(gz, gl));
    CHECK(gzip_inflate(gz, gl, &out, &n) == 0);
    CHECK(n == sizeof plain && memcmp(out, plain, n) == 0);
    free(out); free(gz);
    return 0;
}

static int test_empty_payload(void) {
    size_t gl;
    unsigned char *gz = build_gzip(HDR_PLAIN, 10, "", 0, &gl);
    void *out = NULL; size_t n = 99;
    CHECK(gzip_inflate(gz, gl, &out, &n) == 0);
    CHECK(n == 0);
    free(out); free(gz);
    return 0;
}

/* FEXTRA + FNAME + FCOMMENT + FHCRC をすべて立てたヘッダ */
static int test_optional_header_fields(void) {
    unsigned char hdr[10 + 2 + 3 + 6 + 4 + 2] = { 0x1F, 0x8B, 8, 0x04 | 0x08 | 0x10 | 0x02, 0, 0, 0, 0, 0, 3 };
    size_t p = 10;
    hdr[p++] = 3; hdr[p++] = 0; hdr[p++] = 'x'; hdr[p++] = 'y'; hdr[p++] = 'z'; /* XLEN=3 */
    memcpy(hdr + p, "a.vgm", 6); p += 6;                                       /* FNAME */
    memcpy(hdr + p, "hi!", 4); p += 4;                                          /* FCOMMENT */
    hdr[p++] = 0; hdr[p++] = 0;                                                 /* FHCRC(検証しない) */
    size_t gl;
    unsigned char *gz = build_gzip(hdr, p, "hello hello hello", 17, &gl);
    void *out = NULL; size_t n = 0;
    CHECK(gzip_inflate(gz, gl, &out, &n) == 0);
    CHECK(n == 17 && memcmp(out, "hello hello hello", 17) == 0);
    free(out); free(gz);
    return 0;
}

static int test_failures(void) {
    size_t gl;
    unsigned char *gz = build_gzip(HDR_PLAIN, 10, "abcabcabcabc", 12, &gl);
    void *out = (void *)1; size_t n = 7;

    CHECK(!gzip_is_gzip("PK", 2) && !gzip_is_gzip(NULL, 0) && !gzip_is_gzip(gz, 1));
    CHECK(gzip_inflate("PK\x03\x04................", 20, &out, &n) != 0);

    unsigned char *bad = malloc(gl);
    memcpy(bad, gz, gl);
    bad[gl - 8] ^= 0xFF; /* CRC不一致 */
    CHECK(gzip_inflate(bad, gl, &out, &n) != 0);

    memcpy(bad, gz, gl);
    put_le32(bad + gl - 4, 13); /* ISIZE不一致 */
    CHECK(gzip_inflate(bad, gl, &out, &n) != 0);

    memcpy(bad, gz, gl);
    put_le32(bad + gl - 4, GZIP_MAX_OUTPUT_SIZE + 1); /* 上限超過 */
    CHECK(gzip_inflate(bad, gl, &out, &n) != 0);

    memcpy(bad, gz, gl);
    bad[2] = 7; /* deflate以外 */
    CHECK(gzip_inflate(bad, gl, &out, &n) != 0);

    memcpy(bad, gz, gl);
    bad[3] = 0x20; /* 予約フラグ */
    CHECK(gzip_inflate(bad, gl, &out, &n) != 0);

    for (size_t cut = 0; cut < gl; cut++) { /* あらゆる位置での切り詰め */
        CHECK(gzip_inflate(gz, cut, &out, &n) != 0);
    }
    /* 失敗時は出力を触らない */
    CHECK(out == (void *)1 && n == 7);
    free(bad); free(gz);
    return 0;
}

int main(void) {
    if (test_roundtrip()) return 1;
    if (test_empty_payload()) return 1;
    if (test_optional_header_fields()) return 1;
    if (test_failures()) return 1;
    printf("test_gzip: OK\n");
    return 0;
}

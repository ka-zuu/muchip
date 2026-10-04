/* gzip.h - gzip(RFC1952)展開 (miniz)。 (Issue #55)
 *
 * VGZ(gzip圧縮されたVGM)を開くために使う。libgme自身のVGZ展開はzlib依存
 * (GME_ZLIB)だが、zlibを足すと実機のglibc互換性・バイナリサイズに響くため、
 * 同梱済みのminiz(tinfl)で展開してからメモリ経由でlibgmeに渡す
 * (docs/design-notes.md「VGZ」参照)。
 */
#ifndef MUGBS_GZIP_H
#define MUGBS_GZIP_H

#include <stddef.h>

/* 展開後サイズの上限(archive.cのzip展開と同じ32MB。メモリ保護)。 */
#define GZIP_MAX_OUTPUT_SIZE (32u * 1024 * 1024)

/* 先頭がgzipのマジック(1F 8B)なら1、それ以外は0。 */
int gzip_is_gzip(const void *data, size_t size);

/* in(gzip形式, in_size バイト)をメモリへ展開する。0で成功。
 * 失敗時は out と out_size を触らない。
 * 成功時、*out は malloc された領域で、呼び出し側が free() すること
 * (inの所有権は呼び出し側に残り、この関数は解放しない)。
 * 次のいずれかは失敗として -1 を返す:
 *   ヘッダ不正 / 非対応の圧縮方式(deflate以外) / 切り詰め /
 *   CRC32またはISIZEの不一致 / 展開後サイズがGZIP_MAX_OUTPUT_SIZE超過。 */
int gzip_inflate(const void *in, size_t in_size, void **out, size_t *out_size);

#endif /* MUGBS_GZIP_H */

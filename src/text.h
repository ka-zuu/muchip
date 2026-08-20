/* text.h - メタデータ文字列の文字コード正規化。
 *
 * NSF/GBSのヘッダやM3Uの拡張コメントに Shift_JIS(CP932) の日本語が
 * そのまま入っていることがある(Issue参照: NESM仕様上ASCII前提のフィールドに
 * 非準拠なSJISが書き込まれた実ファイルが確認された)。muChip内部の文字列
 * 表現は常にUTF-8に統一しているので、gme_track_info() 等から取得した
 * フィールドはこのモジュールを通してから複製する(src/playlist.c 参照)。
 *
 * SDLにもlibgmeにも依存しない純粋な文字列処理のみで、ホストだけで回る
 * 単体テスト(tests/test_text.c)を書けるようにしてある。
 */
#ifndef MUCHIP_TEXT_H
#define MUCHIP_TEXT_H

#include <stddef.h>

/* NUL終端文字列 s をUTF-8として正規化し、新たにmallocした文字列を返す
 * (呼び出し側がfree()すること)。判定順は以下で固定:
 *
 *   1. 全バイトがASCII(<0x80)         -> そのまま複製(最頻ケース)
 *   2. 厳密に妥当なUTF-8               -> そのまま複製
 *   3. 厳密に妥当なCP932(Shift_JIS)    -> UTF-8へ変換
 *   4. どれでもない                    -> ASCIIバイトは通し、
 *                                          0x80以上は '?' 1文字へ丸める
 *
 * UTF-8をCP932より先に試すのは、日本語のUTF-8バイト列(例: E3 81 82)が
 * CP932としても妥当なバイト列になり得るため。逆にCP932の日本語
 * (例: 83 5F)が妥当なUTF-8になることは実質無いので、この順序なら
 * 安全側に倒れる。
 *
 * 3.の判定は、末尾がCP932の2バイト文字のリードバイト1個だけで切れて
 * いる場合に限り例外を認める: その1バイトは黙って落とし、手前までを
 * 変換する(Issue #40。NSF/GBSのゲーム名等は固定長フィールドのため、
 * 日本語の2バイト目が切り捨てられた実ファイルがある。末尾以外での
 * 破綻は従来通り4.のフォールバックに落ちる)。
 *
 * s が NULL、またはmalloc失敗時は NULL を返す。 */
char *text_dup_utf8(const char *s);

/* s[0..n) が厳密に妥当なUTF-8バイト列か(overlong符号化・サロゲート域
 * U+D800-DFFF・U+10FFFF超は不正として弾く)。 */
int text_is_valid_utf8(const char *s, size_t n);

/* s[0..n) が厳密に妥当なCP932バイト列か(未定義の符号位置も不正扱い)。 */
int text_is_valid_cp932(const char *s, size_t n);

/* CP932バイト列 in[0..in_len) をUTF-8へ変換して out に書き込む
 * (NUL終端する。out_size 分の空きしか無い場合、UTF-8シーケンスの
 * 途中では打ち切らず、そこで変換をやめてNUL終端する)。
 * 不正なバイトは '?' へ丸めて1バイトだけ読み飛ばす(呼び出し側で
 * text_is_valid_cp932() を確認済みなら発生しない)。
 * 戻り値: 書き込んだバイト数(終端のNULは含まない)。 */
size_t text_cp932_to_utf8(const char *in, size_t in_len, char *out, size_t out_size);

#endif /* MUCHIP_TEXT_H */

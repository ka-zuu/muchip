/* test_text.c - text.c(メタデータ文字列のUTF-8正規化)の単体テスト。
 *
 * Issue #29: 実在のNSF(Downtown Special - 国士くん)のヘッダがCP932
 * (Shift_JIS)で書かれていたため文字化けしていた。ここでは実際のヘッダの
 * バイト列を直接埋め込んだ回帰テストを中心に、判定順序(ASCII/UTF-8/CP932)
 * の境界も固定する。
 *
 * SDLにもlibgmeにもリンクしない(text.cは純libc)。
 */
#include <string.h>

#include "test_util.h"
#include "text.h"

/* ---- 1. ASCIIのみの入力: そのまま複製される ---------------------------- */

static int test_ascii_passthrough(void) {
    char *out = text_dup_utf8("Track 01");
    CHECK(out != NULL);
    CHECK_STREQ(out, "Track 01");
    free(out);

    out = text_dup_utf8("");
    CHECK(out != NULL);
    CHECK_STREQ(out, "");
    free(out);

    return 0;
}

/* ---- 2. 既にUTF-8の日本語: 変換されず素通しされる(二重変換の防止) ------ */

static int test_utf8_passthrough(void) {
    /* "テスト" (UTF-8) */
    const char utf8_in[] = "\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88";
    char *out = text_dup_utf8(utf8_in);
    CHECK(out != NULL);
    CHECK_STREQ(out, utf8_in);
    free(out);
    return 0;
}

/* ---- 3. Issue #29の実バイト列(CP932) -----------------------------------
 * Downtown_Special__Kuniokun_no_Jidaigeki_Da_yo_Zenin_Shuugou_BGM.nsf の
 * ヘッダから直接ダンプしたバイト列(gme_track_info() が返す時点で前後の
 * 空白は既にトリムされている: vendor/game-music-emu/gme/Gme_File.cpp の
 * copy_field_() 参照)。
 */

static int test_nsf_song_field(void) {
    /* CP932: "ダウンタウンスペシャル くにおく" */
    const char song[] =
        "\x83\x5f\x83\x45\x83\x93\x83\x5e\x83\x45\x83\x93\x83\x58\x83\x79"
        "\x83\x56\x83\x83\x83\x8b\x20\x82\xad\x82\xc9\x82\xa8\x82\xad";
    const char expect_utf8[] =
        "\xe3\x83\x80\xe3\x82\xa6\xe3\x83\xb3\xe3\x82\xbf\xe3\x82\xa6\xe3"
        "\x83\xb3\xe3\x82\xb9\xe3\x83\x9a\xe3\x82\xb7\xe3\x83\xa3\xe3\x83"
        "\xab\x20\xe3\x81\x8f\xe3\x81\xab\xe3\x81\x8a\xe3\x81\x8f";

    char *out = text_dup_utf8(song);
    CHECK(out != NULL);
    CHECK_STREQ(out, expect_utf8);
    free(out);
    return 0;
}

static int test_nsf_copyright_field(void) {
    /* CP932: "1991/07/26 テクノスジャパン" (先頭のASCII日付部分はそのまま) */
    const char copyright_[] =
        "1991/07/26 \x83\x65\x83\x4e\x83\x6d\x83\x58\x83\x57\x83\x83\x83"
        "\x70\x83\x93";
    const char expect_utf8[] =
        "1991/07/26 \xe3\x83\x86\xe3\x82\xaf\xe3\x83\x8e\xe3\x82\xb9\xe3"
        "\x82\xb8\xe3\x83\xa3\xe3\x83\x91\xe3\x83\xb3";

    char *out = text_dup_utf8(copyright_);
    CHECK(out != NULL);
    CHECK_STREQ(out, expect_utf8);
    free(out);
    return 0;
}

/* ---- 4. 半角カナ(1バイト) ------------------------------------------------ */

static int test_halfwidth_kana(void) {
    /* 0xB1 = 半角「ｱ」(U+FF71) */
    const char in[] = "\xb1";
    /* U+FF71 のUTF-8: 1110xxxx 10xxxxxx 10xxxxxx */
    const char expect_utf8[] = "\xef\xbd\xb1";

    char *out = text_dup_utf8(in);
    CHECK(out != NULL);
    CHECK_STREQ(out, expect_utf8);
    free(out);
    return 0;
}

/* ---- 5. 壊れたバイト列: '?' に落ちてクラッシュしない --------------------- */

static int test_broken_bytes_fallback(void) {
    /* 0x80 単独: ASCIIでもUTF-8先頭バイトでも半角カナでもCP932 lead でもない */
    char *out = text_dup_utf8("\x80");
    CHECK(out != NULL);
    CHECK_STREQ(out, "?");
    free(out);

    /* CP932の2バイト目が欠けたまま終端 */
    out = text_dup_utf8("\x83");
    CHECK(out != NULL);
    CHECK_STREQ(out, "?");
    free(out);

    /* CP932として未定義の(lead=0x81, trail=0x7f)の組(trail=0x7fはSJISの
     * 2バイト目としては使われない値)。文字列の途中に0x00を含められないため
     * text_is_valid_cp932()/text_cp932_to_utf8() を長さ明示で直接呼ぶ。 */
    const char undefined_pair[] = { (char)0x81, (char)0x7f, (char)0x00 };
    CHECK(!text_is_valid_cp932(undefined_pair, 2));
    /* text_cp932_to_utf8() は未定義のlead(0x81)だけを'?'に丸めて1バイト
     * 読み飛ばす。残ったtrail(0x7f)は単独では有効なASCII(DEL)として
     * そのまま出る(この関数はtext_is_valid_cp932()で妥当性を確認済みの
     * 入力にしか本来使わないので、これはあくまで防御的経路の挙動)。 */
    char buf[8];
    size_t n = text_cp932_to_utf8(undefined_pair, 2, buf, sizeof(buf));
    CHECK(n == 2);
    CHECK(memcmp(buf, "?\x7f", 2) == 0);

    return 0;
}

/* ---- 6. text_is_valid_utf8() / text_is_valid_cp932() の単体境界 --------- */

static int test_is_valid_utf8(void) {
    CHECK(text_is_valid_utf8("hello", 5));
    CHECK(text_is_valid_utf8("\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88", 9)); /* テスト */

    /* overlong符号化: U+0041('A')を2バイトで表現した不正列 */
    CHECK(!text_is_valid_utf8("\xc1\x81", 2));
    /* サロゲート域 U+D800 */
    CHECK(!text_is_valid_utf8("\xed\xa0\x80", 3));
    /* U+10FFFF超 */
    CHECK(!text_is_valid_utf8("\xf4\x90\x80\x80", 4));
    /* 途中で切れている */
    CHECK(!text_is_valid_utf8("\xe3\x83", 2));
    /* CP932の日本語バイト列はUTF-8としては不正 */
    CHECK(!text_is_valid_utf8("\x83\x5f", 2));

    return 0;
}

static int test_is_valid_cp932(void) {
    CHECK(text_is_valid_cp932("hello", 5));
    CHECK(text_is_valid_cp932("\x83\x5f\x83\x45", 4)); /* "ダウ" */
    CHECK(text_is_valid_cp932("\xb1\xb2", 2));          /* 半角カナ2文字 */

    /* 0x80/0xA0/0xFD-0xFF は単独では不正 */
    CHECK(!text_is_valid_cp932("\x80", 1));
    CHECK(!text_is_valid_cp932("\xa0", 1));
    CHECK(!text_is_valid_cp932("\xff", 1));
    /* leadだけでtrailが無い */
    CHECK(!text_is_valid_cp932("\x83", 1));
    /* UTF-8の日本語はCP932としては(たまたま)妥当にならない実例 */
    CHECK(!text_is_valid_cp932("\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88", 9));

    return 0;
}

/* ---- 7. text_cp932_to_utf8() のバッファ境界 ------------------------------
 * 出力バッファが1文字分足りない場合、UTF-8シーケンスを書きかけで
 * 打ち切らずそこで止めてNUL終端すること。 */

static int test_cp932_to_utf8_buffer_boundary(void) {
    const char in[] = "\x83\x5f\x83\x45"; /* "ダウ" -> UTF-8で3+3=6バイト */
    char out[8];

    /* ちょうど収まるサイズ(6バイト+NUL=7)。 */
    size_t n = text_cp932_to_utf8(in, 4, out, 7);
    CHECK(n == 6);
    CHECK(memcmp(out, "\xe3\x83\x80\xe3\x82\xa6", 6) == 0);
    CHECK(out[6] == '\0');

    /* 1バイト足りない(6): 2文字目の3バイトが入りきらないので1文字目だけ。 */
    n = text_cp932_to_utf8(in, 4, out, 6);
    CHECK(n == 3);
    CHECK(memcmp(out, "\xe3\x83\x80", 3) == 0);
    CHECK(out[3] == '\0');

    /* 極端に小さい(1): NUL終端のみ。 */
    n = text_cp932_to_utf8(in, 4, out, 1);
    CHECK(n == 0);
    CHECK(out[0] == '\0');

    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_ascii_passthrough();
    failed |= test_utf8_passthrough();
    failed |= test_nsf_song_field();
    failed |= test_nsf_copyright_field();
    failed |= test_halfwidth_kana();
    failed |= test_broken_bytes_fallback();
    failed |= test_is_valid_utf8();
    failed |= test_is_valid_cp932();
    failed |= test_cp932_to_utf8_buffer_boundary();

    if (failed) {
        fprintf(stderr, "test_text: FAILED\n");
        return 1;
    }
    printf("test_text: OK\n");
    return 0;
}

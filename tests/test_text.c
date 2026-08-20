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

/* ---- 8. 固定長フィールドの末尾切れ(Issue #40) ----------------------------
 * NSFのゲーム名等は固定32バイトで、末尾で2バイト文字のリードバイト
 * だけが残って切れている実ファイルがある(下記3例は実機の
 * /mnt/mmc/ROMS/VGM/NSF/ で確認したゲーム名フィールドそのもの、
 * いずれも31バイト。gme_track_info() が返す時点で前後の空白は既に
 * トリムされている)。末尾のリードバイト1個は黙って落とし、手前までを
 * CP932として変換する。 */

static int test_nsf_truncated_tail_field(void) {
    /* "Super Chinese 2 - Dragon Kid (Japan) [BGM].nsf" のゲーム名。
     * 末尾 0x83 (「キ」のリードバイト)が切れている。 */
    const char super_chinese2[] =
        "\x83\x58\x81\x5b\x83\x70\x81\x5b\x83\x60\x83\x83\x83\x43\x83\x6a"
        "\x81\x5b\x83\x59\x32\x20\x83\x68\x83\x89\x83\x53\x83\x93\x83";
    const char expect_super_chinese2[] =
        "\xe3\x82\xb9\xe3\x83\xbc\xe3\x83\x91\xe3\x83\xbc\xe3\x83\x81\xe3"
        "\x83\xa3\xe3\x82\xa4\xe3\x83\x8b\xe3\x83\xbc\xe3\x82\xba\x32\x20"
        "\xe3\x83\x89\xe3\x83\xa9\xe3\x82\xb4\xe3\x83\xb3";

    char *out = text_dup_utf8(super_chinese2);
    CHECK(out != NULL);
    CHECK_STREQ(out, expect_super_chinese2);
    free(out);

    /* "Bio Senshi Dan - Increaser Tono Tatakai (Japan) [BGM].nsf" の
     * ゲーム名。ASCII("DAN")混じりで、末尾 0x82 (「と」のリードバイト)
     * が切れている。 */
    const char bio_senshi_dan[] =
        "\x83\x6f\x83\x43\x83\x49\x90\xed\x8e\x6d\x44\x41\x4e\x20\x83\x43"
        "\x83\x93\x83\x4e\x83\x8a\x81\x5b\x83\x55\x81\x5b\x82\xc6\x82";
    const char expect_bio_senshi_dan[] =
        "\xe3\x83\x90\xe3\x82\xa4\xe3\x82\xaa\xe6\x88\xa6\xe5\xa3\xab\x44"
        "\x41\x4e\x20\xe3\x82\xa4\xe3\x83\xb3\xe3\x82\xaf\xe3\x83\xaa\xe3"
        "\x83\xbc\xe3\x82\xb6\xe3\x83\xbc\xe3\x81\xa8";

    out = text_dup_utf8(bio_senshi_dan);
    CHECK(out != NULL);
    CHECK_STREQ(out, expect_bio_senshi_dan);
    free(out);

    /* ASCIIの直後で切れるだけの単純なケース。 */
    out = text_dup_utf8("ABC\x83");
    CHECK(out != NULL);
    CHECK_STREQ(out, "ABC");
    free(out);

    return 0;
}

/* 緩和が末尾以外に漏れていないことの退行テスト: 末尾より前で破綻して
 * いる場合は、これまで通り4.の '?' 丸めフォールバックに落ちること。 */
static int test_mid_string_break_still_falls_back(void) {
    /* "ン"(妥当) + 0x80(単独では不正) + "ン"(妥当)。末尾ではなく途中の
     * 破綻なので、cp932_scan()は2バイト目で止まり(次のバイトがまだ
     * 残っているのでtruncated_tail扱いにはならない)、全体がCP932として
     * 不正と判定されるべき。全バイトが0x80以上なので4.のフォールバックで
     * 全部'?'に丸まる。 */
    char *out = text_dup_utf8("\x83\x93\x80\x83\x93");
    CHECK(out != NULL);
    CHECK_STREQ(out, "?????");
    free(out);
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
    failed |= test_nsf_truncated_tail_field();
    failed |= test_mid_string_break_still_falls_back();

    if (failed) {
        fprintf(stderr, "test_text: FAILED\n");
        return 1;
    }
    printf("test_text: OK\n");
    return 0;
}

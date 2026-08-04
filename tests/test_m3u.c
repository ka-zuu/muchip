/* test_m3u.c - m3u.c のセグメント分割ロジックの単体テスト。
 *
 * トラック番号・時間のパース結果は検証しない(libgmeに委譲する部分であり、
 * m3u.c の責務ではないため)。ここで検証するのは「区間の切り方」
 * 「ファイル名の抽出」「ヘッダコメントの複製」だけ。
 */
#include <string.h>

#include "m3u.h"
#include "test_util.h"

static int test_single_file(void) {
    const char *text =
        "# コメント行\n"
        "Game.gbs::GBS,1,Title Screen,0:32,,0:05\n"
        "Game.gbs::GBS,$02,Overworld,2:34,2:34,0:08\n"
        "Game.gbs::GBS,3,Battle,1:45\n";

    m3u_segment_t *segs = NULL;
    int count = 0;
    CHECK(m3u_split_segments(text, strlen(text), &segs, &count) == 0);
    CHECK(count == 1); /* 参照ファイルが1種類のみ -> セグメントは常に1つ */
    CHECK_STREQ(segs[0].filename, "Game.gbs");

    /* 3行のエントリすべてが含まれていること */
    CHECK(strstr(segs[0].text, "Title Screen") != NULL);
    CHECK(strstr(segs[0].text, "Overworld") != NULL);
    CHECK(strstr(segs[0].text, "Battle") != NULL);
    /* ヘッダコメントも複製されていること */
    CHECK(strstr(segs[0].text, "# コメント行") != NULL);

    m3u_free_segments(segs, count);
    return 0;
}

static int test_multi_file_segments(void) {
    /* A,A,B,B,A の順 -> 3セグメントに分かれ、順序も保たれること */
    const char *text =
        "A.gbs::GBS,1,A1,0:10\n"
        "A.gbs::GBS,2,A2,0:10\n"
        "B.gbs::GBS,1,B1,0:10\n"
        "B.gbs::GBS,2,B2,0:10\n"
        "A.gbs::GBS,3,A3,0:10\n";

    m3u_segment_t *segs = NULL;
    int count = 0;
    CHECK(m3u_split_segments(text, strlen(text), &segs, &count) == 0);
    CHECK(count == 3);
    CHECK_STREQ(segs[0].filename, "A.gbs");
    CHECK_STREQ(segs[1].filename, "B.gbs");
    CHECK_STREQ(segs[2].filename, "A.gbs");

    /* セグメント0にはA1/A2は含まれるが、B1/A3は含まれない */
    CHECK(strstr(segs[0].text, "A1") != NULL);
    CHECK(strstr(segs[0].text, "A2") != NULL);
    CHECK(strstr(segs[0].text, "B1") == NULL);
    CHECK(strstr(segs[0].text, "A3") == NULL);

    /* セグメント2(2つ目のA.gbs区間)にはA3のみ */
    CHECK(strstr(segs[2].text, "A3") != NULL);
    CHECK(strstr(segs[2].text, "A1") == NULL);

    m3u_free_segments(segs, count);
    return 0;
}

static int test_blank_and_comment_lines_ignored_for_grouping(void) {
    const char *text =
        "\n"
        "# header\n"
        "\n"
        "A.gbs::GBS,1,A1,0:10\n"
        "\n"
        "# 途中のコメント\n"
        "A.gbs::GBS,2,A2,0:10\n";

    m3u_segment_t *segs = NULL;
    int count = 0;
    CHECK(m3u_split_segments(text, strlen(text), &segs, &count) == 0);
    /* 空行・コメント行を挟んでも同一ファイルなら区間は分かれない */
    CHECK(count == 1);
    CHECK(strstr(segs[0].text, "A1") != NULL);
    CHECK(strstr(segs[0].text, "A2") != NULL);

    m3u_free_segments(segs, count);
    return 0;
}

static int test_no_valid_entries_fails(void) {
    const char *text = "# コメントだけ\n\n";
    m3u_segment_t *segs = NULL;
    int count = 0;
    CHECK(m3u_split_segments(text, strlen(text), &segs, &count) != 0);
    CHECK(segs == NULL);
    CHECK(count == 0);
    return 0;
}

int main(void) {
    if (test_single_file()) return 1;
    if (test_multi_file_segments()) return 1;
    if (test_blank_and_comment_lines_ignored_for_grouping()) return 1;
    if (test_no_valid_entries_fails()) return 1;
    printf("test_m3u: すべて成功\n");
    return 0;
}

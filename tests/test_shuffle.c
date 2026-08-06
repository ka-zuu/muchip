/* test_shuffle.c - shuffle.c(シャッフル再生の並び順ロジック)の単体テスト。
 *
 * SDLにもlibgmeにも依存しない純libcなので、eq.c/m3u.cと同じくSDL_Initなしで
 * 実行できる。乱数のseedは固定しない(rand()の既定seedのまま)。テストは
 * 具体的な並び値には依存せず、「有効な順列であること」「wrap/no-wrapの
 * 境界」「syncの正しさ」といった構造的な性質だけを検証する。
 */
#include <stdio.h>
#include <string.h>

#include "shuffle.h"
#include "test_util.h"

/* order[0..count-1] が [0, count) の順列(各値がちょうど1回ずつ現れる)に
 * なっていることを確認する。 */
static int check_is_permutation(const int *order, int count) {
    char seen[64];
    memset(seen, 0, sizeof(seen));
    CHECK(count <= (int)sizeof(seen));
    for (int i = 0; i < count; i++) {
        CHECK(order[i] >= 0 && order[i] < count);
        CHECK(!seen[order[i]]);
        seen[order[i]] = 1;
    }
    return 0;
}

static int test_reset_is_permutation(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));

    shuffle_reset(&s, 7);
    CHECK(s.order != NULL);
    CHECK(s.count == 7);
    CHECK(s.pos == 0);
    if (check_is_permutation(s.order, 7)) return 1;

    shuffle_free(&s);
    return 0;
}

static int test_reset_zero_or_negative_leaves_unbuilt(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));

    shuffle_reset(&s, 0);
    CHECK(s.order == NULL);
    CHECK(s.count == 0);

    shuffle_reset(&s, -3);
    CHECK(s.order == NULL);

    /* 未構築のときはnext/prevが常に-1。 */
    CHECK(shuffle_next(&s, 0) == -1);
    CHECK(shuffle_next(&s, 1) == -1);
    CHECK(shuffle_prev(&s, 0) == -1);
    CHECK(shuffle_prev(&s, 1) == -1);

    shuffle_free(&s); /* 未構築でも安全に呼べること */
    shuffle_free(&s); /* 2回呼んでも安全(free(NULL)相当) */
    return 0;
}

/* wrap=0でのnextは、固定された並びをposが尽きるまでたどり、
 * 端で-1を返してposをそこに留める(=それ以上進まない)こと。 */
static int test_next_traverses_all_without_wrap(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));
    shuffle_reset(&s, 5);

    int order_copy[5];
    memcpy(order_copy, s.order, sizeof(order_copy));

    for (int i = 1; i < 5; i++) {
        int v = shuffle_next(&s, 0);
        CHECK(v == order_copy[i]);
        CHECK(s.pos == i);
    }
    /* 末尾に達した後、wrap=0なら-1でposは動かない。 */
    CHECK(shuffle_next(&s, 0) == -1);
    CHECK(s.pos == 4);

    shuffle_free(&s);
    return 0;
}

/* wrap!=0でのnextは末尾から先頭(pos=0)へ回り込み、
 * 新しい並び(依然として有効な順列)になること。 */
static int test_next_wraps_and_reshuffles(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));
    shuffle_reset(&s, 4);

    for (int i = 1; i < 4; i++) {
        CHECK(shuffle_next(&s, 0) >= 0);
    }
    CHECK(s.pos == 3);

    int v = shuffle_next(&s, 1);
    CHECK(s.pos == 0);
    CHECK(v == s.order[0]);
    if (check_is_permutation(s.order, 4)) return 1;

    shuffle_free(&s);
    return 0;
}

/* prevはnextをちょうど巻き戻す(同じ並びの中でposを戻すだけ)こと。 */
static int test_prev_mirrors_next(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));
    shuffle_reset(&s, 4);

    int order_copy[4];
    memcpy(order_copy, s.order, sizeof(order_copy));

    CHECK(shuffle_next(&s, 0) == order_copy[1]);
    CHECK(shuffle_next(&s, 0) == order_copy[2]);
    CHECK(s.pos == 2);

    CHECK(shuffle_prev(&s, 0) == order_copy[1]);
    CHECK(shuffle_prev(&s, 0) == order_copy[0]);
    CHECK(s.pos == 0);

    /* 先頭でwrap=0ならこれ以上戻れない。 */
    CHECK(shuffle_prev(&s, 0) == -1);
    CHECK(s.pos == 0);

    shuffle_free(&s);
    return 0;
}

/* prevのwrapはnextと違い、reshuffleしない(直前まで見えていた並びを
 * そのまま使う)こと。 */
static int test_prev_wraps_without_reshuffle(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));
    shuffle_reset(&s, 3);

    int order_copy[3];
    memcpy(order_copy, s.order, sizeof(order_copy));

    int v = shuffle_prev(&s, 1);
    CHECK(s.pos == 2);
    CHECK(v == order_copy[2]);
    /* 並び自体は変わっていないこと。 */
    CHECK(memcmp(s.order, order_copy, sizeof(order_copy)) == 0);

    shuffle_free(&s);
    return 0;
}

static int test_sync_moves_pos_to_value(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));
    shuffle_reset(&s, 5);

    int target_value = s.order[3];
    s.pos = 0; /* 明示的にリセットしてから合わせる */
    shuffle_sync(&s, target_value);
    CHECK(s.pos == 3);

    /* order[]に無い値を渡しても何もしない(posは動かない)。 */
    shuffle_sync(&s, 999);
    CHECK(s.pos == 3);

    shuffle_free(&s);
    return 0;
}

static int test_sync_on_unbuilt_is_noop(void) {
    shuffle_t s;
    memset(&s, 0, sizeof(s));
    s.pos = 42; /* 未構築でも触らないことを確認するため、わざと変な値を入れる */

    shuffle_sync(&s, 0);
    CHECK(s.pos == 42);
    CHECK(s.order == NULL);
    return 0;
}

int main(void) {
    if (test_reset_is_permutation()) return 1;
    if (test_reset_zero_or_negative_leaves_unbuilt()) return 1;
    if (test_next_traverses_all_without_wrap()) return 1;
    if (test_next_wraps_and_reshuffles()) return 1;
    if (test_prev_mirrors_next()) return 1;
    if (test_prev_wraps_without_reshuffle()) return 1;
    if (test_sync_moves_pos_to_value()) return 1;
    if (test_sync_on_unbuilt_is_noop()) return 1;

    printf("test_shuffle: すべて成功\n");
    return 0;
}

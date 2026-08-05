/* test_scope.c - audio_snapshot_scope() のリング折り返し計算の単体テスト。
 * (P8, F-14)
 *
 * 波形リングは「次に書く位置(scope_pos)」を境に、そこから右が古く、
 * 左が新しい。これを古い順の素直な配列へ並べ直す添字計算は off-by-one を
 * 作りやすく、しかも間違えても画面には「たまに波形が横に飛ぶ」程度にしか
 * 出ないため目視では気づきにくい。ここで固定しておく。
 *
 * SDL_Init は一度も呼ばない(tests/test_ui_metrics.c と同じ方針)。
 * audio_snapshot_scope() は内部で SDL_LockAudioDevice(0) を呼ぶが、
 * オーディオサブシステム未初期化なら SDL 側が何もせず戻る。
 */
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "test_util.h"

/* リングに 0,1,2,...,255 を「古い順に」書き込んだ状態を作る。
 * scope_pos は次に書く位置なので、書き終えた直後の位置になる。 */
static void fill_ring(mugbs_audio_t *a, int write_count) {
    memset(a, 0, sizeof(*a));
    int pos = 0;
    for (int i = 0; i < write_count; i++) {
        a->scope[pos] = (short)i;
        if (++pos >= AUDIO_SCOPE_SAMPLES) pos = 0;
    }
    a->scope_pos = pos;
}

/* ちょうど1周ぶん書いた状態(折り返し無し、scope_pos==0)。 */
static int test_exact_wrap(void) {
    mugbs_audio_t a;
    fill_ring(&a, AUDIO_SCOPE_SAMPLES);
    CHECK(a.scope_pos == 0);

    short out[AUDIO_SCOPE_SAMPLES];
    audio_snapshot_scope(&a, out, AUDIO_SCOPE_SAMPLES);
    for (int i = 0; i < AUDIO_SCOPE_SAMPLES; i++) {
        CHECK(out[i] == (short)i);
    }
    return 0;
}

/* 1周と少しだけ書いた状態(scope_pos が途中を指し、折り返しが起きる)。
 * 最後に書いた値が out の末尾に来ていなければならない。 */
static int test_offset_wrap(void) {
    mugbs_audio_t a;
    int n = AUDIO_SCOPE_SAMPLES + 37;
    fill_ring(&a, n);
    CHECK(a.scope_pos == 37);

    short out[AUDIO_SCOPE_SAMPLES];
    audio_snapshot_scope(&a, out, AUDIO_SCOPE_SAMPLES);
    /* 直近 AUDIO_SCOPE_SAMPLES 個は値 n-256 .. n-1 のはず。 */
    for (int i = 0; i < AUDIO_SCOPE_SAMPLES; i++) {
        CHECK(out[i] == (short)(n - AUDIO_SCOPE_SAMPLES + i));
    }
    return 0;
}

/* リング全体より少ない点数を要求した場合、「直近 n 点」が返る
 * (app.c のトリガ処理は全長を取るが、契約として固定しておく)。 */
static int test_partial_request(void) {
    mugbs_audio_t a;
    int n = AUDIO_SCOPE_SAMPLES + 37;
    fill_ring(&a, n);

    const int want = 64;
    short out[AUDIO_SCOPE_SAMPLES];
    audio_snapshot_scope(&a, out, want);
    for (int i = 0; i < want; i++) {
        CHECK(out[i] == (short)(n - want + i));
    }
    return 0;
}

/* リングより多く要求されたら、超過分は0で埋めて破綻しない。 */
static int test_oversized_request(void) {
    mugbs_audio_t a;
    fill_ring(&a, AUDIO_SCOPE_SAMPLES);

    const int want = AUDIO_SCOPE_SAMPLES + 8;
    short out[AUDIO_SCOPE_SAMPLES + 8];
    for (int i = 0; i < want; i++) out[i] = 0x7FFF; /* 埋められることを見るための番人 */
    audio_snapshot_scope(&a, out, want);

    for (int i = 0; i < AUDIO_SCOPE_SAMPLES; i++) {
        CHECK(out[i] == (short)i);
    }
    for (int i = AUDIO_SCOPE_SAMPLES; i < want; i++) {
        CHECK(out[i] == 0);
    }
    return 0;
}

int main(void) {
    if (test_exact_wrap()) return 1;
    if (test_offset_wrap()) return 1;
    if (test_partial_request()) return 1;
    if (test_oversized_request()) return 1;

    printf("test_scope: すべて成功\n");
    return 0;
}

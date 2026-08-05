/* test_eq.c - eq.c の変換表の単体テスト。 (P8, F-20)
 *
 * ここで固定したいのは「向き」と「基準点」。特に eq_bass は libgme 側の
 * 意味論が「値が大きいほど低音が減る」で直感と逆なので、符号の反転を
 * 落とすと Settings 画面で + を押すほど低音が痩せるという、耳で聞くまで
 * 気づきにくい不具合になる。
 *
 * SDL にも libgme にもリンクしない(eq.c は純 libc)。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "eq.h"
#include "test_util.h"

#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

/* ノブ 0 は「EQ を触っていない状態」= Gbs_Emu の既定
 * (vendor/game-music-emu/gme/Gbs_Emu.cpp の make_equalizer(-1.0, 120))
 * に一致していなければならない。ここがずれると、config.ini を作った
 * だけで音が変わる。 */
static int test_neutral_matches_gbs_default(void) {
    CHECK(NEAR(eq_treble_db(0), -1.0));
    CHECK(NEAR(eq_bass_freq(0), 120.0));
    return 0;
}

static int test_endpoints(void) {
    CHECK(NEAR(eq_treble_db(-100), -47.0));
    CHECK(NEAR(eq_treble_db(100), 5.0));
    CHECK(NEAR(eq_bass_freq(-100), 2000.0));
    CHECK(NEAR(eq_bass_freq(100), 15.0));
    return 0;
}

/* config.c は -100..100 にクランプして読むが、eq.c 自身も範囲外の値で
 * 破綻しないこと(将来 config を経由しない呼び出しが増えても安全なように)。 */
static int test_clamping(void) {
    CHECK(NEAR(eq_treble_db(-1000), eq_treble_db(-100)));
    CHECK(NEAR(eq_treble_db(1000), eq_treble_db(100)));
    CHECK(NEAR(eq_bass_freq(-1000), eq_bass_freq(-100)));
    CHECK(NEAR(eq_bass_freq(1000), eq_bass_freq(100)));
    return 0;
}

/* treble は「ノブを上げるほど dB が上がる」、
 * bass は「ノブを上げるほど周波数が下がる(=低音が増える)」。
 * 向きが逆転していないことを全域で確認する。 */
static int test_monotonic_directions(void) {
    for (int k = -100; k < 100; k++) {
        CHECK(eq_treble_db(k) < eq_treble_db(k + 1));
        CHECK(eq_bass_freq(k) > eq_bass_freq(k + 1));
    }
    return 0;
}

/* bass は周波数なので等比補間している。中点(ノブ±50)が算術平均ではなく
 * 幾何平均になっていることで、線形補間へ戻してしまう変更を検出する。 */
static int test_bass_is_geometric(void) {
    double mid_plus = eq_bass_freq(50);
    CHECK(NEAR(mid_plus, sqrt(120.0 * 15.0)));
    double mid_minus = eq_bass_freq(-50);
    CHECK(NEAR(mid_minus, sqrt(120.0 * 2000.0)));
    return 0;
}

/* treble は 0 を境に傾きが変わる区分線形。負側と正側それぞれの中点を見る。 */
static int test_treble_is_piecewise_linear(void) {
    CHECK(NEAR(eq_treble_db(-50), (-1.0 + -47.0) / 2.0));
    CHECK(NEAR(eq_treble_db(50), (-1.0 + 5.0) / 2.0));
    return 0;
}

int main(void) {
    if (test_neutral_matches_gbs_default()) return 1;
    if (test_endpoints()) return 1;
    if (test_clamping()) return 1;
    if (test_monotonic_directions()) return 1;
    if (test_bass_is_geometric()) return 1;
    if (test_treble_is_piecewise_linear()) return 1;

    printf("test_eq: すべて成功\n");
    return 0;
}

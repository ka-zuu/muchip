#include "eq.h"

#include <math.h>

/* 基準点。すべて vendor/game-music-emu/gme/Gbs_Emu.cpp と gme/gme.h の
 * コメントから取っている。 */
#define TREBLE_MIN   (-47.0) /* Gbs_Emu::handheld_eq の treble */
#define TREBLE_MID    (-1.0) /* Gbs_Emu の既定 */
#define TREBLE_MAX     (5.0) /* gme.h: "+5.0 = extra-crisp" */

#define BASS_MIN     (2000.0) /* Gbs_Emu::handheld_eq の bass。低音が最も減る */
#define BASS_MID      (120.0) /* Gbs_Emu の既定 */
#define BASS_MAX       (15.0) /* gme.txt: "15 Hz is normal"。低音が最も出る */

static int clamp_knob(int knob) {
    if (knob < -100) return -100;
    if (knob > 100) return 100;
    return knob;
}

double eq_treble_db(int knob) {
    knob = clamp_knob(knob);
    double t = (double)knob / 100.0;
    /* 0 を境に傾きが変わる区分線形。負側の可動域(46dB)と正側(6dB)が
     * 大きく違うため、単純な一次式では 0 を既定値に固定できない。 */
    if (t < 0.0) {
        return TREBLE_MID + (-t) * (TREBLE_MIN - TREBLE_MID);
    }
    return TREBLE_MID + t * (TREBLE_MAX - TREBLE_MID);
}

double eq_bass_freq(int knob) {
    knob = clamp_knob(knob);
    double t = (double)knob / 100.0;
    /* 周波数は等比で動かさないと、耳で聞いたときのノブの効きが
     * 中央付近に偏る(15Hz->120Hz と 120Hz->2000Hz はどちらも約4オクターブ)。
     * ノブが正(=低音を増やす)方向で周波数が下がる、という向きの反転もここ。 */
    if (t < 0.0) {
        return BASS_MID * pow(BASS_MIN / BASS_MID, -t);
    }
    return BASS_MID * pow(BASS_MAX / BASS_MID, t);
}

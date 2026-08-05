/* eq.h - config.ini の EQ ノブ (-100..100) を libgme の gme_equalizer_t の
 * 値へ写す純関数。 (SPEC 3.3 F-20, P8)
 *
 * SDL にも libgme にも依存しない純 libc なので、config.c と同じく
 * SDL を一切リンクしないテスト(tests/test_eq.c)から直接検証できる。
 *
 * なぜ変換表が要るか:
 *   SPEC 7 の config.ini は eq_bass / eq_treble を「0が中立」の対称な
 *   整数ノブとして例示している(config.c は -100..100 にクランプする)。
 *   一方 libgme の gme_equalizer_t は物理量そのもので、
 *     treble … dB (0=フラット, -50=こもる, +5=きらびやか)
 *     bass  … 低音が落ち始める周波数(Hz)。**値が大きいほど低音が減る**
 *   と単位も向きも違う。特に bass は大小の向きが直感と逆なので、
 *   ここで符号を反転させないと Settings 画面の「+で低音が増える」という
 *   自然な期待と食い違う。
 *
 * 0 を「GBSの既定の音」に合わせている点も重要。Gbs_Emu のコンストラクタは
 * set_equalizer(make_equalizer(-1.0, 120)) を呼ぶ
 * (vendor/game-music-emu/gme/Gbs_Emu.cpp)。config.ini の既定値は 0 なので、
 * 0 -> (-1.0dB, 120Hz) に写しておかないと「EQ を触っていないのに音が変わる」
 * ことになる。
 */
#ifndef MUGBS_EQ_H
#define MUGBS_EQ_H

/* いずれも knob を -100..100 にクランプしてから写す。 */

/* eq_treble -> gme_equalizer_t.treble (dB)。
 *   -100 -> -47.0 (Gbs_Emu::handheld_eq 相当。GBC本体スピーカー的なこもり)
 *      0 -> -1.0  (Gbs_Emu の既定)
 *   +100 -> +5.0  (gme.h が "extra-crisp" とする上限)
 * 0 を挟んで傾きが変わる区分線形。 */
double eq_treble_db(int knob);

/* eq_bass -> gme_equalizer_t.bass (Hz)。**符号が反転する**ことに注意:
 *   -100 -> 2000.0 (Gbs_Emu::handheld_eq 相当。低音がほぼ無い)
 *      0 ->  120.0 (Gbs_Emu の既定)
 *   +100 ->   15.0 (gme.txt が "normal" とする下限。低音が最も出る)
 * 周波数なので線形ではなく対数(等比)補間する。 */
double eq_bass_freq(int knob);

#endif /* MUGBS_EQ_H */

/* shuffle.h - シャッフル再生の並び順を管理する純ロジック。 (F-25, P10)
 *
 * SDL にも libgme にも依存しない純 libc なので、eq.c/m3u.c と同じく
 * player.c と切り離して単体テストできる(tests/test_shuffle.c)。
 * player.c 側は「今どのエントリを再生しているか」を shuffle_sync() で
 * 伝え、shuffle_next()/shuffle_prev() が返す添字で player_play_entry() を
 * 呼ぶだけの薄いラッパになる。
 */
#ifndef MUGBS_SHUFFLE_H
#define MUGBS_SHUFFLE_H

typedef struct {
    int *order; /* [0, count) の順列。malloc'd。NULL="未構築" */
    int count;
    int pos;    /* order[] の中で「現在」を指す添字 */
} shuffle_t;

/* count件のエントリぶんの新しい並びをランダムに作る(Fisher-Yates)。
 * 既存の order があれば解放してから作り直す。pos は 0 になる。
 * count<=0 なら order を解放してNULLのままにする(呼び出し側の
 * shuffle_next/prevは常に-1を返すようになる)。
 * s は事前に(ゼロ初期化などで)有効な状態になっていること。 */
void shuffle_reset(shuffle_t *s, int count);

/* order を解放して未構築の状態に戻す。s={0}相当にも安全に呼べる。 */
void shuffle_free(shuffle_t *s);

/* order[] の中から value と等しい要素を探し、pos をそこへ合わせる。
 * player_play_entry() のような「シャッフル経由でない任意ジャンプ」の直後に
 * 呼び、次のnext/prevがそこを起点に進むようにする。
 * s->order が NULL、または value が見つからない場合は何もしない
 * (呼び出し側がcount不一致のまま呼んだ場合の防御。通常は起きない)。 */
void shuffle_sync(shuffle_t *s, int value);

/* 次/前のエントリ番号を返す。s->order が NULL(未構築)なら常に -1。
 * wrap!=0 (repeat_mode==REPEAT_ALL相当): 末尾/先頭を超えたら反対側へ
 *   回り込む。next の回り込みだけ新しい並びに reshuffle する(同じ周回を
 *   繰り返さないため)。prev の回り込みは reshuffle しない(直前まで見えて
 *   いた並びを壊さない方が「前へ」の直感に合う)。
 * wrap==0: 末尾/先頭で止まり -1 を返す(posは動かさない)。 */
int shuffle_next(shuffle_t *s, int wrap);
int shuffle_prev(shuffle_t *s, int wrap);

#endif /* MUGBS_SHUFFLE_H */

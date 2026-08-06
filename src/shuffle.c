#include "shuffle.h"

#include <stdlib.h>

void shuffle_free(shuffle_t *s) {
    free(s->order);
    s->order = NULL;
    s->count = 0;
    s->pos = 0;
}

void shuffle_reset(shuffle_t *s, int count) {
    shuffle_free(s);
    if (count <= 0) return;

    s->order = malloc(sizeof(int) * (size_t)count);
    for (int i = 0; i < count; i++) s->order[i] = i;

    /* Fisher-Yates: 末尾から前へ、まだ確定していない範囲 [0, i] から
     * 一様ランダムに選んで入れ替える。 */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = s->order[i];
        s->order[i] = s->order[j];
        s->order[j] = tmp;
    }
    s->count = count;
    s->pos = 0;
}

void shuffle_sync(shuffle_t *s, int value) {
    if (!s->order) return;
    for (int i = 0; i < s->count; i++) {
        if (s->order[i] == value) {
            s->pos = i;
            return;
        }
    }
}

int shuffle_next(shuffle_t *s, int wrap) {
    if (!s->order) return -1;
    if (s->pos + 1 < s->count) {
        s->pos++;
    } else if (wrap) {
        /* 次の周回のために並び直す(同じ順番の繰り返しを避ける)。
         * countは変わらないのでshuffle_resetを再利用できる。posは0に戻る。 */
        shuffle_reset(s, s->count);
    } else {
        return -1;
    }
    return s->order[s->pos];
}

int shuffle_prev(shuffle_t *s, int wrap) {
    if (!s->order) return -1;
    if (s->pos > 0) {
        s->pos--;
    } else if (wrap) {
        s->pos = s->count - 1;
    } else {
        return -1;
    }
    return s->order[s->pos];
}

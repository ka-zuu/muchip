#include "favorites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static int has_ctl(const char *s) {
    return strchr(s, '\t') || strchr(s, '\n') || strchr(s, '\r');
}

static void item_free(favorite_t *it) {
    free(it->container);
    free(it->source_key);
    free(it->title);
}

void favorites_resolve_path(char *out, unsigned long out_size, const char *config_path) {
    const char *env = getenv("MUCHIP_FAVORITES");
    if (env && env[0]) {
        snprintf(out, out_size, "%s", env);
        return;
    }
    const char *slash = config_path ? strrchr(config_path, '/') : NULL;
    if (!slash) {
        snprintf(out, out_size, "favorites.txt");
        return;
    }
    snprintf(out, out_size, "%.*s/favorites.txt", (int)(slash - config_path), config_path);
}

void favorites_free(favorites_t *fav) {
    if (!fav) return;
    for (int i = 0; i < fav->count; i++) item_free(&fav->items[i]);
    free(fav->items);
    fav->items = NULL;
    fav->count = 0;
}

/* 所有権はitemsへ移る(失敗時は何も追加せず、引数の文字列も解放しない) */
static int append(favorites_t *fav, char *container, char *source_key, int track, char *title) {
    favorite_t *grown = realloc(fav->items, sizeof(*grown) * (size_t)(fav->count + 1));
    if (!grown) return -1;
    fav->items = grown;
    favorite_t *it = &fav->items[fav->count++];
    it->container = container;
    it->source_key = source_key;
    it->track_index = track;
    it->title = title;
    return 0;
}

int favorites_load(favorites_t *fav, const char *path) {
    favorites_free(fav);

    FILE *f = fopen(path, "rb");
    if (!f) return 0; /* 初回起動など。空リスト */

    char line[4096 + 1024];
    int lineno = 0;
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] != '\n' && !feof(f)) {
            /* 長すぎる行。残りを読み捨てて行全体を無効にする */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            LOG_WARN("favorites: %d行目が長すぎるため読み飛ばします", lineno);
            continue;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n == 0) continue;

        char *f1 = line;
        char *f2 = strchr(f1, '\t');
        char *f3 = f2 ? strchr(f2 + 1, '\t') : NULL;
        if (!f3) {
            LOG_WARN("favorites: %d行目の形式が不正です。読み飛ばします", lineno);
            continue;
        }
        *f2++ = 0;
        *f3++ = 0;
        char *f4 = strchr(f3, '\t');
        if (f4) *f4++ = 0; /* titleは無くてもよい */

        char *end = NULL;
        long track = strtol(f3, &end, 10);
        if (end == f3 || *end != 0 || track < 0 || track > 100000 || !f1[0] || !f2[0]) {
            LOG_WARN("favorites: %d行目の値が不正です。読み飛ばします", lineno);
            continue;
        }
        if (f4 && strchr(f4, '\t')) {
            LOG_WARN("favorites: %d行目のフィールド数が不正です。読み飛ばします", lineno);
            continue;
        }
        if (favorites_find(fav, f1, f2, (int)track) >= 0) continue; /* 重複 */

        char *c = dup_str(f1), *k = dup_str(f2), *t = dup_str(f4 ? f4 : "");
        if (!c || !k || !t || append(fav, c, k, (int)track, t) != 0) {
            free(c);
            free(k);
            free(t);
            LOG_ERR("favorites: メモリ確保に失敗しました");
            rc = -1;
            break;
        }
    }
    fclose(f);
    if (rc != 0) favorites_free(fav);
    return rc;
}

int favorites_save(const favorites_t *fav, const char *path) {
    char tmp[4096 + 8];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        LOG_ERR("favorites: パスが長すぎます: %s", path);
        return -1;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        LOG_ERR("favorites: %s を書き込み用に開けません", tmp);
        return -1;
    }
    for (int i = 0; i < fav->count; i++) {
        const favorite_t *it = &fav->items[i];
        fprintf(f, "%s\t%s\t%d\t%s\n", it->container, it->source_key, it->track_index, it->title);
    }
    int bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    if (bad || rename(tmp, path) != 0) {
        LOG_ERR("favorites: %s の保存に失敗しました", path);
        remove(tmp);
        return -1;
    }
    return 0;
}

int favorites_find(const favorites_t *fav, const char *container,
                   const char *source_key, int track_index) {
    for (int i = 0; i < fav->count; i++) {
        const favorite_t *it = &fav->items[i];
        if (it->track_index == track_index && strcmp(it->container, container) == 0 &&
            strcmp(it->source_key, source_key) == 0) {
            return i;
        }
    }
    return -1;
}

int favorites_toggle(favorites_t *fav, const char *container,
                     const char *source_key, int track_index, const char *title) {
    if (!container || !source_key || !container[0] || !source_key[0] ||
        has_ctl(container) || has_ctl(source_key) || track_index < 0) {
        return -1;
    }

    int idx = favorites_find(fav, container, source_key, track_index);
    if (idx >= 0) {
        item_free(&fav->items[idx]);
        memmove(&fav->items[idx], &fav->items[idx + 1],
                sizeof(fav->items[0]) * (size_t)(fav->count - idx - 1));
        fav->count--;
        return 0;
    }

    char *c = dup_str(container), *k = dup_str(source_key), *t = dup_str(title ? title : "");
    if (!c || !k || !t || append(fav, c, k, track_index, t) != 0) {
        free(c);
        free(k);
        free(t);
        return -1;
    }
    for (char *p = t; *p; p++) {
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    }
    return 1;
}

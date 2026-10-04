#include "playctx.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "log.h"
#include "util.h"

/* browser.c の has_music_ext() と同じ拡張子集合(共有ヘッダにするほどの
 * 重複ではないため、意図的に別々に持つ。増減したら両方直すこと)。 */
static int has_music_ext(const char *name) {
    return ends_with_ci(name, ".gbs") || ends_with_ci(name, ".gb") ||
           ends_with_ci(name, ".nsf") || ends_with_ci(name, ".nsfe") ||
           ends_with_ci(name, ".spc") ||
           ends_with_ci(name, ".vgm") || ends_with_ci(name, ".vgz") ||
           ends_with_ci(name, ".hes") || ends_with_ci(name, ".kss") ||
           ends_with_ci(name, ".ay") || ends_with_ci(name, ".sap") ||
           ends_with_ci(name, ".gym") ||
           ends_with_ci(name, ".m3u") || ends_with_ci(name, ".zip");
}

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

static int name_cmp(const void *pa, const void *pb) {
    const char *const *a = (const char *const *)pa;
    const char *const *b = (const char *const *)pb;
    return strcasecmp(*a, *b);
}

static void free_names(char **names, int count) {
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

/* browser.c の join_path() と同じ(dirが"/"ならさらに"/"を足さない)。
 * out_sizeをsizeof()の定数ではなく実引数として受け取ることで、GCCの
 * -Wformat-truncationが「dir+nameの最大長がoutに収まらないかもしれない」
 * という(実際には両方とも同じ4096バイトのバッファなので起きない)誤検知を
 * 出さないようにしている(browser.cのjoin_path()も同じ理由でこの形)。 */
static void join_path(char *out, size_t out_size, const char *dir, const char *name) {
    snprintf(out, out_size, "%s%s%s", dir, (strcmp(dir, "/") == 0 ? "" : "/"), name);
}

void playctx_free(playctx_t *ctx) {
    free_names(ctx->names, ctx->count);
    free(ctx->dir);
    memset(ctx, 0, sizeof(*ctx));
}

int playctx_open_dir(playctx_t *ctx, const char *opened_path, int show_all,
                      int force_rescan) {
    if (!opened_path || !opened_path[0]) return -1;

    char full[4096];
    if (snprintf(full, sizeof(full), "%s", opened_path) >= (int)sizeof(full)) {
        LOG_WARN("パスが長すぎるため再生コンテキストを作れません: %s", opened_path);
        return -1;
    }

    char dir[4096];
    char base[4096];
    const char *slash = strrchr(full, '/');
    if (!slash) {
        snprintf(base, sizeof(base), "%s", full);
        snprintf(dir, sizeof(dir), ".");
    } else {
        snprintf(base, sizeof(base), "%s", slash + 1);
        size_t dn = (size_t)(slash - full);
        if (dn == 0) dn = 1; /* "/foo.gbs" -> "/" */
        memcpy(dir, full, dn);
        dir[dn] = 0;
    }

    if (!force_rescan && ctx->kind == PLAYCTX_DIRECTORY && ctx->dir &&
        strcmp(ctx->dir, dir) == 0) {
        /* 既に同じディレクトリを開いている。再走査せず current だけ
         * 合わせ直す(browser_open_dir()と違い、こちらはselected/scrollを
         * 持たないのでリセットの心配は無い)。 */
        ctx->current = -1;
        for (int i = 0; i < ctx->count; i++) {
            if (strcmp(ctx->names[i], base) == 0) {
                ctx->current = i;
                break;
            }
        }
        return 0;
    }

    DIR *d = opendir(dir);
    if (!d) {
        LOG_WARN("再生コンテキスト用にディレクトリを開けません: %s", dir);
        return -1;
    }

    char **names = NULL;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.') continue;

        char item_full[4096];
        join_path(item_full, sizeof(item_full), dir, name);

        int is_dir;
        if (de->d_type == DT_DIR) {
            is_dir = 1;
        } else if (de->d_type == DT_REG) {
            is_dir = 0;
        } else {
            struct stat st;
            is_dir = (stat(item_full, &st) == 0) && S_ISDIR(st.st_mode);
        }
        if (is_dir) continue; /* ディレクトリは一覧に出さない(browser.cの役目) */
        if (!show_all && !has_music_ext(name)) continue;

        char **grown = realloc(names, sizeof(*grown) * (size_t)(count + 1));
        if (!grown) {
            LOG_ERR("playctx_open_dir: メモリ確保に失敗しました");
            free_names(names, count);
            closedir(d);
            return -1;
        }
        names = grown;
        names[count++] = dup_str(name);
    }
    closedir(d);

    if (count > 0) qsort(names, (size_t)count, sizeof(*names), name_cmp);

    char *new_dir = dup_str(dir);
    free_names(ctx->names, ctx->count);
    free(ctx->dir);
    ctx->kind = PLAYCTX_DIRECTORY;
    ctx->dir = new_dir;
    ctx->names = names;
    ctx->count = count;
    ctx->current = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], base) == 0) {
            ctx->current = i;
            break;
        }
    }
    return 0;
}

int playctx_step_path(const playctx_t *ctx, int delta, char *out, unsigned long out_size) {
    if (ctx->count <= 0 || !ctx->dir) {
        if (out_size > 0) out[0] = 0;
        return -1;
    }

    int n = ctx->count;
    int idx;
    if (ctx->current < 0) {
        /* 一覧に無い状態(zip内から開いた等)からのUP/DOWNは、送りたい方向の
         * 端から始める(browser_move_wrap()には無い、一覧外始点の分岐)。 */
        idx = (delta > 0) ? 0 : n - 1;
    } else {
        idx = ((ctx->current + delta) % n + n) % n;
    }

    join_path(out, (size_t)out_size, ctx->dir, ctx->names[idx]);
    return 0;
}

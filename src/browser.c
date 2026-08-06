#include "browser.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "log.h"
#include "util.h"

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

static int has_music_ext(const char *name) {
    return ends_with_ci(name, ".gbs") || ends_with_ci(name, ".gb") ||
           ends_with_ci(name, ".m3u") || ends_with_ci(name, ".zip");
}

/* ディレクトリ優先 -> 大小文字無視の名前順 (browser.h の契約)。 */
static int item_cmp(const void *pa, const void *pb) {
    const browser_item_t *a = (const browser_item_t *)pa;
    const browser_item_t *b = (const browser_item_t *)pb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

static void free_items(browser_item_t *items, int count) {
    for (int i = 0; i < count; i++) free(items[i].name);
    free(items);
}

/* path とファイル名を結合する。path が "/" ならさらに "/" を足さない。 */
static void join_path(char *out, size_t out_size, const char *dir, const char *name) {
    snprintf(out, out_size, "%s%s%s", dir, (strcmp(dir, "/") == 0 ? "" : "/"), name);
}

int browser_open_dir(browser_t *b, const char *path, int show_all) {
    DIR *dir = opendir(path);
    if (!dir) {
        LOG_WARN("ディレクトリを開けません: %s", path);
        return -1;
    }

    browser_item_t *items = NULL;
    int count = 0;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.') continue; /* "." ".." および隠しファイルを除外 */

        char full[4096];
        join_path(full, sizeof(full), path, name);

        int is_dir;
        if (de->d_type == DT_DIR) {
            is_dir = 1;
        } else if (de->d_type == DT_REG) {
            is_dir = 0;
        } else {
            /* d_type が信頼できないファイルシステム(DT_UNKNOWN等)向けの
             * フォールバック。 */
            struct stat st;
            is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        }

        if (!is_dir && !show_all && !has_music_ext(name)) continue;

        browser_item_t *grown = realloc(items, sizeof(*items) * (size_t)(count + 1));
        if (!grown) {
            LOG_ERR("browser_open_dir: メモリ確保に失敗しました");
            free_items(items, count);
            closedir(dir);
            return -1;
        }
        items = grown;
        items[count].name = dup_str(name);
        items[count].is_dir = is_dir;
        count++;
    }
    closedir(dir);

    /* count==0 なら items は NULL のまま(realloc が一度も呼ばれない)。
     * qsort() は base が非NULLであることを要求するため(nmemb==0でも)、
     * 空ディレクトリでUBSanが未定義動作を報告する。呼ばずに済ませる。 */
    if (count > 0) qsort(items, (size_t)count, sizeof(*items), item_cmp);

    /* ここまで成功して初めて b を更新する。失敗時は呼び出し側が直前の
     * ディレクトリに留まれるようにする (browser.h の契約)。
     * path が b->cwd 自身を指している場合がある(show_all_files切り替え時の
     * 自己呼び出し等)。先に free(b->cwd) すると path がダングリングポインタに
     * なるため、dup してから free する順序を必ず守ること。 */
    char *new_cwd = dup_str(path);
    free_items(b->items, b->count);
    free(b->cwd);
    b->cwd = new_cwd;
    b->items = items;
    b->count = count;
    b->selected = 0;
    b->scroll = 0;
    return 0;
}

void browser_free(browser_t *b) {
    free_items(b->items, b->count);
    free(b->cwd);
    memset(b, 0, sizeof(*b));
}

void browser_move(browser_t *b, int delta) {
    if (b->count == 0) return;
    b->selected += delta;
    if (b->selected < 0) b->selected = 0;
    if (b->selected >= b->count) b->selected = b->count - 1;
}

void browser_move_wrap(browser_t *b, int delta) {
    if (b->count == 0) return;
    /* delta は ±1 想定だが、剰余で任意の値に対しても素直に回るようにする。 */
    int n = b->count;
    int sel = (b->selected + delta) % n;
    if (sel < 0) sel += n;
    b->selected = sel;
}

void browser_page(browser_t *b, int page_size) {
    browser_move(b, page_size);
}

/* path の親ディレクトリを返す(malloc'd)。末尾のスラッシュは無視する。
 * ルート"/"を渡された場合は"/"を返す(呼び出し側で同一性チェックする)。 */
static char *parent_dir(const char *path) {
    size_t len = strlen(path);
    char *tmp = malloc(len + 1);
    memcpy(tmp, path, len + 1);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
        len--;
    }
    if (strcmp(tmp, "/") == 0) return tmp;

    char *slash = strrchr(tmp, '/');
    if (!slash) {
        /* スラッシュを含まない相対パス。カレントディレクトリを親とみなす。 */
        free(tmp);
        return dup_str(".");
    }
    if (slash == tmp) {
        tmp[1] = 0; /* "/foo" -> "/" */
    } else {
        *slash = 0;
    }
    return tmp;
}

int browser_up(browser_t *b, int show_all) {
    if (!b->cwd) return 0;

    char *parent = parent_dir(b->cwd);
    if (strcmp(parent, b->cwd) == 0) {
        /* ルート境界: これ以上は上がれない。 */
        free(parent);
        return 0;
    }

    int rc = browser_open_dir(b, parent, show_all);
    free(parent);
    return rc == 0 ? 1 : 0;
}

int browser_enter(browser_t *b, int show_all) {
    if (b->count == 0 || b->selected < 0 || b->selected >= b->count) return 0;
    if (!b->items[b->selected].is_dir) return 0;

    char path[4096];
    join_path(path, sizeof(path), b->cwd, b->items[b->selected].name);
    return browser_open_dir(b, path, show_all) == 0 ? 1 : 0;
}

int browser_path_at(const browser_t *b, int index, char *out, unsigned long out_size) {
    if (index < 0 || index >= b->count) {
        if (out_size > 0) out[0] = 0;
        return -1;
    }
    join_path(out, (size_t)out_size, b->cwd, b->items[index].name);
    return 0;
}

int browser_selected_path(const browser_t *b, char *out, unsigned long out_size) {
    return browser_path_at(b, b->selected, out, out_size);
}

int browser_select_by_name(browser_t *b, const char *name) {
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->items[i].name, name) == 0) {
            b->selected = i;
            return 1;
        }
    }
    return 0;
}

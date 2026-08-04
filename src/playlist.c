#include "playlist.h"

#include <ctype.h>
#include <gme/gme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"
#include "m3u.h"

/* ---- 小さな文字列/パスヘルパ ------------------------------------------ */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

static char *dup_len(const char *s, size_t n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

static char *join_dir(const char *dir, const char *name) {
    size_t dn = strlen(dir), nn = strlen(name);
    char *out = malloc(dn + 1 + nn + 1);
    memcpy(out, dir, dn);
    out[dn] = '/';
    memcpy(out + dn + 1, name, nn + 1);
    return out;
}

static char *dirname_dup(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return dup_str(".");
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1; /* "/foo" のようにルート直下の場合 */
    return dup_len(path, n);
}

static char *basename_no_ext_dup(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    return dup_len(base, n);
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    if (xl > sl) return 0;
    for (size_t i = 0; i < xl; i++) {
        if (tolower((unsigned char)s[sl - xl + i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

/* Windows由来のm3uはパス区切りに '\' を使うことがあるため正規化する。
 * 絶対パスならそのまま、相対パスなら base_dir と結合する。 */
static char *resolve_relative(const char *base_dir, const char *ref) {
    char *norm = dup_str(ref);
    for (char *p = norm; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    if (norm[0] == '/') return norm;
    char *joined = join_dir(base_dir, norm);
    free(norm);
    return joined;
}

static int read_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    *out_buf = buf;
    *out_len = rd;
    return 0;
}

/* ---- playlist_t 構築 --------------------------------------------------- */

static int pl_add_source(playlist_t *pl, char *display_path, char *fs_path,
                          char *m3u_text, size_t m3u_len) {
    pl->sources = realloc(pl->sources, sizeof(*pl->sources) * (size_t)(pl->source_count + 1));
    playlist_source_t *s = &pl->sources[pl->source_count];
    s->display_path = display_path;
    s->fs_path = fs_path;
    s->zip_entry = NULL;
    s->m3u_text = m3u_text;
    s->m3u_len = m3u_len;
    return pl->source_count++;
}

/* source_index の指すファイルを実際に開き、gme_track_count() 分だけ
 * gme_track_info() を回して entries[] に追記する。曲長等の重い判定は
 * player.c 側(再生開始時)に任せ、ここではタイトルの収集に徹する。 */
static int pl_scan_source(playlist_t *pl, int source_index, const mugbs_config_t *cfg) {
    playlist_source_t *src = &pl->sources[source_index];

    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(src->fs_path, &emu, cfg->sample_rate);
    if (err) {
        LOG_WARN("開けませんでした: %s: %s", src->fs_path, err);
        return -1;
    }

    if (src->m3u_text) {
        err = gme_load_m3u_data(emu, src->m3u_text, (long)src->m3u_len);
        if (err) {
            LOG_WARN("m3uの読み込みに失敗しました(%s): %s。生トラックをそのまま列挙します",
                      src->display_path, err);
        }
    }

    int count = gme_track_count(emu);
    for (int i = 0; i < count; i++) {
        gme_info_t *info = NULL;
        char title_buf[64];
        const char *title = NULL;

        if (!gme_track_info(emu, &info, i) && info->song[0]) {
            title = info->song;
        }
        if (!title) {
            snprintf(title_buf, sizeof(title_buf), "Track %02d", i + 1);
            title = title_buf;
        }

        pl->entries = realloc(pl->entries, sizeof(*pl->entries) * (size_t)(pl->entry_count + 1));
        playlist_entry_t *e = &pl->entries[pl->entry_count];
        e->title = dup_str(title);
        e->source_index = source_index;
        e->track_index = i;
        pl->entry_count++;

        if ((!pl->game || !pl->game[0]) && info && info->game[0]) {
            free(pl->game);
            pl->game = dup_str(info->game);
        }

        if (info) gme_free_info(info); /* gme_track_info はヒープを返すので必ず解放する */
    }

    gme_delete(emu);
    return 0;
}

/* m3uテキストをセグメント分割し、セグメントごとにソースを追加・スキャンする。
 * 参照ファイルが1種類のみなら、セグメントは自動的に1つになる
 * (SPEC 5.2-2 の「推奨パス」に一致。特別扱いの分岐は不要)。 */
static int build_from_m3u_text(playlist_t *pl, const char *text, size_t len,
                                const char *base_dir, const mugbs_config_t *cfg) {
    m3u_segment_t *segs = NULL;
    int seg_count = 0;
    if (m3u_split_segments(text, len, &segs, &seg_count) != 0) {
        LOG_ERR("m3uの解析に失敗しました(有効なエントリがありません)");
        return -1;
    }

    int ok_any = 0;
    for (int i = 0; i < seg_count; i++) {
        m3u_segment_t *seg = &segs[i];
        char *resolved = resolve_relative(base_dir, seg->filename);

        if (!file_exists(resolved)) {
            /* T-13: 存在しないファイルを参照するエントリはスキップして警告する。 */
            LOG_WARN("m3uが参照するファイルが見つかりません。スキップします: %s", resolved);
            free(resolved);
            continue;
        }

        char *m3u_copy = dup_len(seg->text, seg->text_len);
        int source_index = pl_add_source(pl, dup_str(resolved), resolved, m3u_copy, seg->text_len);
        if (pl_scan_source(pl, source_index, cfg) == 0) {
            ok_any = 1;
        }
    }

    m3u_free_segments(segs, seg_count);
    return ok_any ? 0 : -1;
}

static int playlist_open_m3u(playlist_t *pl, const char *path, const mugbs_config_t *cfg) {
    if (!file_exists(path)) {
        LOG_ERR("ファイルが見つかりません: %s", path);
        return -1;
    }

    char *text = NULL;
    size_t len = 0;
    if (read_file(path, &text, &len) != 0) {
        LOG_ERR("m3uの読み込みに失敗しました: %s", path);
        return -1;
    }

    char *dir = dirname_dup(path);
    int rc = build_from_m3u_text(pl, text, len, dir, cfg);
    free(dir);
    free(text);
    return rc;
}

static int playlist_open_gbs(playlist_t *pl, const char *path, const mugbs_config_t *cfg) {
    if (!file_exists(path)) {
        LOG_ERR("ファイルが見つかりません: %s", path);
        return -1;
    }

    char *dir = dirname_dup(path);
    char *base = basename_no_ext_dup(path);
    char *m3u_name = malloc(strlen(base) + 5); /* ".m3u" + NUL */
    sprintf(m3u_name, "%s.m3u", base);
    char *sidecar = join_dir(dir, m3u_name);
    free(m3u_name);
    free(base);

    int rc = -2; /* -2: 単体ファイルとして開く(フォールバック)の印 */
    if (file_exists(sidecar)) {
        LOG_INFO("同名のm3uを検出しました: %s", sidecar);
        char *text = NULL;
        size_t len = 0;
        if (read_file(sidecar, &text, &len) == 0) {
            rc = build_from_m3u_text(pl, text, len, dir, cfg);
            free(text);
        } else {
            LOG_WARN("m3uの読み込みに失敗しました: %s。m3u無しで続行します", sidecar);
        }
    }
    free(sidecar);
    free(dir);

    if (rc == -2) {
        /* m3u無し: 単体ファイルとして開き、gme_track_count() で全トラックを
         * 自動命名して列挙する (SPEC 5.2-3)。 */
        int source_index = pl_add_source(pl, dup_str(path), dup_str(path), NULL, 0);
        rc = pl_scan_source(pl, source_index, cfg);
    }

    return rc;
}

int playlist_open(const char *path, const mugbs_config_t *config, playlist_t **out) {
    playlist_t *pl = calloc(1, sizeof(*pl));
    pl->game = dup_str("");

    int rc;
    if (ends_with_ci(path, ".m3u")) {
        rc = playlist_open_m3u(pl, path, config);
    } else {
        rc = playlist_open_gbs(pl, path, config);
    }

    if (rc != 0 || pl->entry_count == 0) {
        if (pl->entry_count == 0) {
            LOG_ERR("再生可能なトラックがありません: %s", path);
        }
        playlist_free(pl);
        return -1;
    }

    *out = pl;
    return 0;
}

void playlist_free(playlist_t *pl) {
    if (!pl) return;
    for (int i = 0; i < pl->source_count; i++) {
        free(pl->sources[i].display_path);
        free(pl->sources[i].fs_path);
        free(pl->sources[i].zip_entry);
        free(pl->sources[i].m3u_text);
    }
    free(pl->sources);
    for (int i = 0; i < pl->entry_count; i++) {
        free(pl->entries[i].title);
    }
    free(pl->entries);
    free(pl->game);
    free(pl);
}

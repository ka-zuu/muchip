#include "archive.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "miniz.h" /* miniz_common/tdef/tinfl/zip をこの順で正しくincludeしてくれる */

#define MAX_EXTRACT_SIZE (32u * 1024 * 1024) /* 32MB (SPEC 5.3 メモリ保護) */

struct archive {
    mz_zip_archive zip;
};

/* libgmeが開ける(かもしれない)拡張子。GBSが主目的だが、SPEC 1.2の通り
 * NSF/SPC/VGM等が「たまたま動く」ことを妨げない。 */
static const char *k_music_exts[] = {
    ".gbs", ".gb", ".nsf", ".nsfe", ".spc", ".vgm", ".vgz",
    ".ay", ".hes", ".kss", ".sap", ".gym", NULL
};

static int ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    if (xl > sl) return 0;
    for (size_t i = 0; i < xl; i++) {
        if (tolower((unsigned char)s[sl - xl + i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

static int is_music_ext(const char *name) {
    for (int i = 0; k_music_exts[i]; i++) {
        if (ends_with_ci(name, k_music_exts[i])) return 1;
    }
    return 0;
}

/* パス区切り(\ と /)を落として basename を取り、小文字化して返す
 * (呼び出し側が free すること)。zip内のパスの揺れ・大小文字の揺れを
 * 吸収して比較するための正規化 (SPEC 5.3)。 */
static char *normalize_basename(const char *name) {
    const char *base = name;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    size_t n = strlen(base);
    char *out = malloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        out[i] = (char)tolower((unsigned char)base[i]);
    }
    out[n] = 0;
    return out;
}

int archive_open(const char *zip_path, archive_t **out) {
    archive_t *ar = calloc(1, sizeof(*ar));
    memset(&ar->zip, 0, sizeof(ar->zip));

    if (!mz_zip_reader_init_file(&ar->zip, zip_path, 0)) {
        LOG_ERR("zipを開けませんでした: %s", zip_path);
        free(ar);
        return -1;
    }

    *out = ar;
    return 0;
}

void archive_close(archive_t *ar) {
    if (!ar) return;
    mz_zip_reader_end(&ar->zip);
    free(ar);
}

int archive_list(archive_t *ar, archive_entry_t **out_entries, int *out_count) {
    mz_uint n = mz_zip_reader_get_num_files(&ar->zip);
    archive_entry_t *entries = calloc(n, sizeof(*entries));
    int count = 0;

    for (mz_uint i = 0; i < n; i++) {
        if (mz_zip_reader_is_file_a_directory(&ar->zip, i)) continue;

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&ar->zip, i, &st)) continue;

        int music = is_music_ext(st.m_filename);
        int m3u = ends_with_ci(st.m_filename, ".m3u");
        if (!music && !m3u) continue; /* 対象外の拡張子は列挙しない */

        entries[count].name = strdup(st.m_filename);
        entries[count].index = (int)i;
        entries[count].is_music = music;
        entries[count].is_m3u = m3u;
        entries[count].uncompressed_size = (unsigned long long)st.m_uncomp_size;
        count++;
    }

    *out_entries = entries;
    *out_count = count;
    return 0;
}

void archive_free_entries(archive_entry_t *entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

int archive_find(archive_t *ar, const char *name) {
    char *want = normalize_basename(name);

    mz_uint n = mz_zip_reader_get_num_files(&ar->zip);
    int found = -1;
    for (mz_uint i = 0; i < n; i++) {
        if (mz_zip_reader_is_file_a_directory(&ar->zip, i)) continue;

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&ar->zip, i, &st)) continue;

        char *have = normalize_basename(st.m_filename);
        int match = (strcmp(have, want) == 0);
        free(have);
        if (match) {
            found = (int)i;
            break;
        }
    }

    free(want);
    return found;
}

int archive_extract(archive_t *ar, int index, void **out_data, size_t *out_size) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&ar->zip, (mz_uint)index, &st)) {
        LOG_ERR("archive_extract: mz_zip_reader_file_stat(%d)に失敗しました", index);
        return -1;
    }

    if (st.m_uncomp_size > MAX_EXTRACT_SIZE) {
        LOG_ERR("展開後サイズが上限(32MB)を超えるため拒否します: %s (%llu bytes)",
                 st.m_filename, (unsigned long long)st.m_uncomp_size);
        return -1;
    }

    size_t size = 0;
    void *data = mz_zip_reader_extract_to_heap(&ar->zip, (mz_uint)index, &size, 0);
    if (!data) {
        LOG_ERR("展開に失敗しました: %s", st.m_filename);
        return -1;
    }

    *out_data = data;
    *out_size = size;
    return 0;
}

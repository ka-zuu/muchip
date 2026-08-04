#include "m3u.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* 可変長バッファ */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void sb_reserve(strbuf_t *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t newcap = b->cap ? b->cap * 2 : 256;
    while (newcap < b->len + extra + 1) newcap *= 2;
    b->data = realloc(b->data, newcap);
    b->cap = newcap;
}

static void sb_append(strbuf_t *b, const char *s, size_t n) {
    sb_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void sb_append_line(strbuf_t *b, const char *line, size_t n) {
    sb_append(b, line, n);
    sb_append(b, "\n", 1);
}

static void sb_free(strbuf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static int is_blank(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!isspace((unsigned char)s[i])) return 0;
    }
    return 1;
}

/* libgme の M3u_Playlist::parse_filename() 相当の「終端条件」だけを真似た
 * 簡易版。バックスラッシュエスケープの解釈はしない(セグメント分けの
 * 比較にしか使わないため、多少ずれても実害はない)。
 * 終端条件は次のどちらか:
 *   - "::" が現れる (::TYPE サフィックスの開始)
 *   - "," の後(空白を挟んでもよい) に数字か '$' が続く (トラック番号欄の開始) */
static char *extract_filename(const char *line, size_t len) {
    size_t i = 0;
    while (i < len) {
        char c = line[i];
        if (c == ':' && i + 1 < len && line[i + 1] == ':') break;
        if (c == ',') {
            size_t j = i + 1;
            while (j < len && line[j] == ' ') j++;
            if (j < len && (line[j] == '$' || (line[j] >= '0' && line[j] <= '9'))) break;
        }
        i++;
    }

    size_t start = 0;
    while (start < i && isspace((unsigned char)line[start])) start++;
    size_t end = i;
    while (end > start && isspace((unsigned char)line[end - 1])) end--;

    char *name = malloc(end - start + 1);
    memcpy(name, line + start, end - start);
    name[end - start] = 0;
    return name;
}

/* 1行を読み進める。改行文字(\r\n / \r / \n)は読み飛ばす。
 * line_start/line_len に行の中身(改行を含まない)を返す。
 * 戻り値は次の読み取り開始位置。 */
static size_t read_line(const char *text, size_t len, size_t pos,
                         size_t *line_start, size_t *line_len) {
    *line_start = pos;
    while (pos < len && text[pos] != '\n' && text[pos] != '\r') pos++;
    *line_len = pos - *line_start;
    if (pos < len && text[pos] == '\r') pos++;
    if (pos < len && text[pos] == '\n') pos++;
    return pos;
}

int m3u_split_segments(const char *text, size_t len,
                        m3u_segment_t **out_segs, int *out_count) {
    *out_segs = NULL;
    *out_count = 0;

    /* 1パス目: ヘッダコメント行(#始まり)をファイル全体から集める。
     * 2パス目でこの完全な comments を全セグメントに複製するため、
     * 先に全体を舐めておく必要がある(後方のコメントが前方のセグメントに
     * 反映されない、という順序依存を避けるため)。 */
    strbuf_t comments;
    sb_init(&comments);
    {
        size_t pos = 0;
        while (pos < len) {
            size_t ls, ll;
            pos = read_line(text, len, pos, &ls, &ll);
            const char *line = text + ls;
            if (ll > 0 && line[0] == '#') {
                sb_append_line(&comments, line, ll);
            }
        }
    }

    m3u_segment_t *segs = NULL;
    int seg_count = 0, seg_cap = 0;
    strbuf_t cur_body;
    sb_init(&cur_body);
    char *cur_filename = NULL;

    size_t pos = 0;
    while (pos < len) {
        size_t ls, ll;
        pos = read_line(text, len, pos, &ls, &ll);
        const char *line = text + ls;

        if (ll == 0 || is_blank(line, ll) || line[0] == '#') {
            continue; /* 空行・コメント行はエントリではない (gmeパーサと同様) */
        }

        char *fname = extract_filename(line, ll);

        /* ファイル名が空 (「::TYPE,...」で始まる行) は「直前と同じファイル」
         * とみなす。拡張M3Uの一部の実装では2行目以降でファイル名を省略する
         * ことがあるため。 */
        int starts_new_segment = (cur_filename == NULL) ||
                                  (fname[0] != '\0' && strcmp(cur_filename, fname) != 0);

        if (starts_new_segment) {
            if (cur_filename) {
                if (seg_count >= seg_cap) {
                    seg_cap = seg_cap ? seg_cap * 2 : 4;
                    segs = realloc(segs, sizeof(*segs) * (size_t)seg_cap);
                }
                strbuf_t full;
                sb_init(&full);
                sb_append(&full, comments.data ? comments.data : "", comments.len);
                sb_append(&full, cur_body.data ? cur_body.data : "", cur_body.len);
                segs[seg_count].filename = cur_filename;
                segs[seg_count].text = full.data;
                segs[seg_count].text_len = full.len;
                seg_count++;
                sb_free(&cur_body);
                sb_init(&cur_body);
            }
            /* 先頭行でファイル名省略というのは通常あり得ないが、保険として
             * 空文字列そのものをファイル名として扱う(区間は1つのまま)。 */
            cur_filename = fname;
        } else {
            free(fname);
        }

        sb_append_line(&cur_body, line, ll);
    }

    if (cur_filename) {
        if (seg_count >= seg_cap) {
            seg_cap = seg_cap ? seg_cap * 2 : 4;
            segs = realloc(segs, sizeof(*segs) * (size_t)seg_cap);
        }
        strbuf_t full;
        sb_init(&full);
        sb_append(&full, comments.data ? comments.data : "", comments.len);
        sb_append(&full, cur_body.data ? cur_body.data : "", cur_body.len);
        segs[seg_count].filename = cur_filename;
        segs[seg_count].text = full.data;
        segs[seg_count].text_len = full.len;
        seg_count++;
    }

    sb_free(&comments);
    sb_free(&cur_body);

    if (seg_count == 0) {
        free(segs);
        return -1;
    }

    *out_segs = segs;
    *out_count = seg_count;
    return 0;
}

void m3u_free_segments(m3u_segment_t *segs, int count) {
    if (!segs) return;
    for (int i = 0; i < count; i++) {
        free(segs[i].filename);
        free(segs[i].text);
    }
    free(segs);
}

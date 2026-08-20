#include "playlist.h"

#include <ctype.h>
#include <gme/gme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <sys/stat.h>

#include "archive.h"
#include "log.h"
#include "m3u.h"
#include "text.h"
#include "util.h"

/* ---- 小さな文字列/パスヘルパ ------------------------------------------ */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

/* gme_track_info() 由来のメタデータ複製用。Shift_JIS(CP932)で書かれた
 * ヘッダを持つNSFが実在するため(Issue参照)、text_dup_utf8() で
 * UTF-8へ正規化してから複製する。malloc失敗時は("" と同じく)以降の
 * [0] 参照が落ちないよう空文字列にフォールバックする。 */
static char *dup_meta(const char *s) {
    char *r = text_dup_utf8(s);
    return r ? r : dup_str("");
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

/* gme_info_t.play_length は曲長不明時に -1 ではなく 150000(既定150秒)を
 * 返してしまう(docs/design-notes.md「libgmeの使い方と既知の乖離」)ため、
 * これだけでは「本当に不明か」を
 * 判定できない。length(総曲長)/intro_length+loop_length(ループ構造)の
 * 有無で判定する。playlist.c(スキャン時)と player.c(再生開始時)の
 * 双方が同じ判定を必要とするため、ここに一本化する
 * (元は player.c 内の static実装)。 */
int playlist_natural_length_ms(const gme_info_t *info, int *out_known) {
    if (info->length > 0) {
        if (out_known) *out_known = 1;
        return info->length;
    }
    if (info->intro_length > 0 && info->loop_length > 0) {
        if (out_known) *out_known = 1;
        return info->play_length;
    }
    if (out_known) *out_known = 0;
    return 0;
}

/* Issue #24: loop_length>0 だけを見る。playlist_natural_length_ms()の
 * intro>0 && loop>0 は「introが0/未指定でもloopは有効」なm3u表現
 * (`,-` や `,0:30` )を取りこぼすため、ここでは独立して判定する。 */
int playlist_track_loops(const gme_info_t *info) {
    return info->loop_length > 0;
}

/* Issue #19: length_override_sec(非0)が設定されていれば、原則として
 * 全トラックの曲長をそれへ強制する (F-28)。ただし Issue #24: 曲長が既知
 * (known!=0)なのに鳴り続けられない(loops==0)トラックは、上書きしても
 * 実際には natural_ms で無音自動終了してしまい表示と実挙動が乖離するため、
 * min(override, natural_ms) にとどめる(延長はしないが短縮は効く)。
 * 曲長不明(known==0。素のGBS/NSFなど判断材料が無い曲)は現状どおり
 * 強制する。autoならF-08どおりknownならnatural_ms、そうでなければ
 * default_length_secへフォールバックする。 */
int playlist_resolve_length_ms(int natural_ms, int known, int loops,
                                const mugbs_config_t *cfg) {
    if (cfg->length_override_sec > 0) {
        int override_ms = cfg->length_override_sec * 1000;
        if (loops || !known) return override_ms;
        return override_ms < natural_ms ? override_ms : natural_ms;
    }
    return known ? natural_ms : cfg->default_length_sec * 1000;
}

int playlist_effective_length_ms(const gme_info_t *info, const mugbs_config_t *cfg,
                                  int *out_known) {
    int known;
    int natural_ms = playlist_natural_length_ms(info, &known);
    if (out_known) *out_known = known;
    return playlist_resolve_length_ms(natural_ms, known, playlist_track_loops(info), cfg);
}

int playlist_is_short(const playlist_entry_t *e, const mugbs_config_t *cfg) {
    if (cfg->skip_short_sec <= 0) return 0;
    if (!e->length_known) return 0; /* 曲長不明は誤って消さない */
    return e->natural_ms <= cfg->skip_short_sec * 1000;
}

/* all[] から entries[](可視ビュー)を作り直す。all[] の要素を浅くコピーする
 * (title は借用。playlist_free() は entries[] 側の title を解放しない)。
 * keep_source/keep_track に非負を渡すと、そのトラックは skip_short_sec に
 * 関わらず必ず可視に残す。可視が1件も無ければフィルタ自体を諦めて全件可視に
 * 戻す(全滅ガード)。 */
static void rebuild_view(playlist_t *pl, const mugbs_config_t *cfg,
                          int keep_source, int keep_track) {
    free(pl->entries);
    pl->entries = malloc(sizeof(*pl->entries) * (size_t)pl->all_count);
    pl->entry_count = 0;

    for (int i = 0; i < pl->all_count; i++) {
        const playlist_entry_t *e = &pl->all[i];
        int keep = (e->source_index == keep_source && e->track_index == keep_track);
        if (keep || !playlist_is_short(e, cfg)) {
            pl->entries[pl->entry_count++] = *e;
        }
    }

    if (pl->entry_count == 0 && pl->all_count > 0) {
        /* 全滅ガード (Issue #21): 全トラックがしきい値以下だった場合、
         * フィルタを諦めて全件可視に戻す。曲が1つも無い状態で
         * playlist_open() を失敗させないため。 */
        LOG_WARN("skip_short_secにより全トラックが隠れるため、フィルタを無視して全曲表示します");
        for (int i = 0; i < pl->all_count; i++) {
            pl->entries[pl->entry_count++] = pl->all[i];
        }
    }
}

void playlist_apply_config(playlist_t *pl, const mugbs_config_t *cfg,
                            int keep_source, int keep_track) {
    for (int i = 0; i < pl->all_count; i++) {
        playlist_entry_t *e = &pl->all[i];
        e->duration_ms = playlist_resolve_length_ms(e->natural_ms, e->length_known, e->loops, cfg);
    }
    rebuild_view(pl, cfg, keep_source, keep_track);
}

int playlist_find_entry(const playlist_t *pl, int source_index, int track_index) {
    for (int i = 0; i < pl->entry_count; i++) {
        if (pl->entries[i].source_index == source_index &&
            pl->entries[i].track_index == track_index) {
            return i;
        }
    }
    return -1;
}

int playlist_effects_supported(const playlist_t *pl, int source_index) {
    if (!pl || source_index < 0 || source_index >= pl->source_count) return 1;
    return pl->sources[source_index].effects_supported;
}

int playlist_fade_start_ms(int length_ms, const mugbs_config_t *cfg) {
    return cfg->repeat_mode == REPEAT_ONE ? -1 : length_ms;
}

/* ---- playlist_t 構築 --------------------------------------------------- */

/* fs_path と zip_entry のどちらか一方だけを指定する(排他)。
 * zip由来のソースは zip_entry (zip内のエントリ名) を渡し、fs_path は NULL にする。 */
static int pl_add_source(playlist_t *pl, char *display_path, char *fs_path, char *zip_entry,
                          char *m3u_text, size_t m3u_len) {
    pl->sources = realloc(pl->sources, sizeof(*pl->sources) * (size_t)(pl->source_count + 1));
    playlist_source_t *s = &pl->sources[pl->source_count];
    s->display_path = display_path;
    s->fs_path = fs_path;
    s->zip_entry = zip_entry;
    s->m3u_text = m3u_text;
    s->m3u_len = m3u_len;
    s->author = dup_str("");
    s->copyright = dup_str("");
    s->system = dup_str("");
    s->effects_supported = 1; /* Issue #43: pl_scan_source()がSPCなら0へ差し替える */
    return pl->source_count++;
}

/* source_index の指すファイルを実際に開き、gme_track_count() 分だけ
 * gme_track_info() を回して all[] に追記する。曲長等の重い判定は
 * player.c 側(再生開始時)に任せ、ここではタイトルの収集に徹する。
 * zip由来のソース(zip_entry != NULL)は、その都度zipから展開して
 * gme_open_data() で開く(SPEC 5.3: 一時ファイルをディスクに書かない)。 */
static int pl_scan_source(playlist_t *pl, int source_index, const mugbs_config_t *cfg) {
    playlist_source_t *src = &pl->sources[source_index];

    Music_Emu *emu = NULL;
    gme_err_t err;

    if (src->zip_entry) {
        int idx = archive_find(pl->archive, src->zip_entry);
        if (idx < 0) {
            LOG_WARN("zip内にファイルが見つかりません: %s", src->zip_entry);
            return -1;
        }
        void *data = NULL;
        size_t size = 0;
        if (archive_extract(pl->archive, idx, &data, &size) != 0) {
            return -1;
        }
        err = gme_open_data(data, (long)size, &emu, cfg->sample_rate);
        /* gme_open_data はデータをコピーするので、ここで解放してよい
         * (SPEC 5.1落とし穴3として書かれている「コピーしない場合がある」は
         * 実際のlibgme(0.6.6)には当てはまらないことをヘッダで確認済み)。 */
        free(data);
        if (err) {
            LOG_WARN("開けませんでした(zip内 %s): %s", src->zip_entry, err);
            return -1;
        }
    } else {
        err = gme_open_file(src->fs_path, &emu, cfg->sample_rate);
        if (err) {
            LOG_WARN("開けませんでした: %s: %s", src->fs_path, err);
            return -1;
        }
    }

    /* Issue #43: SPCはEQ/ステレオ深度が効かない(playlist.hのeffects_supported
     * コメント参照)。Settings画面向けにここで一度だけ判定して焼き込む。 */
    src->effects_supported = (gme_type(emu) != gme_spc_type);

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

        /* Issue #21: スキャン結果は常に all[] へ追記する(全件・順序不変)。
         * 可視ビュー entries[] は playlist_open() の末尾で
         * playlist_apply_config() が all[] から作る。 */
        pl->all = realloc(pl->all, sizeof(*pl->all) * (size_t)(pl->all_count + 1));
        playlist_entry_t *e = &pl->all[pl->all_count];
        e->title = dup_meta(title);
        e->source_index = source_index;
        e->track_index = i;
        if (info) {
            e->natural_ms = playlist_natural_length_ms(info, &e->length_known);
            e->loops = playlist_track_loops(info);
        } else {
            e->natural_ms = 0;
            e->length_known = 0;
            e->loops = 0;
        }
        /* Issue #19: duration_msは「今使うべき」実効値。Length上書き中に
         * 開いたファイルでも、auto に戻したとき natural_ms から復元できる
         * よう別途保持しておく(playlist_apply_config()参照)。 */
        e->duration_ms = playlist_resolve_length_ms(e->natural_ms, e->length_known, e->loops, cfg);
        pl->all_count++;

        if (info) {
            if ((!pl->game || !pl->game[0]) && info->game[0]) {
                free(pl->game);
                pl->game = dup_meta(info->game);
            }
            /* ソース単位のメタデータ(SPEC 6.1)は最初に取得できたトラックの
             * ものを採用する。GBSの著作権・作者はファイル全体で共通なのが
             * 通例のため、トラックごとに上書きし続ける必要はない。 */
            if (!src->author[0] && info->author[0]) {
                free(src->author);
                src->author = dup_meta(info->author);
            }
            if (!src->copyright[0] && info->copyright[0]) {
                free(src->copyright);
                src->copyright = dup_meta(info->copyright);
            }
            if (!src->system[0] && info->system[0]) {
                free(src->system);
                src->system = dup_meta(info->system);
            }
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
        int source_index = pl_add_source(pl, dup_str(resolved), resolved, NULL, m3u_copy, seg->text_len);
        if (pl_scan_source(pl, source_index, cfg) == 0) {
            ok_any = 1;
        }
    }

    m3u_free_segments(segs, seg_count);
    return ok_any ? 0 : -1;
}

/* build_from_m3u_text() のzip版。参照ファイルの解決をファイルシステムの
 * パス結合ではなく archive_find() (パス区切り・大小文字の揺れを吸収) で行う。
 * (SPEC 5.3) */
static int build_from_m3u_text_zip(playlist_t *pl, const char *text, size_t len,
                                    const char *zip_path, const mugbs_config_t *cfg) {
    m3u_segment_t *segs = NULL;
    int seg_count = 0;
    if (m3u_split_segments(text, len, &segs, &seg_count) != 0) {
        LOG_ERR("m3uの解析に失敗しました(有効なエントリがありません)");
        return -1;
    }

    int ok_any = 0;
    for (int i = 0; i < seg_count; i++) {
        m3u_segment_t *seg = &segs[i];

        if (archive_find(pl->archive, seg->filename) < 0) {
            /* T-13相当: zip内にも参照先が見つからないエントリはスキップする。 */
            LOG_WARN("m3uが参照するファイルがzip内に見つかりません。スキップします: %s",
                      seg->filename);
            continue;
        }

        char display[600];
        snprintf(display, sizeof(display), "%s:%s", zip_path, seg->filename);
        char *m3u_copy = dup_len(seg->text, seg->text_len);
        int source_index = pl_add_source(pl, dup_str(display), NULL, dup_str(seg->filename),
                                          m3u_copy, seg->text_len);
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

/* .gbs/.gb/.nsf/.nsfe いずれもここへ来る(playlist_open()参照)。同名の
 * サイドカーm3uがあればそれで曲名を確定させ、無ければ単体ファイルとして
 * 開き gme_track_count() 分を自動命名で列挙する。形式ごとの分岐は無い
 * (libgmeがgme_open_file()で拡張子から自動判別する)。 */
static int playlist_open_music_file(playlist_t *pl, const char *path, const mugbs_config_t *cfg) {
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
        int source_index = pl_add_source(pl, dup_str(path), dup_str(path), NULL, NULL, 0);
        rc = pl_scan_source(pl, source_index, cfg);
    }

    return rc;
}

/* zip内のm3uエントリを名前順に並べるための比較関数 (qsort用)。
 * archive_list() は中央ディレクトリの列挙順を返すだけでソートしないため、
 * "01 BGM #01.m3u" ... "18 Jingle #01.m3u" を曲順に並べるにはここで
 * 明示的に並べ替える必要がある。 */
static int m3u_entry_cmp(const void *a, const void *b) {
    const archive_entry_t *const *pa = a;
    const archive_entry_t *const *pb = b;
    return strcasecmp((*pa)->name, (*pb)->name);
}

/* .zip を開く (SPEC 5.3)。
 *   - .m3u が(1つ以上)含まれる場合: **全ての** .m3u を名前順に連結して1つの
 *     テキストとみなし、build_from_m3u_text_zip() でセグメントごとにzip内の
 *     ファイルを解決する。zophar.net の配布パックのように1曲ごとの単曲m3uを
 *     18個同梱する形式が実在し、最初の1つだけを採用すると1曲しか再生できない
 *     ため (P9で修正。SPEC 5.3 / T-14)。
 *     連結してから m3u_split_segments() に通すのがポイントで、同じ .gbs を
 *     指す行は既存のセグメント分割ロジックが自然に1ソースへまとめてくれる
 *     (=「Track 1/18」と表示され、トラック切替のたびにzip展開とgme_open_data
 *     をやり直す無駄も無い)。異なる .gbs を指すm3uが混在する場合も、
 *     セグメント分割がそのままソースを分ける。
 *   - m3u が無い場合: zip内の音楽ファイルすべてを列挙し、各々の全トラックを
 *     自動命名で列挙する。SPEC本文は「1つだけなら即座に開く／複数ならユーザーに
 *     選択させる」だが、対話選択はUI(P5)の仕事であり、playlist_open()の時点では
 *     常にこの正規化されたモデルへ落とし込む方が上位(player.c)が単純になるため、
 *     1つでも複数でも同じコードパスで「常に再生可能なフラットなプレイリスト」を
 *     構築する(1つの場合はSPECの動作と自然に一致する)。 */
static int playlist_open_zip(playlist_t *pl, const char *path, const mugbs_config_t *cfg) {
    if (!file_exists(path)) {
        LOG_ERR("ファイルが見つかりません: %s", path);
        return -1;
    }

    archive_t *ar = NULL;
    if (archive_open(path, &ar) != 0) {
        return -1;
    }
    pl->archive = ar;

    archive_entry_t *aentries = NULL;
    int acount = 0;
    archive_list(ar, &aentries, &acount);

    /* zip内の全m3uを名前順に集める。 */
    archive_entry_t **m3us = NULL;
    int m3u_count = 0;
    for (int i = 0; i < acount; i++) {
        if (!aentries[i].is_m3u) continue;
        m3us = realloc(m3us, sizeof(*m3us) * (size_t)(m3u_count + 1));
        m3us[m3u_count++] = &aentries[i];
    }
    if (m3u_count > 1) {
        qsort(m3us, (size_t)m3u_count, sizeof(*m3us), m3u_entry_cmp);
        LOG_INFO("zip内に%d個のm3uがあります。名前順に連結して1つのプレイリストにします",
                  m3u_count);
    }

    int rc;
    if (m3u_count > 0) {
        /* 全m3uのテキストを改行区切りで連結する。archive_extract() の戻りは
         * NUL終端されないので、常に長さで扱うこと。 */
        char *text = NULL;
        size_t len = 0;
        for (int i = 0; i < m3u_count; i++) {
            void *part = NULL;
            size_t part_len = 0;
            if (archive_extract(ar, m3us[i]->index, &part, &part_len) != 0) {
                LOG_WARN("zip内のm3uを展開できませんでした。スキップします: %s", m3us[i]->name);
                continue;
            }
            /* +1 は区切りの改行。連結後にNUL終端はしない(長さで渡すため)。 */
            text = realloc(text, len + part_len + 1);
            memcpy(text + len, part, part_len);
            len += part_len;
            text[len++] = '\n';
            free(part);
        }
        free(m3us);
        if (!text) {
            LOG_ERR("zip内のm3uを1つも読めませんでした: %s", path);
            archive_free_entries(aentries, acount);
            return -1;
        }
        rc = build_from_m3u_text_zip(pl, text, len, path, cfg);
        free(text);
    } else {
        free(m3us);
        rc = -1;
        for (int i = 0; i < acount; i++) {
            if (!aentries[i].is_music) continue;
            char display[600];
            snprintf(display, sizeof(display), "%s:%s", path, aentries[i].name);
            int source_index = pl_add_source(pl, dup_str(display), NULL,
                                              dup_str(aentries[i].name), NULL, 0);
            if (pl_scan_source(pl, source_index, cfg) == 0) rc = 0;
        }
        if (rc != 0) {
            LOG_ERR("zip内に再生可能な音楽ファイルがありません: %s", path);
        }
    }

    archive_free_entries(aentries, acount);
    return rc;
}

int playlist_open(const char *path, const mugbs_config_t *config, playlist_t **out) {
    playlist_t *pl = calloc(1, sizeof(*pl));
    pl->game = dup_str("");

    int rc;
    if (ends_with_ci(path, ".zip")) {
        rc = playlist_open_zip(pl, path, config);
    } else if (ends_with_ci(path, ".m3u")) {
        rc = playlist_open_m3u(pl, path, config);
    } else {
        /* .gbs/.gb/.nsf/.nsfe いずれもここに来る。単体ファイル + 任意の
         * 同名サイドカーm3u、という扱いは形式によらず共通 (SPEC 5.2-3/4)。 */
        rc = playlist_open_music_file(pl, path, config);
    }

    /* Issue #21: スキャンはall[]へ追記されただけなので、可視ビュー
     * entries[]をここで初めて作る(keep_source/keep_trackは無し。
     * 再生開始前なので「いま鳴っている曲」は存在しない)。 */
    playlist_apply_config(pl, config, -1, -1);

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
        free(pl->sources[i].author);
        free(pl->sources[i].copyright);
        free(pl->sources[i].system);
    }
    free(pl->sources);
    /* Issue #21: title の所有権は all[] にある。entries[] は all[] からの
     * 浅いコピー(可視ビュー)なので、その title を解放してはいけない
     * (二重解放になる)。配列自体はどちらも free する。 */
    for (int i = 0; i < pl->all_count; i++) {
        free(pl->all[i].title);
    }
    free(pl->all);
    free(pl->entries);
    free(pl->game);
    archive_close(pl->archive); /* NULLなら何もしない */
    free(pl);
}

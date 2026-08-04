/* test_playlist.c - playlist.c の統合テスト。
 *
 * 合成した最小GBSファイル(ヘッダのみ有効な擬似ファイル。SPEC 10.3)と
 * 各種パターンのm3uテキストを実行時に一時ディレクトリへ書き出し、
 * playlist_open() が正しいエントリ表を構築することを確認する。
 * 著作権上の理由から本物の .gbs は使わない。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "playlist.h"
#include "test_util.h"

/* tests/fixtures/make_gbs.py のC版簡易実装。ヘッダのみ有効で、
 * initがGB APUのチャンネル1に書き込むだけの最小GBSを生成する。 */
static void write_synthetic_gbs(const char *path, int track_count) {
    unsigned char hdr[0x70];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'G'; hdr[1] = 'B'; hdr[2] = 'S'; hdr[3] = 1;
    hdr[4] = (unsigned char)track_count;
    hdr[5] = 1; /* first track */
    unsigned load = 0x400, init = 0x400;
    hdr[6] = load & 0xFF; hdr[7] = (load >> 8) & 0xFF;
    hdr[8] = init & 0xFF; hdr[9] = (init >> 8) & 0xFF;
    /* play address は init直後(下記コードの1バイト目、RET単体)を指す */
    unsigned play = init + 1;
    hdr[10] = play & 0xFF; hdr[11] = (play >> 8) & 0xFF;
    hdr[12] = 0xFE; hdr[13] = 0xFF; /* SP=0xFFFE */
    memcpy(hdr + 0x10, "Synthetic Game", 14);

    unsigned char code[2] = {0xC9, 0xC9}; /* init: RET / play: RET */

    FILE *f = fopen(path, "wb");
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(code, 1, sizeof(code), f);
    fclose(f);
}

static void write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static char g_tmpdir[256];

static void setup_tmpdir(void) {
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s/mugbs_test_playlist_XXXXXX", base);
    if (!mkdtemp(g_tmpdir)) {
        fprintf(stderr, "mkdtemp failed\n");
        exit(1);
    }
}

/* strdup した文字列を返す(呼び出し側で複数回呼んで結果を保持しても
 * 上書きされないように。プロセスは短命なテストなので解放しない)。 */
static char *path_in(const char *name) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", g_tmpdir, name);
    return strdup(buf);
}

/* T-01相当: m3uなしの単体.gbsを開く -> 全トラックが "Track NN" で列挙される */
static int test_no_m3u_auto_naming(void) {
    char *gbs = path_in("plain.gbs");
    write_synthetic_gbs(gbs, 3);

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 3);
    CHECK_STREQ(pl->entries[0].title, "Track 01");
    CHECK_STREQ(pl->entries[1].title, "Track 02");
    CHECK_STREQ(pl->entries[2].title, "Track 03");
    CHECK(pl->source_count == 1);

    playlist_free(pl);
    return 0;
}

/* T-02: 同名.m3uがある.gbsを開く -> 曲名がm3u通りに反映される */
static int test_sidecar_m3u(void) {
    char *gbs = path_in("sidecar.gbs");
    write_synthetic_gbs(gbs, 3);
    write_text_file(path_in("sidecar.m3u"),
        "sidecar.gbs::GBS,1,Title Screen,0:32\n"
        "sidecar.gbs::GBS,2,Overworld,2:34\n"
        "sidecar.gbs::GBS,3,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 3);
    CHECK_STREQ(pl->entries[0].title, "Title Screen");
    CHECK_STREQ(pl->entries[1].title, "Overworld");
    CHECK_STREQ(pl->entries[2].title, "Battle");

    playlist_free(pl);
    return 0;
}

/* T-03: .m3uを直接開く -> 同上。トラック順もm3uの記載順 */
static int test_open_m3u_directly(void) {
    char *gbs = path_in("direct.gbs");
    write_synthetic_gbs(gbs, 3);
    char *m3u = path_in("direct.m3u");
    write_text_file(m3u,
        "direct.gbs::GBS,3,Battle,1:45\n"   /* あえて記載順を入れ替える */
        "direct.gbs::GBS,1,Title Screen,0:32\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(m3u, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 2);
    /* m3uの記載順(Battleが先)がそのままエントリ順になること */
    CHECK_STREQ(pl->entries[0].title, "Battle");
    CHECK_STREQ(pl->entries[1].title, "Title Screen");

    playlist_free(pl);
    return 0;
}

/* T-04: 16進トラック番号($0A)を含むm3u -> エラーにならず曲名が反映される
 * (10番目の生トラックとして正しくremapされたことの間接証拠。
 * remapが失敗していればgme_track_info自体がエラーになりtitleが
 * "Track NN"にフォールバックするはず)。 */
static int test_hex_track_number(void) {
    char *gbs = path_in("hex.gbs");
    write_synthetic_gbs(gbs, 12); /* $0A=10番目が存在するよう十分な数を確保 */
    char *m3u = path_in("hex.m3u");
    write_text_file(m3u, "hex.gbs::GBS,$0A,Hex Track Ten,1:23\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(m3u, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 1);
    CHECK_STREQ(pl->entries[0].title, "Hex Track Ten");

    playlist_free(pl);
    return 0;
}

/* T-05: 複数ファイルを参照するm3u -> ファイルをまたいでエントリが構築される。
 * T-13: 存在しないファイルを参照するエントリはスキップされる(警告のみ)。 */
static int test_multi_file_and_missing(void) {
    char *gbs_a = path_in("multiA.gbs");
    char *gbs_b = path_in("multiB.gbs");
    write_synthetic_gbs(gbs_a, 2);
    write_synthetic_gbs(gbs_b, 2);
    char *m3u = path_in("multi.m3u");
    write_text_file(m3u,
        "multiA.gbs::GBS,1,A1,0:10\n"
        "multiB.gbs::GBS,1,B1,0:10\n"
        "does_not_exist.gbs::GBS,1,Ghost,0:05\n"
        "multiA.gbs::GBS,2,A2,0:10\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(m3u, &cfg, &pl) == 0);
    /* Ghost(存在しないファイル)はスキップされ、残り3エントリが構築される */
    CHECK(pl->entry_count == 3);
    CHECK_STREQ(pl->entries[0].title, "A1");
    CHECK_STREQ(pl->entries[1].title, "B1");
    CHECK_STREQ(pl->entries[2].title, "A2");
    /* A1とA2は同じファイルでもm3u上で非連続区間なので別ソース扱いになる */
    CHECK(pl->entries[0].source_index != pl->entries[2].source_index);
    CHECK(pl->source_count == 3);

    playlist_free(pl);
    return 0;
}

int main(void) {
    setup_tmpdir();

    if (test_no_m3u_auto_naming()) return 1;
    if (test_sidecar_m3u()) return 1;
    if (test_open_m3u_directly()) return 1;
    if (test_hex_track_number()) return 1;
    if (test_multi_file_and_missing()) return 1;

    printf("test_playlist: すべて成功\n");
    return 0;
}

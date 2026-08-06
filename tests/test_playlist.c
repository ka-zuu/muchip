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
#include "miniz.h"
#include "playlist.h"
#include "test_util.h"

/* 合成GBSのバイト列 (ヘッダ0x70 + コード2バイト)。 */
#define SYNTHETIC_GBS_SIZE (0x70 + 2)

/* tests/fixtures/make_gbs.py のC版簡易実装。ヘッダのみ有効で、
 * initがGB APUのチャンネル1に書き込むだけの最小GBSを out に組み立てる。
 * out は SYNTHETIC_GBS_SIZE バイト以上であること。 */
static void build_synthetic_gbs(unsigned char *out, int track_count) {
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

    memcpy(out, hdr, sizeof(hdr));
    memcpy(out + sizeof(hdr), code, sizeof(code));
}

static void write_synthetic_gbs(const char *path, int track_count) {
    unsigned char buf[SYNTHETIC_GBS_SIZE];
    build_synthetic_gbs(buf, track_count);
    FILE *f = fopen(path, "wb");
    fwrite(buf, 1, sizeof(buf), f);
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

/* ---- zip を組み立てるヘルパ (T-14用) --------------------------------------
 *
 * tests/test_archive.c の write_test_zip() と同趣旨だが、あちらは内容を
 * strlen() で測るためNULを含むバイナリ(合成GBS)を入れられない。ここでは
 * (ポインタ, 長さ) の組を取る。 */
typedef struct {
    const char *name;
    const void *data;
    size_t size;
} zip_member_t;

static void write_binary_zip(const char *zip_path, const zip_member_t *members, int count) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, zip_path, 0)) {
        fprintf(stderr, "mz_zip_writer_init_file failed: %s\n", zip_path);
        exit(1);
    }
    for (int i = 0; i < count; i++) {
        if (!mz_zip_writer_add_mem(&zip, members[i].name, members[i].data, members[i].size,
                                    MZ_BEST_COMPRESSION)) {
            fprintf(stderr, "mz_zip_writer_add_mem failed: %s\n", members[i].name);
            exit(1);
        }
    }
    if (!mz_zip_writer_finalize_archive(&zip)) {
        fprintf(stderr, "mz_zip_writer_finalize_archive failed\n");
        exit(1);
    }
    mz_zip_writer_end(&zip);
}

/* T-14: zip内に「1曲ごとの単曲m3u」が複数入っている場合
 * (zophar.net の配布パック形式)、全てがマージされて全曲再生できること。
 * 修正前は最初の1つだけが採用され1曲しか再生できなかった (P9)。
 *
 * わざと中央ディレクトリの順序を曲順と逆に詰めることで、名前順ソートが
 * 効いていることも同時に確認する。 */
static int test_zip_multiple_m3u(void) {
    unsigned char gbs[SYNTHETIC_GBS_SIZE];
    build_synthetic_gbs(gbs, 3);

    const char *m3u1 = "GAME.gbs::GBS,1,BGM #01,0:39,,10\n";
    const char *m3u2 = "GAME.gbs::GBS,2,BGM #02,1:02,,10\n";
    const char *m3u3 = "GAME.gbs::GBS,3,Jingle #01,0:05,,10\n";

    /* 追加順(=中央ディレクトリ順)は 03, 01, 02 とバラバラにしておく。 */
    zip_member_t members[] = {
        { "03 Jingle #01.m3u", m3u3, strlen(m3u3) },
        { "GAME.gbs",           gbs,  sizeof(gbs) },
        { "01 BGM #01.m3u",    m3u1, strlen(m3u1) },
        { "02 BGM #02.m3u",    m3u2, strlen(m3u2) },
    };
    char *zip = path_in("multi_m3u.zip");
    write_binary_zip(zip, members, (int)(sizeof(members) / sizeof(members[0])));

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(zip, &cfg, &pl) == 0);
    /* 3つのm3uがマージされ、3曲すべてが列挙される(修正前は1曲だった)。 */
    CHECK(pl->entry_count == 3);
    /* 全て同じ .gbs を指すので1ソースにまとまる(=トラック切替のたびに
     * zip展開とgme_open_dataをやり直さない)。 */
    CHECK(pl->source_count == 1);
    /* ファイル名順に連結されるので 01 -> 02 -> 03 の並びになる。 */
    CHECK_STREQ(pl->entries[0].title, "BGM #01");
    CHECK_STREQ(pl->entries[1].title, "BGM #02");
    CHECK_STREQ(pl->entries[2].title, "Jingle #01");

    playlist_free(pl);
    return 0;
}

/* zip内のm3uが1つだけの場合も従来どおり動くこと(連結処理の退行防止)。 */
static int test_zip_single_m3u(void) {
    unsigned char gbs[SYNTHETIC_GBS_SIZE];
    build_synthetic_gbs(gbs, 2);

    const char *m3u = "GAME.gbs::GBS,1,First,0:10\n"
                       "GAME.gbs::GBS,2,Second,0:20\n";
    zip_member_t members[] = {
        { "GAME.gbs",  gbs, sizeof(gbs) },
        { "GAME.m3u", m3u, strlen(m3u) },
    };
    char *zip = path_in("single_m3u.zip");
    write_binary_zip(zip, members, 2);

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(zip, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 2);
    CHECK(pl->source_count == 1);
    CHECK_STREQ(pl->entries[0].title, "First");
    CHECK_STREQ(pl->entries[1].title, "Second");

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
    if (test_zip_single_m3u()) return 1;
    if (test_zip_multiple_m3u()) return 1;

    printf("test_playlist: すべて成功\n");
    return 0;
}

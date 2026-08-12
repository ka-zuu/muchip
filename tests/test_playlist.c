/* test_playlist.c - playlist.c の統合テスト。
 *
 * 合成した最小GBS/NSFファイル(ヘッダのみ有効な擬似ファイル。SPEC 10.3)と
 * 各種パターンのm3uテキストを実行時に一時ディレクトリへ書き出し、
 * playlist_open() が正しいエントリ表を構築することを確認する。
 * 著作権上の理由から本物の .gbs / .nsf は使わない。
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

/* 合成NSFのバイト列 (ヘッダ0x80 + コード2バイト)。 */
#define SYNTHETIC_NSF_SIZE (0x80 + 2)

/* build_synthetic_gbs() のNSF版 (Issue #2)。ヘッダ構造は
 * vendor/game-music-emu/gme/Nsf_Emu.h の header_t 参照。GBSと同様
 * ヘッダのみ有効で、init/playはRTS(0x60)単体で足りる最小ファイルを組み立てる。
 * load/init を rom_begin(=Nsf_Emu.h の enum rom_begin = 0x8000)に置くこと。 */
static void build_synthetic_nsf(unsigned char *out, int track_count) {
    unsigned char hdr[0x80];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "NESM\x1A", 5);
    hdr[5] = 1; /* version */
    hdr[6] = (unsigned char)track_count;
    hdr[7] = 1; /* first_track。NSFは1始まり(GBSと違いheader上も1始まり) */
    unsigned load = 0x8000, init = 0x8000;
    hdr[8] = load & 0xFF; hdr[9] = (load >> 8) & 0xFF;
    hdr[10] = init & 0xFF; hdr[11] = (init >> 8) & 0xFF;
    /* play address は init直後(下記コードの1バイト目、RTS単体)を指す */
    unsigned play = init + 1;
    hdr[12] = play & 0xFF; hdr[13] = (play >> 8) & 0xFF;
    memcpy(hdr + 14, "Synthetic NES Game", 18); /* game[32] (offset 0x0E) */

    unsigned char code[2] = {0x60, 0x60}; /* init: RTS / play: RTS */

    memcpy(out, hdr, sizeof(hdr));
    memcpy(out + sizeof(hdr), code, sizeof(code));
}

static void write_synthetic_nsf(const char *path, int track_count) {
    unsigned char buf[SYNTHETIC_NSF_SIZE];
    build_synthetic_nsf(buf, track_count);
    FILE *f = fopen(path, "wb");
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
}

static void write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

/* g_tmpdir / setup_tmpdir() / path_in() は test_util.h にある。 */

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
    /* 10進のトラック番号は0始まり(GBSの生のsubtrack索引と同じ。
     * vendor/game-music-emu/gme/Gbs_Emu.cpp のflags_パッチ参照)。 */
    write_text_file(path_in("sidecar.m3u"),
        "sidecar.gbs::GBS,0,Title Screen,0:32\n"
        "sidecar.gbs::GBS,1,Overworld,2:34\n"
        "sidecar.gbs::GBS,2,Battle,1:45\n");

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

/* Issue #2 T-01相当: m3uなしの単体.nsfを開く -> browser.cの拡張子フィルタとは
 * 独立に、playlist_open()が.nsfを.gbsと同じ「単体音楽ファイル」経路
 * (playlist_open_music_file())で扱えることを確認する。 */
static int test_nsf_no_m3u_auto_naming(void) {
    char *nsf = path_in("plain.nsf");
    write_synthetic_nsf(nsf, 3);

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(nsf, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 3);
    CHECK_STREQ(pl->entries[0].title, "Track 01");
    CHECK_STREQ(pl->entries[1].title, "Track 02");
    CHECK_STREQ(pl->entries[2].title, "Track 03");
    CHECK(pl->source_count == 1);

    playlist_free(pl);
    return 0;
}

/* Issue #2: NSFの拡張M3Uは10進トラック番号が1始まり(GBSの0始まりとは逆)。
 * これはgame-music-emu本家(upstream)の既定動作そのものであり、GBS用に
 * 当てたフォークパッチ(vendor/game-music-emu/gme/Gbs_Emu.cpp の
 * flags_ |= 0x02。Gme_File::remap_track_()参照)は gme_gbs_type_ にしか
 * 適用されていないため、gme_nsf_type_ には影響しない。
 * m3u上で "NSF,1,...".."NSF,3,..." (1始まり)と書き、3トラックとも
 * エラーなく列挙できることを確認する。
 *
 * test_decimal_track_number_is_zero_based() と同じ限界がある: titleは
 * remap後ではなくm3uファイル上の位置からそのまま採用されるため、この
 * テストだけでは「実際に鳴る物理トラックが正しい」ことまでは証明できない
 * (物理トラックの検証は実機で実際のNSFリップを再生して確認する。
 * docs/design-notes.md「libgmeフォーク運用」参照)。ここでは「1始まりの宣言がエラーにならず期待通りの
 * entry_count/titleになる」ことのスモークテストとして残す。 */
static int test_nsf_sidecar_m3u_is_one_based(void) {
    char *nsf = path_in("nsf_sidecar.nsf");
    write_synthetic_nsf(nsf, 3);
    write_text_file(path_in("nsf_sidecar.m3u"),
        "nsf_sidecar.nsf::NSF,1,Title Screen,0:32\n"
        "nsf_sidecar.nsf::NSF,2,Overworld,2:34\n"
        "nsf_sidecar.nsf::NSF,3,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(nsf, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 3);
    CHECK_STREQ(pl->entries[0].title, "Title Screen");
    CHECK_STREQ(pl->entries[1].title, "Overworld");
    CHECK_STREQ(pl->entries[2].title, "Battle");

    playlist_free(pl);
    return 0;
}

/* 実機で使っているzophar.net配布パック(Parodius (EMU).zophar)の実データを
 * 確認したところ、各m3uの10進トラック番号は0始まりだった
 * (例: "01 Parodius Ondo.m3u"が"GBS,0,..."、"02 Hello.m3u"が"GBS,1,..."、
 * ...のように、1曲目から0で始まる)。
 *
 * ところが同梱しているgame-music-emu(libgme)は元々、10進のm3uトラック番号を
 * 「1始まり」とみなして内部で-1する仕様だった(Gme_File::remap_track_()。
 * 16進($始まり)はこの-1の対象外)。この前提が実際のファイルの0始まり
 * 慣習と食い違い、宣言「0」は-1されて不正な索引(-1)になり、宣言「N」は
 * 本来の(N-1)番目のトラックを再生してしまう、というズレが起きていた
 * (ユーザー報告: 「m3uで2曲目を再生すると、GBS内の1曲目が再生される」)。
 * vendor/game-music-emu/gme/Gbs_Emu.cpp の flags_ に 0x02
 * (KSSと同じ「10進もそのまま使う」ビット)を立てるパッチで、GBSの10進の
 * m3uトラック番号は0始まりのまま(-1されずに)使われるようにした。
 *
 * **このテストが検証できる範囲には限界がある**: GBSの
 * `Gbs_Emu::track_info_()` は渡されたトラック番号を一切使わず(GBSヘッダの
 * game/author/copyrightをそのまま返すだけ)、m3uロード時のtitle
 * (`out->song`)も remap 後の値ではなく m3u ファイル上の位置
 * (`playlist[track]`。track はこちらが渡した0始まりのループ添字で、
 * remapの結果とは無関係)から取られる。そのため title の一致は
 * remap(=実際に鳴る物理トラック)の正しさを一切証明しない
 * (このテストは実際、flags_パッチを外した状態でも成功することを
 * 確認済み)。ここでは「宣言0がクラッシュ/エラーにならず
 * entry_countやtitleが崩れない」という限定的なスモークテストとして
 * 残す。**物理トラックの選択が正しいことの検証は、実機で実際に
 * Parodius (EMU).zopharパックを聴いて確認する
 * (docs/design-notes.md「libgmeフォーク運用」参照)。** */
static int test_decimal_track_number_is_zero_based(void) {
    char *gbs = path_in("zerobased.gbs");
    write_synthetic_gbs(gbs, 3);
    char *m3u = path_in("zerobased.m3u");
    write_text_file(m3u,
        "zerobased.gbs::GBS,0,First,0:10\n"
        "zerobased.gbs::GBS,1,Second,0:10\n"
        "zerobased.gbs::GBS,2,Third,0:10\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(m3u, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 3);
    /* 修正前は宣言0がremapエラーになり"Track 01"にフォールバックしていた。 */
    CHECK_STREQ(pl->entries[0].title, "First");
    CHECK_STREQ(pl->entries[1].title, "Second");
    CHECK_STREQ(pl->entries[2].title, "Third");

    playlist_free(pl);
    return 0;
}

/* T-03: .m3uを直接開く -> 同上。トラック順もm3uの記載順 */
static int test_open_m3u_directly(void) {
    char *gbs = path_in("direct.gbs");
    write_synthetic_gbs(gbs, 3);
    char *m3u = path_in("direct.m3u");
    write_text_file(m3u,
        "direct.gbs::GBS,2,Battle,1:45\n"   /* あえて記載順を入れ替える(0始まり) */
        "direct.gbs::GBS,0,Title Screen,0:32\n");

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
 * "Track NN"にフォールバックするはず)。16進は元々0始まりの生索引として
 * 扱われる(decimal_trackフラグが立たないため-1されない)ので、
 * flags_パッチの影響を受けない。 */
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
        "multiA.gbs::GBS,0,A1,0:10\n"
        "multiB.gbs::GBS,0,B1,0:10\n"
        "does_not_exist.gbs::GBS,0,Ghost,0:05\n"
        "multiA.gbs::GBS,1,A2,0:10\n");

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

    const char *m3u1 = "GAME.gbs::GBS,0,BGM #01,0:39,,10\n";
    const char *m3u2 = "GAME.gbs::GBS,1,BGM #02,1:02,,10\n";
    const char *m3u3 = "GAME.gbs::GBS,2,Jingle #01,0:05,,10\n";

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

    const char *m3u = "GAME.gbs::GBS,0,First,0:10\n"
                       "GAME.gbs::GBS,1,Second,0:20\n";
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

/* Issue #19/#24: playlist_resolve_length_ms() は length_override_sec の
 * みを見る純関数(loopsも引数で渡すだけ)。SDLもlibgmeの初期化も要らない。 */
static int test_resolve_length_ms(void) {
    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.default_length_sec = 180;

    /* auto(0): 既知ならnatural_msをそのまま、不明ならdefault_length_secへ
     * (loopsの値には左右されない) */
    cfg.length_override_sec = 0;
    CHECK(playlist_resolve_length_ms(32000, 1, 0, &cfg) == 32000);
    CHECK(playlist_resolve_length_ms(32000, 1, 1, &cfg) == 32000);
    CHECK(playlist_resolve_length_ms(0, 0, 0, &cfg) == 180000);

    /* 上書き中(900秒=15分):
     * - ループする曲(loops!=0)は既知/不明を問わず強制される (F-28) */
    cfg.length_override_sec = 900;
    CHECK(playlist_resolve_length_ms(32000, 1, 1, &cfg) == 900000);
    CHECK(playlist_resolve_length_ms(0, 0, 1, &cfg) == 900000);
    /* - 曲長不明(known=0)は loops=0 でも強制される(判断材料が無いため。
     *   素のGBS/NSFの現行動作を保つ) */
    CHECK(playlist_resolve_length_ms(0, 0, 0, &cfg) == 900000);
    /* - Issue #24: 曲長既知(known=1)かつループしない(loops=0)曲は、
     *   延長されず min(override, natural_ms) にキャップされる */
    CHECK(playlist_resolve_length_ms(32000, 1, 0, &cfg) == 32000);
    /* - 同条件でも、natural_msの方が上書き値より長ければ短縮方向は効く
     *   (このケースは60分相当のnatural_msなので900秒側が勝つ) */
    CHECK(playlist_resolve_length_ms(3600000, 1, 0, &cfg) == 900000);

    /* autoへ戻すと、渡したnatural_msがそのまま復元される
     * (=呼び出し側がnatural_msを保持しておけば情報が失われない) */
    cfg.length_override_sec = 0;
    CHECK(playlist_resolve_length_ms(32000, 1, 0, &cfg) == 32000);

    return 0;
}

/* Issue #19/#24: これらのトラックはm3uの曲長欄だけを持ちループ欄が無い
 * (=loops==0)、つまり実際には途中で終わる曲。ながさチェンジ中でも
 * duration_msがnatural_msを超えて延長されないこと(Issue #24)、
 * playlist_apply_config()でauto(0)へ戻しても引き続きnatural_msのまま
 * であること(=情報が失われていないこと)を確認する。
 * test_sidecar_m3u()と同じ合成フィクスチャ・m3u構文(拡張M3Uの曲長
 * フィールド)を使う。 */
static int test_length_override_applies_and_reverts(void) {
    char *gbs = path_in("length_override.gbs");
    write_synthetic_gbs(gbs, 3);
    write_text_file(path_in("length_override.m3u"),
        "length_override.gbs::GBS,0,Title Screen,0:32\n"
        "length_override.gbs::GBS,1,Overworld,2:34\n"
        "length_override.gbs::GBS,2,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.length_override_sec = 900; /* 15分 (Settingsが出す選択肢の1つ) */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 3);

    /* Issue #24: ループしない曲(loops==0)なので、上書き中でも
     * duration_msはm3uの実測値のまま(15分へ延長されない)。 */
    CHECK(pl->entries[0].duration_ms == 32000);
    CHECK(pl->entries[1].duration_ms == 154000);
    CHECK(pl->entries[2].duration_ms == 105000);
    for (int i = 0; i < pl->entry_count; i++) {
        CHECK(pl->entries[i].length_known); /* 実測できたこと自体は変わらない */
        CHECK(!pl->entries[i].loops);
    }
    /* 実測値がnatural_msに残っていること(上書きに巻き込まれて消えていない)。 */
    CHECK(pl->entries[0].natural_ms == 32000);
    CHECK(pl->entries[1].natural_ms == 154000);
    CHECK(pl->entries[2].natural_ms == 105000);

    /* autoへ戻す(Settings画面でLengthをautoに操作したときと同じ経路)。
     * ファイルを開き直さずに m3u 由来の実測値へ復元されること
     * (この場合、上書き中と同じ値になる)。 */
    cfg.length_override_sec = 0;
    playlist_apply_config(pl, &cfg, -1, -1);
    CHECK(pl->entries[0].duration_ms == 32000);
    CHECK(pl->entries[1].duration_ms == 154000);
    CHECK(pl->entries[2].duration_ms == 105000);

    /* もう一度上書きへ戻しても、ループしない曲は引き続き延長されないこと
     * (往復できること)。300秒(5分)は各曲の実測値より長いので、
     * ここでもduration_msは実測値のまま。 */
    cfg.length_override_sec = 300;
    playlist_apply_config(pl, &cfg, -1, -1);
    CHECK(pl->entries[0].duration_ms == 32000);
    CHECK(pl->entries[1].duration_ms == 154000);
    CHECK(pl->entries[2].duration_ms == 105000);

    playlist_free(pl);
    return 0;
}

/* Issue #24: m3uのループ欄付き("Overworld,2:34,-" = 全体がループする曲。
 * M3u_Playlist.cpp: parse_line() の trailing '-' 記法)なトラックは、
 * ながさチェンジ中に上書き値まで延長されること(F-28本来の挙動)を確認する。 */
static int test_length_override_extends_looping_track(void) {
    char *gbs = path_in("length_override_loop.gbs");
    write_synthetic_gbs(gbs, 1);
    write_text_file(path_in("length_override_loop.m3u"),
        "length_override_loop.gbs::GBS,0,Overworld,2:34,-\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.length_override_sec = 900; /* 15分 */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 1);
    CHECK(pl->entries[0].loops);
    CHECK(pl->entries[0].length_known);
    CHECK(pl->entries[0].natural_ms == 154000); /* 2:34。上書きの影響を受けない */
    CHECK(pl->entries[0].duration_ms == 900000); /* 延長される */

    playlist_free(pl);
    return 0;
}

/* Issue #24: sidecar m3uが無い素のGBS(曲長情報を一切持たない
 * Tetris等と同条件)は、ながさチェンジ中も現状どおり上書き値へ
 * 強制されること(=回帰防止)。判断材料が無いトラックまで
 * キャップしてしまうと、実在の合成音楽が短く切られてしまう。 */
static int test_length_override_applies_to_unknown_length(void) {
    char *gbs = path_in("length_override_unknown.gbs");
    write_synthetic_gbs(gbs, 2);
    /* m3uを書かない: gme_track_info() が length/intro/loop すべて
     * -1 を返す素のGBSと同じ状態になる。 */

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.length_override_sec = 900; /* 15分 */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 2);
    for (int i = 0; i < pl->entry_count; i++) {
        CHECK(!pl->entries[i].length_known);
        CHECK(!pl->entries[i].loops);
        CHECK(pl->entries[i].duration_ms == 900000);
    }

    playlist_free(pl);
    return 0;
}

/* Issue #21: skip_short_sec が非0のとき、実測曲長がしきい値以下のトラックが
 * entries[](可視ビュー)から隠れること。境界(ちょうどしきい値と同じ長さ)も
 * 隠れる側(<=)であることを確認する。test_sidecar_m3u()と同じ合成
 * フィクスチャ・m3u構文を使う。 */
static int test_skip_short_hides_and_boundary(void) {
    char *gbs = path_in("skip_short.gbs");
    write_synthetic_gbs(gbs, 4);
    write_text_file(path_in("skip_short.m3u"),
        "skip_short.gbs::GBS,0,Title Screen,0:32\n"
        "skip_short.gbs::GBS,1,Jingle,0:03\n"
        "skip_short.gbs::GBS,2,Exactly Five,0:05\n"
        "skip_short.gbs::GBS,3,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.skip_short_sec = 5; /* Jingle(3秒)とExactly Five(5秒。境界)が対象 */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 2);
    CHECK_STREQ(pl->entries[0].title, "Title Screen");
    CHECK_STREQ(pl->entries[1].title, "Battle");
    /* all[]側は全件残っている(隠しているのはビューだけ)。 */
    CHECK(pl->all_count == 4);

    playlist_free(pl);
    return 0;
}

/* Issue #21: skip_short_sec = 0(off、既定)では何も隠れないこと
 * (アップデートしても既存ユーザーの一覧が黙って変わらないための非退行確認)。 */
static int test_skip_short_off_keeps_all(void) {
    char *gbs = path_in("skip_short_off.gbs");
    write_synthetic_gbs(gbs, 2);
    write_text_file(path_in("skip_short_off.m3u"),
        "skip_short_off.gbs::GBS,0,Jingle,0:03\n"
        "skip_short_off.gbs::GBS,1,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    CHECK(cfg.skip_short_sec == 0);

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 2);

    playlist_free(pl);
    return 0;
}

/* Issue #21: 曲長不明(m3uの時間フィールドが空、length_known==0)のトラックは
 * skip_short_secの対象外(誤って消さない)。
 *
 * m3uをロードすると gme_track_count() は生のトラック数ではなく
 * m3uのエントリ数になる(vendor/game-music-emu/gme/M3u_Playlist.cpp の
 * track_count_ = playlist.size())。そのため「曲長不明のトラック」を
 * 混在させるには、そのトラック用の行自体は書きつつ時間フィールドだけ
 * 空にする必要がある(時間フィールドを省略すると
 * vendor/game-music-emu/gme/Gme_File.cpp の track_info() が
 * out->length を既定値-1のままにする=unknownになる)。 */
static int test_skip_short_keeps_unknown_length(void) {
    char *gbs = path_in("skip_short_unknown.gbs");
    write_synthetic_gbs(gbs, 2);
    write_text_file(path_in("skip_short_unknown.m3u"),
        "skip_short_unknown.gbs::GBS,0,Jingle,0:03\n"
        /* 名前・時間とも空にするには末尾にもう1つカンマが要る
         * (M3u_Playlist.cppのparse_name(): 空の名前フィールドを区切りの
         * カンマとして認識させるには、その直後がカンマ/ダッシュ/数字の
         * いずれかである必要がある。無いと直前のカンマ自体が名前の一部
         * として飲み込まれてしまう)。結果は "Track 02"・曲長不明になる。 */
        "skip_short_unknown.gbs::GBS,1,,,\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.skip_short_sec = 30; /* Jingleは確実に隠れるしきい値 */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    /* Jingle(実測3秒、既知)は隠れるが、曲長不明の"Track 02"は残る。 */
    CHECK(pl->entry_count == 1);
    CHECK_STREQ(pl->entries[0].title, "Track 02");
    CHECK(!pl->entries[0].length_known);

    playlist_free(pl);
    return 0;
}

/* Issue #21: Length(length_override_sec)で全曲の見かけの長さを上書き中でも、
 * スキップ判定は常に実測長(natural_ms)で行われること。上書きで曲長不明の
 * トラックまで「見かけ上15分」になっても、それだけでskip_short_secの対象には
 * ならない(length_knownが0のままなので)。
 * BattleとJingleはどちらもm3uにループ欄が無い(loops==0)ので、Issue #24
 * により上書き自体は「延長しない」側で効く(min(override, natural_ms))。 */
static int test_skip_short_ignores_length_override(void) {
    char *gbs = path_in("skip_short_override.gbs");
    write_synthetic_gbs(gbs, 2);
    write_text_file(path_in("skip_short_override.m3u"),
        "skip_short_override.gbs::GBS,0,Jingle,0:03\n"
        "skip_short_override.gbs::GBS,1,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.skip_short_sec = 5;
    cfg.length_override_sec = 900; /* 15分(Battleの実測1:45より長い) */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    /* 上書き中でも、中身が3秒のJingleは隠れる。 */
    CHECK(pl->entry_count == 1);
    CHECK_STREQ(pl->entries[0].title, "Battle");
    CHECK(pl->entries[0].duration_ms == 105000); /* Issue #24: 延長されない */

    playlist_free(pl);
    return 0;
}

/* Issue #21: playlist_apply_config()でしきい値を上げ下げしたとき、
 * entries[]の件数がそのたびに正しく増減すること(情報が失われていない)。 */
static int test_skip_short_apply_config_round_trip(void) {
    char *gbs = path_in("skip_short_toggle.gbs");
    write_synthetic_gbs(gbs, 2);
    write_text_file(path_in("skip_short_toggle.m3u"),
        "skip_short_toggle.gbs::GBS,0,Jingle,0:03\n"
        "skip_short_toggle.gbs::GBS,1,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    CHECK(pl->entry_count == 2); /* skip_short_sec=0(既定)なので開いた時点では全件 */

    cfg.skip_short_sec = 5;
    playlist_apply_config(pl, &cfg, -1, -1);
    CHECK(pl->entry_count == 1);
    CHECK_STREQ(pl->entries[0].title, "Battle");

    cfg.skip_short_sec = 0;
    playlist_apply_config(pl, &cfg, -1, -1);
    CHECK(pl->entry_count == 2);
    CHECK_STREQ(pl->entries[0].title, "Jingle");
    CHECK_STREQ(pl->entries[1].title, "Battle");

    /* 往復できること(もう一度上げても同じ結果になる)。 */
    cfg.skip_short_sec = 5;
    playlist_apply_config(pl, &cfg, -1, -1);
    CHECK(pl->entry_count == 1);

    playlist_free(pl);
    return 0;
}

/* Issue #21: keep_source/keep_track に「いま再生中のトラック」を渡すと、
 * しきい値以下でもそのトラックだけは可視に残ること(しきい値変更で
 * 再生中の曲を見失わないための仕組み。app_apply_settings()参照)。 */
static int test_skip_short_keeps_current_track(void) {
    char *gbs = path_in("skip_short_keep.gbs");
    write_synthetic_gbs(gbs, 2);
    write_text_file(path_in("skip_short_keep.m3u"),
        "skip_short_keep.gbs::GBS,0,Jingle,0:03\n"
        "skip_short_keep.gbs::GBS,1,Battle,1:45\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    int jingle_source = pl->entries[0].source_index;
    int jingle_track = pl->entries[0].track_index;

    cfg.skip_short_sec = 5;
    /* Jingleを再生中、という体でkeepを渡す。 */
    playlist_apply_config(pl, &cfg, jingle_source, jingle_track);
    CHECK(pl->entry_count == 2); /* 本来隠れるはずのJingleも残る */
    CHECK(playlist_find_entry(pl, jingle_source, jingle_track) >= 0);

    /* 再生中でなくなれば(keepを渡さなければ)通常どおり隠れる。 */
    playlist_apply_config(pl, &cfg, -1, -1);
    CHECK(pl->entry_count == 1);
    CHECK(playlist_find_entry(pl, jingle_source, jingle_track) < 0);

    playlist_free(pl);
    return 0;
}

/* Issue #21: 全滅ガード。既知の曲長を持つ全トラックがしきい値以下の場合、
 * フィルタを諦めて全件可視に戻る(playlist_open()がエントリなしで
 * 失敗しないように)。 */
static int test_skip_short_all_filtered_guard(void) {
    char *gbs = path_in("skip_short_guard.gbs");
    write_synthetic_gbs(gbs, 2);
    write_text_file(path_in("skip_short_guard.m3u"),
        "skip_short_guard.gbs::GBS,0,Jingle A,0:02\n"
        "skip_short_guard.gbs::GBS,1,Jingle B,0:03\n");

    mugbs_config_t cfg;
    config_set_defaults(&cfg);
    cfg.skip_short_sec = 30; /* 両方とも対象になるしきい値 */

    playlist_t *pl = NULL;
    CHECK(playlist_open(gbs, &cfg, &pl) == 0);
    /* 全件フィルタされるはずが、全滅ガードにより2件とも可視のまま。 */
    CHECK(pl->entry_count == 2);

    playlist_free(pl);
    return 0;
}

/* Issue #15: playlist_fade_start_ms() は REPEAT_ONE のときだけフェードを
 * 無効化(-1)する純関数。SDLもlibgmeの初期化も要らない。 */
static int test_fade_start_ms(void) {
    mugbs_config_t cfg;
    config_set_defaults(&cfg);

    cfg.repeat_mode = REPEAT_NONE;
    CHECK(playlist_fade_start_ms(12345, &cfg) == 12345);

    cfg.repeat_mode = REPEAT_ALL;
    CHECK(playlist_fade_start_ms(12345, &cfg) == 12345);

    cfg.repeat_mode = REPEAT_ONE;
    CHECK(playlist_fade_start_ms(12345, &cfg) == -1);
    CHECK(playlist_fade_start_ms(0, &cfg) == -1); /* 0msでもフェードは無効のまま */

    return 0;
}

int main(void) {
    setup_tmpdir("playlist");

    if (test_no_m3u_auto_naming()) return 1;
    if (test_sidecar_m3u()) return 1;
    if (test_nsf_no_m3u_auto_naming()) return 1;
    if (test_nsf_sidecar_m3u_is_one_based()) return 1;
    if (test_decimal_track_number_is_zero_based()) return 1;
    if (test_open_m3u_directly()) return 1;
    if (test_hex_track_number()) return 1;
    if (test_multi_file_and_missing()) return 1;
    if (test_zip_single_m3u()) return 1;
    if (test_zip_multiple_m3u()) return 1;
    if (test_fade_start_ms()) return 1;
    if (test_resolve_length_ms()) return 1;
    if (test_length_override_applies_and_reverts()) return 1;
    if (test_length_override_extends_looping_track()) return 1;
    if (test_length_override_applies_to_unknown_length()) return 1;
    if (test_skip_short_hides_and_boundary()) return 1;
    if (test_skip_short_off_keeps_all()) return 1;
    if (test_skip_short_keeps_unknown_length()) return 1;
    if (test_skip_short_ignores_length_override()) return 1;
    if (test_skip_short_apply_config_round_trip()) return 1;
    if (test_skip_short_keeps_current_track()) return 1;
    if (test_skip_short_all_filtered_guard()) return 1;

    printf("test_playlist: すべて成功\n");
    return 0;
}

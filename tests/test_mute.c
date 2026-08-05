/* test_mute.c - T-10「4chミュート: 該当チャンネルのみ無音になる」の自動検証。 (P8, F-10)
 *
 * mugbs 側のミュートは player.c が config の voice_mute_mask を
 * gme_mute_voices() へ渡すだけの薄い橋渡しなので、ここで固定したいのは
 * 「そのビットマスクの意味論」そのもの:
 *   - bit i が voice i に一対一で対応すること
 *   - 全ビットを立てれば **完全な無音**（≠「小さくなる」）になること
 *   - 各 voice が実際に音へ寄与していること（= フィクスチャが4ch鳴っている）
 * これらは実機に持って行くまで気づけない種類の食い違い（ビット順の反転や
 * off-by-one）を機械的に潰す。player.c 側の配線そのものは
 * tests/ui_smoke.script（画面遷移）と実機検証でカバーする。
 *
 * SDL には依存せず、gme_static だけをリンクする（test_playlist.c と同じ形）。
 * フィクスチャは gen_fixture_gbs.c を GEN_FIXTURE_GBS_NO_MAIN 付きで
 * 一緒にコンパイルして再利用する。
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gme/gme.h>

#include "test_util.h"

/* gen_fixture_gbs.c（4ボイスすべてが鳴る合成GBSを書き出す） */
void write_synthetic_gbs(const char *path, int track_count);

#define SAMPLE_RATE 44100
/* 0.5秒分のステレオインタリーブ short 個数。gme_play() の count は
 * 「フレーム数」ではなく short の個数であることに注意（SPEC 5.1 落とし穴1）。 */
#define SAMPLE_COUNT (SAMPLE_RATE * 2 / 2)

static char g_gbs_path[512];

static void setup_fixture(void) {
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    char dir[400];
    snprintf(dir, sizeof(dir), "%s/mugbs_test_mute_XXXXXX", base);
    if (!mkdtemp(dir)) {
        fprintf(stderr, "mkdtemp failed\n");
        exit(1);
    }
    snprintf(g_gbs_path, sizeof(g_gbs_path), "%s/Mute.gbs", dir);
    write_synthetic_gbs(g_gbs_path, 1);
}

/* mask でミュートした状態の1トラック目を SAMPLE_COUNT 分レンダリングし、
 * out へ書く。失敗したら非0。 */
static int render(int mask, short *out) {
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(g_gbs_path, &emu, SAMPLE_RATE);
    if (err) {
        fprintf(stderr, "gme_open_file: %s\n", err);
        return 1;
    }
    /* gme_mute_voices() は require(sample_rate()) を持つため emu を開いた
     * 後でしか呼べない。mugbs も player_play_entry() で開いた直後に呼ぶ。 */
    gme_mute_voices(emu, mask);
    err = gme_start_track(emu, 0);
    if (err) {
        fprintf(stderr, "gme_start_track: %s\n", err);
        gme_delete(emu);
        return 1;
    }
    err = gme_play(emu, SAMPLE_COUNT, out);
    gme_delete(emu);
    if (err) {
        fprintf(stderr, "gme_play: %s\n", err);
        return 1;
    }
    return 0;
}

static double rms(const short *buf) {
    double sum = 0.0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sum += (double)buf[i] * (double)buf[i];
    }
    return sqrt(sum / SAMPLE_COUNT);
}

static int is_silent(const short *buf) {
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (buf[i] != 0) return 0;
    }
    return 1;
}

/* GBSのボイス構成が想定どおりであること。config.c の
 * [voices] mute_mask のクランプ上限 15 (=4bit) はこれに依存している。 */
static int test_voice_names(void) {
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(g_gbs_path, &emu, SAMPLE_RATE);
    CHECK(err == NULL);
    CHECK(gme_voice_count(emu) == 4);
    CHECK_STREQ(gme_voice_name(emu, 0), "Square 1");
    CHECK_STREQ(gme_voice_name(emu, 1), "Square 2");
    CHECK_STREQ(gme_voice_name(emu, 2), "Wave");
    CHECK_STREQ(gme_voice_name(emu, 3), "Noise");
    gme_delete(emu);
    return 0;
}

static int test_mute_masks(void) {
    short *all = malloc(sizeof(short) * SAMPLE_COUNT);
    short *buf = malloc(sizeof(short) * SAMPLE_COUNT);
    CHECK(all && buf);

    /* mask=0: 4ボイスすべてが鳴る。フィクスチャが無音だと以降の
     * 差分比較がすべて無意味になるので、まずここで担保する。 */
    CHECK(render(0, all) == 0);
    CHECK(!is_silent(all));
    double rms_all = rms(all);
    CHECK(rms_all > 1000.0);

    /* mask=15: 全ボイスミュート -> 完全な無音（振幅が小さいのではなく厳密に0）。 */
    CHECK(render(0x0F, buf) == 0);
    CHECK(is_silent(buf));

    /* 各ボイスを1つだけミュートすると、そのボイス分だけ出力が変わる。
     * bit i が voice i に対応していることの確認でもある。 */
    for (int i = 0; i < 4; i++) {
        CHECK(render(1 << i, buf) == 0);
        CHECK(!is_silent(buf));                 /* 残り3ボイスは鳴っている */
        CHECK(memcmp(buf, all, sizeof(short) * SAMPLE_COUNT) != 0);
        CHECK(rms(buf) < rms_all);              /* 音は減る方向にしか動かない */
    }

    /* 逆に1ボイスだけ残す(他3つをミュート)ても無音にはならない
     * = 4ボイスそれぞれが単独で音を出している。 */
    for (int i = 0; i < 4; i++) {
        CHECK(render(0x0F & ~(1 << i), buf) == 0);
        CHECK(!is_silent(buf));
    }

    free(all);
    free(buf);
    return 0;
}

int main(void) {
    setup_fixture();

    if (test_voice_names()) return 1;
    if (test_mute_masks()) return 1;

    printf("test_mute: すべて成功\n");
    return 0;
}

/* gen_fixture_spc.c - ヘッドレスUIスモークテスト(--ui-script)用の
 * 合成SPCフィクスチャ生成器。 (SPEC 10.3, Issue #43)
 *
 * 著作権上の理由からリポジトリに本物の.spcは含めないため、SPCヘッダと
 * 最小限のS-DSPレジスタ初期値・BRRサンプル1ブロックを自前で組み立てる。
 *
 * gen_fixture_gbs.c と違い、CPU(SPC700)コードは一切実行しない。
 * Snes_Spc::load_spc()はヘッダ末尾のDSPレジスタダンプ(dsp[128])を
 * dsp.load()でそのままDSPの初期状態として読み込み、その中のKON
 * (レジスタ0x4C)ビットは次のサンプル処理ステップでそのままキーオンとして
 * 処理される(vendor/game-music-emu/gme/Spc_Dsp.cpp のrun_until_()参照)。
 * つまりCPUを一切動かさずとも、ヘッダのDSPレジスタとRAM上のBRRサンプルだけで
 * 音を鳴らし続けられる。
 *
 * 鳴らす内容: ボイス0にloop+end両方が立った1ブロックだけのBRRサンプルを
 * 割り当て、無限ループで鳴らし続ける(GAINを直接モードの固定値にして
 * ADSRのアタック/ディケイに頼らない。P8, F-14 波形ビジュアライザが
 * 実際に振れることを目視確認できるように)。
 *
 * 使い方: gen_fixture_spc <output.spc> [len_secs]
 * len_secsを渡すとID666の曲長(秒、ASCII3桁)を書き込む(省略時は0=不明)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SPCファイル全体のサイズ = Snes_Spc::spc_min_file_size
 * (ヘッダ0x100 + RAM 0x10000 + DSPレジスタ0x80)。ヘッダより後ろの
 * unused/ipl_romは省いてよい(Snes_Spc::load_spc()はそこを読まない)。 */
#define SPC_FILE_SIZE 0x10180

/* DSPレジスタのアドレス(vendor/game-music-emu/gme/Spc_Dsp.h の enum と
 * 同じ値。ヘッダをincludeせず値だけ複製しているのは、この生成器を
 * libgme本体のビルドグラフに組み込みたくないため(gen_fixture_gbs.cと
 * 同じ方針))。 */
#define R_MVOLL 0x0C
#define R_MVOLR 0x1C
#define R_KON   0x4C
#define R_FLG   0x6C
#define R_DIR   0x5D
/* ボイス0(n=0)のレジスタは オフセットそのまま(n*0x10+offset, n=0)。 */
#define V0_VOLL   0x00
#define V0_VOLR   0x01
#define V0_PITCHL 0x02
#define V0_PITCHH 0x03
#define V0_SRCN   0x04
#define V0_ADSR0  0x05
#define V0_GAIN   0x07

static void put_le16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void write_synthetic_spc(const char *path, int len_secs) {
    unsigned char *buf = calloc(1, SPC_FILE_SIZE);
    if (!buf) {
        fprintf(stderr, "gen_fixture_spc: メモリ確保に失敗しました\n");
        exit(1);
    }

    /* --- ヘッダ (vendor/game-music-emu/gme/Spc_Emu.h の header_t) --- */
    memcpy(buf, "SNES-SPC700 Sound File Data v0.30\x1A\x1A", 35);
    buf[0x23] = 26; /* format: ID666タグあり(テキスト形式) */
    buf[0x24] = 30; /* version: 0.30 */
    /* pc/a/x/y/psw/sp (0x25-0x2B) はCPUを動かさないので0のままでよい。 */
    memcpy(buf + 0x2E, "UI Smoke Test", 13); /* song[32] */
    memcpy(buf + 0x4E, "muChip fixture", 14); /* game[32] */
    if (len_secs > 0 && len_secs < 1000) {
        char digits[4];
        snprintf(digits, sizeof(digits), "%03d", len_secs);
        memcpy(buf + 0xA9, digits, 3); /* len_secs[3] (ASCII10進) */
    }

    /* --- RAM (0x100起点、64KB) ---
     * DIRテーブル(ページ0x02=アドレス0x0200)のエントリ0: 開始/ループとも
     * 同じ1ブロックを指す(無限ループ)。 */
    unsigned char *ram = buf + 0x100;
    const unsigned dir_page = 0x02;
    const unsigned brr_addr = 0x0300;
    put_le16(ram + 0x0200, brr_addr); /* start */
    put_le16(ram + 0x0202, brr_addr); /* loop (start と同じ = 単一ブロックの無限ループ) */

    /* BRRブロック1個(9バイト): header + 16サンプル(8バイト)。
     * header: shift=12(最大に近い非クリップ値)・filter=0・loop=1・end=1。
     * データは +7/-8 のニブルを交互に並べ、ボイスの出力が確実に振れる
     * ようにする(正確な音程・音色は問わない。UIスモーク用の可聴信号)。 */
    ram[brr_addr + 0] = 0xC3; /* header: (12<<4)|(0<<2)|(1<<1)|1 */
    for (int i = 0; i < 8; i++) ram[brr_addr + 1 + i] = 0x78; /* nibble +7,-8 */

    /* --- DSPレジスタ初期値 (0x10100起点、128バイト) ---
     * ボイス0だけを使い、ADSRではなくGAIN直接モード(一定音量、
     * アタック/ディケイのランプ無し)で鳴らし続ける。 */
    unsigned char *dsp = buf + 0x10100;
    dsp[R_MVOLL] = 0x40;
    dsp[R_MVOLR] = 0x40;
    dsp[R_DIR] = (unsigned char)dir_page;
    dsp[R_FLG] = 0x00; /* soft reset/mute/echo disableいずれも立てない */
    dsp[V0_VOLL] = 0x50;
    dsp[V0_VOLR] = 0x50;
    put_le16(dsp + V0_PITCHL, 0x1000); /* 0x1000 = 原音同等速度 */
    dsp[V0_SRCN] = 0; /* DIRテーブルのエントリ0 */
    dsp[V0_ADSR0] = 0x00; /* ADSR無効化 -> GAINレジスタを使う */
    dsp[V0_GAIN] = 0x7F; /* 直接モード、最大固定音量 */
    dsp[R_KON] = 0x01; /* ボイス0をキーオン */

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "gen_fixture_spc: fopen(%s) failed\n", path);
        free(buf);
        exit(1);
    }
    fwrite(buf, 1, SPC_FILE_SIZE, f);
    fclose(f);
    free(buf);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "使い方: %s <output.spc> [len_secs]\n", argv[0]);
        return 1;
    }
    int len_secs = argc > 2 ? atoi(argv[2]) : 0;
    write_synthetic_spc(argv[1], len_secs);
    return 0;
}

/* gen_fixture_gbs.c - ヘッドレスUIスモークテスト(--ui-script)と
 * チャンネルミュートテスト(test_mute.c, T-10)用の合成GBSフィクスチャ生成器。
 * (SPEC 10.3)
 *
 * 著作権上の理由からリポジトリに本物の.gbsは含めないため、GBSヘッダと
 * ごく短いGame Boy機械語を自前で組み立てる。
 *
 * P8まではinit/playとも `RET` だけの「何も鳴らない」擬似GBSだったが、
 * それでは T-10(4chミュート: 該当チャンネルのみ無音になる)を機械的に
 * 検証できない。そこでinitルーチンに GB APU のレジスタ書き込み列を
 * 生成させ、**4ボイス(Square 1 / Square 2 / Wave / Noise)すべてが
 * 同時に鳴る**ようにした。各ボイスは長さカウンタもエンベロープも
 * 使わないため、トリガ後は永久に一定音量で鳴り続ける(ミュートの
 * 有無を差分で判定するのに都合が良い)。
 *
 * 使い方: gen_fixture_gbs <output.gbs> [track_count]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* initルーチンが書き込む APU レジスタ列。{ 0xFF00+low, value } を
 * そのまま `LD A,val` / `LDH (low),A` へ展開する。
 *
 * 順序に意味がある:
 *   1. NR52(電源)を最初に入れる。libgmeのGb_Apu::write_register()は
 *      電源OFF中にNR51を書くと osc.enabled を落とすため。
 *   2. NR50/NR51(音量・出力先)を各chのトリガより前に確定させる。
 *   3. 各chは NRx4 のbit7(トリガ)で発音開始。bit6(長さ有効)は立てない。 */
static const unsigned char APU_INIT[][2] = {
    { 0x26, 0x80 }, /* NR52: APU 電源ON */
    { 0x24, 0x77 }, /* NR50: 左右とも最大音量、VIN無し */
    { 0x25, 0xFF }, /* NR51: 4ch すべてを左右両方へ出力 */

    /* --- Ch1: Square 1 (約512Hz) --- */
    { 0x10, 0x00 }, /* NR10: スイープ無し */
    { 0x11, 0x80 }, /* NR11: デューティ50%、長さカウンタ未使用 */
    { 0x12, 0xF0 }, /* NR12: 初期音量15、エンベロープ無し */
    { 0x13, 0x00 }, /* NR13: 周波数下位8bit */
    { 0x14, 0x87 }, /* NR14: トリガ + 周波数上位3bit(=0x700) */

    /* --- Ch2: Square 2 (約256Hz。Ch1と別音程にして差分を出やすくする) --- */
    { 0x16, 0x80 }, /* NR21 */
    { 0x17, 0xF0 }, /* NR22 */
    { 0x18, 0x00 }, /* NR23 */
    { 0x19, 0x86 }, /* NR24: トリガ + 周波数上位(=0x600) */

    /* --- Ch3: Wave --- */
    { 0x1A, 0x00 }, /* NR30: 波形RAMを書く前にDACを落とす(実機の作法) */
    /* 波形RAM 0xFF30-0xFF3F。1バイト=4bitサンプル2個なので、
     * 0xFF×8 + 0x00×8 で32サンプル分の矩形波になる。 */
    { 0x30, 0xFF }, { 0x31, 0xFF }, { 0x32, 0xFF }, { 0x33, 0xFF },
    { 0x34, 0xFF }, { 0x35, 0xFF }, { 0x36, 0xFF }, { 0x37, 0xFF },
    { 0x38, 0x00 }, { 0x39, 0x00 }, { 0x3A, 0x00 }, { 0x3B, 0x00 },
    { 0x3C, 0x00 }, { 0x3D, 0x00 }, { 0x3E, 0x00 }, { 0x3F, 0x00 },
    { 0x1A, 0x80 }, /* NR30: DAC ON */
    { 0x1B, 0x00 }, /* NR31: 長さカウンタ未使用 */
    { 0x1C, 0x20 }, /* NR32: 出力レベル100% */
    { 0x1D, 0x00 }, /* NR33 */
    { 0x1E, 0x85 }, /* NR34: トリガ + 周波数上位(=0x500) */

    /* --- Ch4: Noise --- */
    { 0x20, 0x00 }, /* NR41: 長さカウンタ未使用 */
    { 0x21, 0xF0 }, /* NR42: 初期音量15、エンベロープ無し */
    { 0x22, 0x50 }, /* NR43: クロックシフト/分周比 */
    { 0x23, 0x80 }, /* NR44: トリガ */
};
#define APU_INIT_COUNT ((int)(sizeof(APU_INIT) / sizeof(APU_INIT[0])))

/* LD A,n (2バイト) + LDH (n),A (2バイト) */
#define BYTES_PER_WRITE 4

/* static にしないのは、tests/test_mute.c が GEN_FIXTURE_GBS_NO_MAIN 付きで
 * この .c をそのままコンパイル・リンクしてフィクスチャ生成を再利用するため。 */
void write_synthetic_gbs(const char *path, int track_count) {
    /* initルーチン本体 + 末尾のRET、その後ろにplayルーチンのRET。 */
    unsigned char code[APU_INIT_COUNT * BYTES_PER_WRITE + 2];
    int n = 0;
    for (int i = 0; i < APU_INIT_COUNT; i++) {
        code[n++] = 0x3E;              /* LD A, d8   */
        code[n++] = APU_INIT[i][1];
        code[n++] = 0xE0;              /* LDH (a8),A */
        code[n++] = APU_INIT[i][0];
    }
    code[n++] = 0xC9;                  /* init: RET */
    int play_offset = n;
    code[n++] = 0xC9;                  /* play: RET (発音はinitで完結する) */

    unsigned load = 0x400;
    unsigned init = load;              /* initはコード先頭 */
    unsigned play = load + (unsigned)play_offset;

    unsigned char hdr[0x70];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'G'; hdr[1] = 'B'; hdr[2] = 'S'; hdr[3] = 1;
    hdr[4] = (unsigned char)track_count;
    hdr[5] = 1; /* first track */
    hdr[6] = load & 0xFF; hdr[7] = (load >> 8) & 0xFF;
    hdr[8] = init & 0xFF; hdr[9] = (init >> 8) & 0xFF;
    hdr[10] = play & 0xFF; hdr[11] = (play >> 8) & 0xFF;
    hdr[12] = 0xFE; hdr[13] = 0xFF; /* SP=0xFFFE */
    memcpy(hdr + 0x10, "UI Smoke Test", 13);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "gen_fixture_gbs: fopen(%s) failed\n", path);
        exit(1);
    }
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(code, 1, (size_t)n, f);
    fclose(f);
}

#ifndef GEN_FIXTURE_GBS_NO_MAIN
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "使い方: %s <output.gbs> [track_count]\n", argv[0]);
        return 1;
    }
    int track_count = argc > 2 ? atoi(argv[2]) : 2;
    write_synthetic_gbs(argv[1], track_count);
    return 0;
}
#endif

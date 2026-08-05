/* gen_fixture_gbs.c - ヘッドレスUIスモークテスト(--ui-script)用の
 * 合成GBSフィクスチャ生成器。 (SPEC 10.3)
 *
 * test_playlist.c の write_synthetic_gbs() と同じ最小実装(ヘッダのみ
 * 有効な擬似GBS)をビルド時にCMakeのカスタムコマンドから呼べるように
 * したもの。著作権上の理由からリポジトリに本物の.gbsは含めない。
 *
 * 使い方: gen_fixture_gbs <output.gbs> [track_count]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_synthetic_gbs(const char *path, int track_count) {
    unsigned char hdr[0x70];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'G'; hdr[1] = 'B'; hdr[2] = 'S'; hdr[3] = 1;
    hdr[4] = (unsigned char)track_count;
    hdr[5] = 1; /* first track */
    unsigned load = 0x400, init = 0x400;
    hdr[6] = load & 0xFF; hdr[7] = (load >> 8) & 0xFF;
    hdr[8] = init & 0xFF; hdr[9] = (init >> 8) & 0xFF;
    unsigned play = init + 1; /* init直後(下記コードのRET単体)を指す */
    hdr[10] = play & 0xFF; hdr[11] = (play >> 8) & 0xFF;
    hdr[12] = 0xFE; hdr[13] = 0xFF; /* SP=0xFFFE */
    memcpy(hdr + 0x10, "UI Smoke Test", 13);

    unsigned char code[2] = { 0xC9, 0xC9 }; /* init: RET / play: RET */

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "gen_fixture_gbs: fopen(%s) failed\n", path);
        exit(1);
    }
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(code, 1, sizeof(code), f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "使い方: %s <output.gbs> [track_count]\n", argv[0]);
        return 1;
    }
    int track_count = argc > 2 ? atoi(argv[2]) : 2;
    write_synthetic_gbs(argv[1], track_count);
    return 0;
}

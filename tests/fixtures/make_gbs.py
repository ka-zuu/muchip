#!/usr/bin/env python3
"""make_gbs.py - 合成GBSファイル生成器。 (SPEC 10.3)

著作権上の理由から本物の .gbs はリポジトリに含めない。代わりに、
ヘッダのみ有効で、init時にGB APUのチャンネル1へ実際に音を書き込む
最小限のコード(RET一発)を持つ擬似GBSファイルを合成する。

m3uパーサ自体のテストにはこのファイルの音声内容は関係しない
(トラック数さえ足りていればよい)ので、CTestからは主に
「トラック数だけ多いGBS」を要求するテスト(16進トラック番号など)に使う。

使い方:
    python3 make_gbs.py <output.gbs> [track_count] [title]
"""
import struct
import sys


def build_gbs(track_count=1, first_track=1, title=b"Test", author=b"Claude",
              copyright=b"2026 Test"):
    def pad(b, n):
        return b[:n] + b"\0" * (n - len(b))

    load = 0x400
    init = 0x400

    def ld_a(n):
        return bytes([0x3E, n])

    def ld_nn_a(addr):
        return bytes([0xEA, addr & 0xFF, (addr >> 8) & 0xFF])

    code = b""
    code += ld_a(0x80) + ld_nn_a(0xFF26)  # NR52 サウンド有効化
    code += ld_a(0x77) + ld_nn_a(0xFF24)  # NR50 音量最大
    code += ld_a(0x11) + ld_nn_a(0xFF25)  # NR51 ch1を両スピーカへ
    code += ld_a(0x00) + ld_nn_a(0xFF10)  # NR10 スイープ無効
    code += ld_a(0x80) + ld_nn_a(0xFF11)  # NR11 デューティ50%
    code += ld_a(0xF0) + ld_nn_a(0xFF12)  # NR12 音量15、エンベロープ無効
    code += ld_a(0xD6) + ld_nn_a(0xFF13)  # NR13 周波数下位 (約440Hz)
    code += ld_a(0x86) + ld_nn_a(0xFF14)  # NR14 トリガ+周波数上位
    code += bytes([0xC9])                 # RET

    play = load + len(code)
    code += bytes([0xC9])                 # play: RET (直前の音を維持するだけ)

    hdr = b"GBS" + bytes([1])
    hdr += bytes([track_count & 0xFF])
    hdr += bytes([first_track & 0xFF])
    hdr += struct.pack("<H", load)
    hdr += struct.pack("<H", init)
    hdr += struct.pack("<H", play)
    hdr += struct.pack("<H", 0xFFFE)  # SP
    hdr += bytes([0])  # timer modulo
    hdr += bytes([0])  # timer control
    hdr += pad(title, 32) + pad(author, 32) + pad(copyright, 32)
    assert len(hdr) == 0x70

    return hdr + code


def main():
    if len(sys.argv) < 2:
        print(f"使い方: {sys.argv[0]} <output.gbs> [track_count] [title]", file=sys.stderr)
        return 1

    out_path = sys.argv[1]
    track_count = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    title = sys.argv[3].encode("ascii", "replace") if len(sys.argv) > 3 else b"Test"

    data = build_gbs(track_count=track_count, title=title)
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"wrote {out_path}: {len(data)} bytes, {track_count} tracks")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/bin/sh
# 화면 미리보기: 기기와 똑같은 그리기 코드(src/logic/screen_draw)로 모든 장면을 그림 파일로 만든다.
#   test/host/preview.sh [결과.png]      (기본: docs/design/screens.png)
# 기기에 올리기 전에 화면이 어떻게 나올지 이 그림으로 확인한다(D22: 미리보기 = 기기 픽셀).
# 같은 폴더에 mascots.png(마스코트 다섯 x 표정 여덟, D26)도 함께 만든다.
set -e
cd "$(dirname "$0")/../.."
out="${1:-docs/design/screens.png}"
if [ -d /Library/Developer/CommandLineTools/usr/bin ]; then
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi
mkdir -p .pio/host "$(dirname "$out")"
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I src \
    test/host/preview.cpp src/logic/pixel_art.cpp src/logic/screen_draw.cpp -o .pio/host/preview
.pio/host/preview .pio/host/preview.ppm
.pio/host/preview --mascots .pio/host/mascots.ppm
python3 - "$out" "$(dirname "$out")/mascots.png" <<'PY'
import struct, sys, zlib
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
def to_png(ppm, out):
    data = open(ppm, "rb").read()
    # P6 머리: "P6\n너비 높이\n255\n"
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(out, "wb").write(png)
    print("저장:", out, f"({w}x{h})")
to_png(".pio/host/preview.ppm", sys.argv[1])
to_png(".pio/host/mascots.ppm", sys.argv[2])
PY

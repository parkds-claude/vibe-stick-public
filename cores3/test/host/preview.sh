#!/bin/sh
# CoreS3 화면 미리보기: 기기와 똑같은 그리기 코드(cores3/logic/touch_draw)와 같은 조각 그림(art_data)으로 모든 장면을 그림으로 만든다.
#   cores3/test/host/preview.sh [결과.png]      (기본: docs/design/cores3-screens.png)
# 기기에 올리기 전에 화면이 어떻게 나올지 이 그림으로 확인한다(미리보기 = 기기 픽셀).
# 같은 폴더에 cores3-layout.png(대기 화면 한 장, 3배)도 함께 만든다.
set -e
cd "$(dirname "$0")/../../.."
out="${1:-docs/design/cores3-screens.png}"
if [ -d /Library/Developer/CommandLineTools/usr/bin ]; then
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi
mkdir -p .pio/host "$(dirname "$out")"
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I src -I cores3/logic \
    cores3/test/host/preview.cpp src/logic/pixel_art.cpp cores3/logic/touch_draw.cpp cores3/logic/art_data.cpp \
    -o .pio/host/touch_preview
.pio/host/touch_preview .pio/host/touch_preview.ppm
.pio/host/touch_preview --one .pio/host/touch_layout.ppm
python3 - "$out" "$(dirname "$out")/cores3-layout.png" <<'PY'
import struct, sys, zlib
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
def to_png(ppm, out):
    data = open(ppm, "rb").read()
    parts = data.split(b"\n", 3)  # "P6\n너비 높이\n255\n"
    w, h = map(int, parts[1].split())
    px = parts[3]
    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(out, "wb").write(png)
    print("저장:", out, f"({w}x{h})")
to_png(".pio/host/touch_preview.ppm", sys.argv[1])
to_png(".pio/host/touch_layout.ppm", sys.argv[2])
PY

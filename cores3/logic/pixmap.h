#pragma once
#include <stdint.h>

#include "art_data.h"

// 메모리 속의 작은 그림(RGB565)과, 그 위에 조각을 얹는 일. 하드웨어를 모른다(컴퓨터에서 시험 가능).
// 보는 곳(캐릭터·상태 막대)은 여기서 합친 뒤 통째로 화면에 보낸다 — 설계서 D57.
namespace gfx {

struct Pixmap {
  uint16_t* px;
  int16_t   w, h;
};

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 두 색을 섞는다. a = 0 이면 bg, 255 면 fg.
inline uint16_t mix(uint16_t bg, uint16_t fg, int a) {
  if (a <= 0) return bg;
  if (a >= 255) return fg;
  const int r = ((bg >> 11) & 31) * (255 - a) + ((fg >> 11) & 31) * a;
  const int g = ((bg >> 5) & 63) * (255 - a) + ((fg >> 5) & 63) * a;
  const int b = (bg & 31) * (255 - a) + (fg & 31) * a;
  return (uint16_t)((((r + 127) / 255) << 11) | (((g + 127) / 255) << 5) | ((b + 127) / 255));
}

// src 의 (sx, sy) 부터 dst 크기만큼 옮겨 온다(dst 를 통째로 덮는다). src 밖은 건드리지 않는다.
inline void copyFrom(Pixmap& dst, const art::Image& src, int sx, int sy) {
  for (int y = 0; y < dst.h; y++) {
    if (sy + y < 0 || sy + y >= src.h) continue;
    for (int x = 0; x < dst.w; x++) {
      if (sx + x < 0 || sx + x >= src.w) continue;
      dst.px[y * dst.w + x] = src.px[(sy + y) * src.w + sx + x];
    }
  }
}

// 사각형을 한 색으로. dst 밖은 잘라 낸다.
inline void fill(Pixmap& dst, int x, int y, int w, int h, uint16_t color) {
  for (int yy = (y < 0 ? 0 : y); yy < y + h && yy < dst.h; yy++) {
    for (int xx = (x < 0 ? 0 : x); xx < x + w && xx < dst.w; xx++) dst.px[yy * dst.w + xx] = color;
  }
}

// "덮는 정도" 그림을 color 로 찍는다. alpha: 전체를 얼마나 옅게(255 = 그대로).
// clipW: 그림의 왼쪽 clipW 열만 찍는다(배터리 채움처럼 일부만 보일 때). 음수면 전부.
inline void blend(Pixmap& dst, int x, int y, const art::Mask& m, uint16_t color, int alpha = 255, int clipW = -1) {
  const int w = (clipW < 0 || clipW > m.w) ? m.w : clipW;
  for (int r = 0; r < m.h; r++) {
    const int yy = y + r;
    if (yy < 0 || yy >= dst.h) continue;
    for (int c = 0; c < w; c++) {
      const int xx = x + c;
      if (xx < 0 || xx >= dst.w) continue;
      const int a = m.a[r * m.w + c] * alpha / 255;
      if (a > 0) dst.px[yy * dst.w + xx] = mix(dst.px[yy * dst.w + xx], color, a);
    }
  }
}

}  // namespace gfx

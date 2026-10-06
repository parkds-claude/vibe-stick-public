#include "screen_draw.h"

using pixelart::Cell;
using pixelart::Eyes;
using pixelart::Glyph;
using pixelart::Mascot;
using pixelart::Mouth;
using pixelart::Rgb;

namespace screen {

const char* const NO_STICK_TEXT = "NO STICK";

namespace {

const SceneSpec SPECS[SCENE_COUNT] = {
    /* Ready      */ {"HI!",  nullptr, color::CREAM,  Eyes::Dot,   Mouth::None},
    /* Pairing    */ {"PAIR", "ME",    color::LIME,   Eyes::Heart, Mouth::None},
    /* Searching  */ {"MAC?", nullptr, color::SKY,    Eyes::Sleep, Mouth::None},
    /* Connecting */ {"WAIT", nullptr, color::SKY,    Eyes::Dot,   Mouth::Flat},
    /* Talk       */ {"TALK", nullptr, color::PINK,   Eyes::Wide,  Mouth::Open},
    /* Ok         */ {"OK!",  nullptr, color::YELLOW, Eyes::Happy, Mouth::None},
    /* Nope       */ {"NOPE", nullptr, color::CORAL,  Eyes::Cross, Mouth::Flat},
    /* Up         */ {"UP",   nullptr, color::MINT,   Eyes::Up,    Mouth::None},
    /* Down       */ {"DOWN", nullptr, color::MINT,   Eyes::Down,  Mouth::None},
    /* Next       */ {"NEXT", nullptr, color::LILAC,  Eyes::Right, Mouth::None},
    /* Back       */ {"BACK", nullptr, color::LILAC,  Eyes::Left,  Mouth::None},
    /* NoLink     */ {"NO",   "LINK",  color::GREY,   Eyes::Sad,   Mouth::Flat},
    /* Bye        */ {"BYE!", nullptr, color::LIME,   Eyes::Happy, Mouth::Open},
    /* Oops       */ {"OOPS", nullptr, color::GREY,   Eyes::Cross, Mouth::Flat},
};

// 바탕색보다 조금 어두운 색(점무늬용)
uint16_t darker(uint16_t c) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = r > 3 ? r - 3 : 0;
  g = g > 7 ? g - 7 : 0;
  b = b > 3 ? b - 3 : 0;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// 글자 한 줄을 그린다. 가로로 이어진 점은 사각형 하나로 묶는다(그리는 횟수를 줄인다).
void drawText(Canvas& c, const char* s, int x, int y, int scale, uint16_t col, bool bold) {
  for (const char* p = s; *p; ++p) {
    const Glyph g = bold ? pixelart::boldGlyph(*p) : pixelart::smallGlyph(*p);
    for (int r = 0; r < g.h; r++) {
      int k = 0;
      while (k < g.w) {
        if (!(g.rows[r] & (0x80 >> k))) {
          ++k;
          continue;
        }
        int run = 1;
        while (k + run < g.w && (g.rows[r] & (0x80 >> (k + run)))) ++run;
        c.fillRect(x + k * scale, y + r * scale, run * scale, scale, col);
        k += run;
      }
    }
    x += (g.w + 1) * scale;
  }
}

void drawTextCentered(Canvas& c, const char* s, int cx, int y, int scale, uint16_t col, bool bold) {
  const int w = (bold ? pixelart::boldWidth(s) : pixelart::smallWidth(s)) * scale;
  drawText(c, s, cx - w / 2, y, scale, col, bold);
}

void drawBackground(Canvas& c, uint16_t bg) {
  c.fillRect(0, BAR_H, W, H - BAR_H, bg);
  const uint16_t dot = darker(bg);
  const int step = 12;
  for (int y = step * 2; y < H; y += step) {
    for (int x = ((y / step) % 2) * (step / 2); x + 3 <= W; x += step) {
      if (y + 3 <= H) c.fillRect(x, y, 3, 3, dot);
    }
  }
}

void drawBattery(Canvas& c, int pct) {
  const int x = 108, y = 5;
  const uint16_t col = color::WHITE;
  c.fillRect(x, y, 20, 1, col);
  c.fillRect(x, y + 9, 20, 1, col);
  c.fillRect(x, y, 1, 10, col);
  c.fillRect(x + 19, y, 1, 10, col);
  c.fillRect(x + 20, y + 3, 2, 4, col);  // 꼭지
  const int fill = batteryFill(pct);
  if (fill > 0) c.fillRect(x + 2, y + 2, fill, 6, batteryLow(pct) ? color::RED : col);
}

void drawStatusBar(Canvas& c, LinkState link, int batteryPct) {
  c.fillRect(0, 0, W, BAR_H, color::INK);
  drawText(c, linkWord(link), 6, 7, 1, link == LinkState::Ready ? color::GREEN : color::YELLOW,
           false);
  drawBattery(c, batteryPct);
}

uint16_t to565(Rgb c) { return rgb565(c.r, c.g, c.b); }
bool sameColor(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

void drawFace(Canvas& c, const Mascot& m, const Cell* cells, int n) {
  const int s = MASCOT_SCALE, x0 = mascotX(m), y0 = mascotY(m);
  for (int i = 0; i < n; i++) {
    c.fillRect(x0 + cells[i].col * s, y0 + (cells[i].row + m.faceDy) * s, cells[i].w * s,
               cells[i].h * s, to565(m.face));
  }
}

// 가로로 이어진 칸은 사각형 하나로 묶는다(그리는 횟수를 줄인다).
void drawMascot(Canvas& c, const Mascot& m, Eyes eyes, Mouth mouth) {
  const int s = MASCOT_SCALE, x0 = mascotX(m), y0 = mascotY(m);
  const int edge = 2;  // 스티커 같은 검은 테두리의 두께
  Rgb col{}, next{};
  for (int r = 0; r < m.h; r++) {  // 테두리: 빈칸이 아닌 칸의 이어짐
    int k = 0;
    while (k < m.w) {
      if (!pixelart::mascotCell(m, k, r, col)) {
        ++k;
        continue;
      }
      int run = 1;
      while (pixelart::mascotCell(m, k + run, r, next)) ++run;
      c.fillRect(x0 + k * s - edge, y0 + r * s - edge, run * s + 2 * edge, s + 2 * edge, color::INK);
      k += run;
    }
  }
  for (int r = 0; r < m.h; r++) {  // 몸: 같은 색 칸의 이어짐
    int k = 0;
    while (k < m.w) {
      if (!pixelart::mascotCell(m, k, r, col)) {
        ++k;
        continue;
      }
      int run = 1;
      while (pixelart::mascotCell(m, k + run, r, next) && sameColor(next, col)) ++run;
      c.fillRect(x0 + k * s, y0 + r * s, run * s, s, to565(col));
      k += run;
    }
  }
  const Cell* cells = nullptr;
  int n = pixelart::eyeCells(eyes, cells);
  drawFace(c, m, cells, n);
  n = pixelart::mouthCells(mouth, cells);
  drawFace(c, m, cells, n);
}

void drawBubble(Canvas& c, const SceneSpec& spec) {
  if (!spec.line1) return;  // 표에 빠진 장면(있어서는 안 된다)
  const int x = BUBBLE_X, y = BUBBLE_Y, w = BUBBLE_W, h = BUBBLE_H;
  c.fillRect(x - 3, y - 3, w + 6, h + 6, color::INK);
  c.fillRect(x, y, w, h, color::WHITE);
  // 말풍선 꼬리(마스코트 쪽으로)
  for (int k = 0; k < 10; k++) c.fillRect(x + 30 + k, y - 3 - (10 - k), 3, 3, color::INK);
  for (int k = 0; k < 8; k++) c.fillRect(x + 33 + k, y - (8 - k), 1, 8 - k, color::WHITE);

  const int scale = bubbleScale(spec);
  const int lines = spec.line2 ? 2 : 1;
  const int lineH = 8 * scale, gap = scale;
  int ty = y + (h - (lines * lineH + (lines - 1) * gap)) / 2;
  drawTextCentered(c, spec.line1, x + w / 2, ty, scale, color::INK, true);
  if (spec.line2) drawTextCentered(c, spec.line2, x + w / 2, ty + lineH + gap, scale, color::INK, true);
}

// 정해진 줄 범위 밖은 버리는 캔버스(띠 하나만 그릴 때)
class ClipCanvas : public Canvas {
 public:
  ClipCanvas(Canvas& inner, int y0, int y1) : inner_(inner), y0_(y0), y1_(y1) {}
  void fillRect(int x, int y, int w, int h, uint16_t col) override {
    const int top = y < y0_ ? y0_ : y;
    const int bottom = (y + h) > y1_ ? y1_ : (y + h);
    if (bottom > top) inner_.fillRect(x, top, w, bottom - top, col);
  }

 private:
  Canvas& inner_;
  const int y0_, y1_;
};

}  // namespace

int batteryFill(int pct) {
  if (pct < 0) return 0;
  if (pct > 100) pct = 100;
  int fill = 16 * pct / 100;
  if (batteryLow(pct) && fill < 3) fill = 3;
  return fill;
}

const SceneSpec& specFor(Scene s) { return SPECS[(int)s < SCENE_COUNT ? (int)s : 0]; }

const char* linkWord(LinkState l) {
  switch (l) {
    case LinkState::Pairing:    return "PAIR";
    case LinkState::Searching:  return "SEARCH";
    case LinkState::Connecting: return "LINKING";
    case LinkState::Ready:      return "READY";
  }
  return "";
}

int bubbleScale(const SceneSpec& spec) {
  const int lines = spec.line2 ? 2 : 1;
  if (!spec.line1) return 1;
  int widest = pixelart::boldWidth(spec.line1);
  if (spec.line2 && pixelart::boldWidth(spec.line2) > widest) widest = pixelart::boldWidth(spec.line2);
  int scale = (lines == 1) ? 4 : 3;
  while (scale > 1 && (widest * scale > BUBBLE_W - 2 * BUBBLE_PAD ||
                       lines * 8 * scale + (lines - 1) * scale > BUBBLE_H - 2 * BUBBLE_PAD)) {
    --scale;
  }
  return scale;
}

void drawFrame(Canvas& c, const Frame& f) { drawFrame(c, f, pixelart::mascot()); }

void drawFrame(Canvas& c, const Frame& f, const Mascot& m) {
  const SceneSpec& spec = specFor(f.scene);
  drawBackground(c, spec.bg);
  drawStatusBar(c, f.link, f.batteryPct);
  drawMascot(c, m, spec.eyes, spec.mouth);
  drawBubble(c, spec);
  if (f.noStick) {
    c.fillRect(0, STRIP_Y, W, STRIP_H, color::INK);
    drawTextCentered(c, NO_STICK_TEXT, W / 2, STRIP_Y + 7, 1, color::YELLOW, false);
  }
}

void drawFrameBand(Canvas& c, const Frame& f, int band) {
  if (band < 0 || band >= BANDS) return;
  ClipCanvas clip(c, band * H / BANDS, (band + 1) * H / BANDS);
  drawFrame(clip, f);
}

}  // namespace screen

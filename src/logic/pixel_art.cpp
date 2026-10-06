#include "pixel_art.h"

namespace pixelart {

namespace {

// 바탕 글자: 5 x 7. 굵은 글자와 작은 글자는 여기서 만들어 낸다.
const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?%.-";
const uint8_t FONT_5X7[][7] = {
    {0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88},  // A
    {0xF0, 0x88, 0x88, 0xF0, 0x88, 0x88, 0xF0},  // B
    {0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70},  // C
    {0xF0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xF0},  // D
    {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8},  // E
    {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80},  // F
    {0x70, 0x88, 0x80, 0xB8, 0x88, 0x88, 0x70},  // G
    {0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88},  // H
    {0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF8},  // I
    {0x38, 0x10, 0x10, 0x10, 0x10, 0x90, 0x60},  // J
    {0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88},  // K
    {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8},  // L
    {0x88, 0xD8, 0xA8, 0xA8, 0x88, 0x88, 0x88},  // M
    {0x88, 0xC8, 0xA8, 0x98, 0x88, 0x88, 0x88},  // N
    {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},  // O
    {0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80},  // P
    {0x70, 0x88, 0x88, 0x88, 0xA8, 0x90, 0x68},  // Q
    {0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88},  // R
    {0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xF0},  // S
    {0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20},  // T
    {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},  // U
    {0x88, 0x88, 0x88, 0x88, 0x88, 0x50, 0x20},  // V
    {0x88, 0x88, 0x88, 0xA8, 0xA8, 0xD8, 0x88},  // W
    {0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88},  // X
    {0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20},  // Y
    {0xF8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8},  // Z
    {0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70},  // 0
    {0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70},  // 1
    {0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8},  // 2
    {0xF0, 0x08, 0x08, 0x70, 0x08, 0x08, 0xF0},  // 3
    {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10},  // 4
    {0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70},  // 5
    {0x70, 0x80, 0x80, 0xF0, 0x88, 0x88, 0x70},  // 6
    {0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40},  // 7
    {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70},  // 8
    {0x70, 0x88, 0x88, 0x78, 0x08, 0x08, 0x70},  // 9
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20},  // !
    {0x70, 0x88, 0x08, 0x10, 0x20, 0x00, 0x20},  // ?
    {0xC8, 0xD0, 0x10, 0x20, 0x40, 0x58, 0x98},  // %
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60},  // .
    {0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00},  // -
};

static_assert(sizeof(FONT_5X7) / sizeof(FONT_5X7[0]) == sizeof(FONT_CHARS) - 1,
              "글자 표의 줄 수가 FONT_CHARS 의 글자 수와 다르다");

int indexOf(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (int i = 0; FONT_CHARS[i]; i++) {
    if (FONT_CHARS[i] == c) return i;
  }
  return -1;
}

const uint8_t* base(char c) {
  int i = indexOf(c);
  if (i < 0) i = indexOf('?');
  return FONT_5X7[i];
}

// 굵게 하면 뭉개지거나 다른 글자로 보이는 것은 손으로 그린 모양을 쓴다.
struct Override {
  char  c;
  Glyph g;
};
const Override BOLD_OVERRIDES[] = {
    // ##...##
    // ###.###
    // #######
    // ##.#.##
    // ##.#.##
    // ##...##  (아래 두 줄 같음)
    {'M', {7, 8, {0xC6, 0xEE, 0xFE, 0xD6, 0xD6, 0xC6, 0xC6, 0xC6}}},
    // ##...##  (위 세 줄 같음)
    // ##.#.##
    // ##.#.##
    // #######
    // ###.###
    // ##...##
    {'W', {7, 8, {0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0xFE, 0xEE, 0xC6}}},
    // ##..##
    // ###.##
    // ###.##
    // ######
    // ##.###
    // ##.###
    // ##..##  (아래 두 줄 같음)
    {'N', {6, 8, {0xCC, 0xEC, 0xEC, 0xFC, 0xDC, 0xDC, 0xCC, 0xCC}}},
    // ##..##
    // ##..##
    // .####.
    // ..##..
    // ..##..
    // .####.
    // ##..##
    // ##..##
    {'X', {6, 8, {0xCC, 0xCC, 0x78, 0x30, 0x30, 0x78, 0xCC, 0xCC}}},
    // ##..##
    // ##.##.
    // ####..
    // ###...
    // ####..
    // ##.##.
    // ##..##
    // ##..##
    {'K', {6, 8, {0xCC, 0xD8, 0xF0, 0xE0, 0xF0, 0xD8, 0xCC, 0xCC}}},
    // 느낌표: 줄기와 점 사이를 띄워야 I 로 보이지 않는다
    {'!', {2, 8, {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0xC0, 0xC0}}},
};

inline uint8_t widen(uint8_t row) { return (uint8_t)(row | (row >> 1)); }

int widthOf(const char* s, bool bold) {
  int w = 0;
  for (const char* p = s; p && *p; ++p) {
    if (p != s) w += 1;
    w += bold ? boldGlyph(*p).w : smallGlyph(*p).w;
  }
  return w;
}

// ---- 마스코트 (D26) ----
// 몸은 글자 격자로 그린다. '.' 은 빈칸이고 그 밖의 글자는 바로 아래 색 표의 색이다.
// 표정 표(EYES_*, MOUTH_*)는 18칸 너비의 얼굴을 기준으로 그려져 있어, 마스코트마다
// 얼굴을 몇 칸 내릴지(faceDy)만 정하면 눈 11종·입 3종을 그대로 쓴다.
// 마스코트를 더할 때: 격자와 색 표를 쓰고 MASCOTS 와 MascotId(pixel_art.h)에 같은 순서로 넣는다.
//   줄 너비·색 표·표정이 몸 위에 놓이는지·자리를 벗어나지 않는지는 test_screen.cpp 가 본다.

// 스틱이: 오락실 조이스틱
const char* const STICK_ROWS[] = {
    ".....########.....",
    "...############...",
    "..##############..",
    "..##############..",
    "..##############..",
    "..##############..",
    "..##############..",
    "...############...",
    ".....########.....",
    "........kk........",
    "........kk........",
    ".....kkkkkkkk.....",
    "..kkkkkkkkkkkkkk..",
};
const Swatch STICK_SWATCHES[] = {{'#', {0xF2, 0x46, 0x3A}}, {'k', {0x3A, 0x3A, 0x38}}};

// 키캡이: 키보드에서 빠져나온 키 하나
const char* const KEYCAP_ROWS[] = {
    "...++++++++++++...",
    "..+############+..",
    "..+############+..",
    "..+############+..",
    "..+############+..",
    "..+############+..",
    "..+############+..",
    "..+############+..",
    ".++++++++++++++++.",
    "++++++++++++++++++",
    "++++++++++++++++++",
};
const Swatch KEYCAP_SWATCHES[] = {{'#', {0xFF, 0xD8, 0x4D}}, {'+', {0xE3, 0xA0, 0x08}}};

// 까망이: 검은 고양이(귀 안쪽과 코는 분홍, 눈은 노랑)
const char* const CAT_ROWS[] = {
    "..##..........##..",
    "..#p#........#p#..",
    "..#pp#......#pp#..",
    "..##############..",
    ".################.",
    ".################.",
    ".################.",
    ".################.",
    ".#######pp#######.",
    "..##############..",
    "...############...",
};
const Swatch CAT_SWATCHES[] = {{'#', {0x2A, 0x2A, 0x28}}, {'p', {0xFF, 0x8F, 0xB3}}};

// 삐삐: 안테나 달린 로봇(얼굴은 흰 판)
const char* const ROBOT_ROWS[] = {
    "........aa........",
    "........kk........",
    "..##############..",
    ".##++++++++++++##.",
    ".##++++++++++++##.",
    "k##++++++++++++##k",
    "k##++++++++++++##k",
    ".##++++++++++++##.",
    ".##++++++++++++##.",
    ".################.",
    "..##############..",
    "....###....###....",
};
const Swatch ROBOT_SWATCHES[] = {{'#', {0x3D, 0x7B, 0xFF}},
                                 {'+', {0xFF, 0xFF, 0xFF}},
                                 {'k', {0x8C, 0x91, 0x99}},
                                 {'a', {0xFF, 0xD8, 0x4D}}};

// 새싹이: 머리에 새싹 난 찹쌀떡(볼은 분홍)
const char* const SPROUT_ROWS[] = {
    "......gg..gg......",
    ".......gggg.......",
    "........gg........",
    ".....########.....",
    "...############...",
    "..##############..",
    ".################.",
    ".################.",
    ".################.",
    ".#oo##########oo#.",
    "..##############..",
    "...############...",
};
const Swatch SPROUT_SWATCHES[] = {{'#', {0xFF, 0xFF, 0xFF}},
                                  {'g', {0x3D, 0xBE, 0x6B}},
                                  {'o', {0xFF, 0x9E, 0xB8}}};

// 클로드: Claude Code 첫 화면의 캐릭터를 점으로 옮긴 것(옛 D23 의 18x10 비트 그림을 글자 격자로 다시 적음).
// Anthropic 의 캐릭터를 옮긴 비공식 팬아트(D29) — 승인받은 것이 아니며 권리자가 요청하면 이 격자와
// MASCOTS·MascotId 의 "claude" 줄을 지운다(NOTICE 참고). 표정 표는 원래 이 얼굴 기준으로 그려졌으므로 faceDy 는 0.
const char* const CLAUDE_ROWS[] = {
    "...############...",
    "...############...",
    "...############...",
    "...############...",
    ".################.",
    ".################.",
    "...############...",
    "...############...",
    "....#.#....#.#....",
    "....#.#....#.#....",
};
const Swatch CLAUDE_SWATCHES[] = {{'#', {0xD9, 0x77, 0x57}}};

// 줄 수와 색 수는 배열 크기에서 얻는다(따로 적어 어긋나는 일을 막는다).
template <int R, int S>
constexpr Mascot makeMascot(const char* name, const char* const (&rows)[R], const Swatch (&sw)[S],
                            Rgb face, int8_t faceDy) {
  return {name, MASCOT_MAX_W, (int8_t)R, rows, sw, (int8_t)S, face, faceDy};
}

// MascotId 와 같은 순서
const Mascot MASCOTS[] = {
    makeMascot("stick", STICK_ROWS, STICK_SWATCHES, INK_RGB, 1),
    makeMascot("keycap", KEYCAP_ROWS, KEYCAP_SWATCHES, INK_RGB, 1),
    makeMascot("cat", CAT_ROWS, CAT_SWATCHES, {0xFF, 0xE4, 0x5C}, 4),
    makeMascot("robot", ROBOT_ROWS, ROBOT_SWATCHES, INK_RGB, 2),
    makeMascot("sprout", SPROUT_ROWS, SPROUT_SWATCHES, INK_RGB, 5),
    makeMascot("claude", CLAUDE_ROWS, CLAUDE_SWATCHES, INK_RGB, 0),
};
static_assert(sizeof(MASCOTS) / sizeof(MASCOTS[0]) == MASCOT_COUNT,
              "MASCOTS 의 줄 수가 MASCOT_COUNT 와 다르다");

const Cell EYES_DOT[]   = {{5, 2, 1, 2}, {12, 2, 1, 2}};
const Cell EYES_WIDE[]  = {{5, 1, 1, 3}, {12, 1, 1, 3}};
const Cell EYES_HAPPY[] = {{4, 3, 1, 1}, {5, 2, 1, 1}, {6, 3, 1, 1}, {11, 3, 1, 1}, {12, 2, 1, 1}, {13, 3, 1, 1}};
const Cell EYES_SLEEP[] = {{4, 3, 3, 1}, {11, 3, 3, 1}};
const Cell EYES_CROSS[] = {{4, 1, 1, 1},  {6, 1, 1, 1},  {5, 2, 1, 1},  {4, 3, 1, 1},  {6, 3, 1, 1},
                           {11, 1, 1, 1}, {13, 1, 1, 1}, {12, 2, 1, 1}, {11, 3, 1, 1}, {13, 3, 1, 1}};
const Cell EYES_UP[]    = {{5, 1, 1, 2}, {12, 1, 1, 2}};
const Cell EYES_DOWN[]  = {{5, 3, 1, 2}, {12, 3, 1, 2}};
const Cell EYES_LEFT[]  = {{4, 2, 1, 2}, {11, 2, 1, 2}};
const Cell EYES_RIGHT[] = {{6, 2, 1, 2}, {13, 2, 1, 2}};
const Cell EYES_HEART[] = {{4, 1, 1, 1},  {6, 1, 1, 1},  {4, 2, 3, 1},  {5, 3, 1, 1},
                           {11, 1, 1, 1}, {13, 1, 1, 1}, {11, 2, 3, 1}, {12, 3, 1, 1}};
const Cell EYES_SAD[]   = {{4, 2, 1, 1}, {5, 3, 1, 1}, {6, 3, 1, 1}, {11, 3, 1, 1}, {12, 3, 1, 1}, {13, 2, 1, 1}};
const Cell MOUTH_OPEN[] = {{8, 5, 2, 2}};
const Cell MOUTH_FLAT[] = {{8, 5, 2, 1}};

template <int N>
int cells(const Cell (&arr)[N], const Cell*& out) {
  out = arr;
  return N;
}

}  // namespace

bool hasGlyph(char c) { return indexOf(c) >= 0; }

Glyph boldGlyph(char c) {
  if (c == ' ') return {3, 8, {0, 0, 0, 0, 0, 0, 0, 0}};
  char up = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  for (const Override& o : BOLD_OVERRIDES) {
    if (o.c == up) return o.g;
  }
  // 바탕 글자의 각 점을 오른쪽과 아래로 한 칸씩 더 칠한다 → 6 x 8
  const uint8_t* b = base(c);
  Glyph g{6, 8, {0, 0, 0, 0, 0, 0, 0, 0}};
  for (int r = 0; r < 8; r++) {
    const uint8_t here  = (r < 7) ? widen(b[r]) : 0;
    const uint8_t above = (r > 0) ? widen(b[r - 1]) : 0;
    g.rows[r] = (uint8_t)(here | above);
  }
  return g;
}

Glyph smallGlyph(char c) {
  if (c == ' ') return {3, 7, {0, 0, 0, 0, 0, 0, 0, 0}};
  const uint8_t* b = base(c);
  Glyph g{6, 7, {0, 0, 0, 0, 0, 0, 0, 0}};
  for (int r = 0; r < 7; r++) g.rows[r] = widen(b[r]);
  return g;
}

int boldWidth(const char* s) { return widthOf(s, true); }
int smallWidth(const char* s) { return widthOf(s, false); }

// 여기서 빌드가 멈추면("no member named ... in MascotId") 빌드 때 준 MASCOT_ID 가
// pixel_art.h 의 MascotId 에 없는 이름이다(Stick · Keycap · Cat · Robot · Sprout · Claude 중 하나여야 한다).
const Mascot& mascot() { return MASCOTS[(int)MascotId::MASCOT_ID]; }

const Mascot& mascotAt(int i) { return (i >= 0 && i < MASCOT_COUNT) ? MASCOTS[i] : mascot(); }

bool mascotCell(const Mascot& m, int col, int row, Rgb& out) {
  if (col < 0 || row < 0 || col >= m.w || row >= m.h) return false;
  const char ch = m.rows[row][col];
  if (ch == '.') return false;
  for (int i = 0; i < m.swatchCount; i++) {
    if (m.swatches[i].key == ch) {
      out = m.swatches[i].rgb;
      return true;
    }
  }
  return false;  // 색 표에 없는 글자(있어서는 안 된다 — 시험이 잡는다)
}

int eyeCells(Eyes e, const Cell*& out) {
  switch (e) {
    case Eyes::Dot:   return cells(EYES_DOT, out);
    case Eyes::Wide:  return cells(EYES_WIDE, out);
    case Eyes::Happy: return cells(EYES_HAPPY, out);
    case Eyes::Sleep: return cells(EYES_SLEEP, out);
    case Eyes::Cross: return cells(EYES_CROSS, out);
    case Eyes::Up:    return cells(EYES_UP, out);
    case Eyes::Down:  return cells(EYES_DOWN, out);
    case Eyes::Left:  return cells(EYES_LEFT, out);
    case Eyes::Right: return cells(EYES_RIGHT, out);
    case Eyes::Heart: return cells(EYES_HEART, out);
    case Eyes::Sad:   return cells(EYES_SAD, out);
  }
  return cells(EYES_DOT, out);
}

int mouthCells(Mouth m, const Cell*& out) {
  switch (m) {
    case Mouth::Open: return cells(MOUTH_OPEN, out);
    case Mouth::Flat: return cells(MOUTH_FLAT, out);
    case Mouth::None: break;
  }
  out = nullptr;
  return 0;
}

}  // namespace pixelart

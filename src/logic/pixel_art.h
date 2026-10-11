#pragma once
#include <stdint.h>

// 화면에 쓰는 점 그림: 글자와 마스코트. 하드웨어를 모른다.

// D26 마스코트: 이 프로젝트를 위해 새로 그린 다섯(스틱이 Stick · 키캡이 Keycap · 까망이 Cat ·
//     삐삐 Robot · 새싹이 Sprout)을 모두 넣고, 빌드 때 하나를 고른다. 기본은 까망이.
//   이유: 처음 쓰던 캐릭터(D23, 아래 Claude)는 Anthropic 의 것이라 판매용으로는 쓸 수 없고 MIT 의 예외다.
//         자체 다섯을 모두 데이터로 두면 바꾸는 일이 한 줄이고, 받아 쓰는 사람도 취향대로 고를 수 있다.
//   비용: 펌웨어가 1KB 남짓 커진다. 시험이 다섯을 모두 그려 본다.
//   탈출구: 아래 MASCOT_ID 를 바꾸거나 빌드 때 -DMASCOT_ID=Robot 처럼 준다(그림은 test/host/preview.sh 로
//           먼저 본다). 캐릭터를 줄이려면 pixel_art.cpp 의 MASCOTS 와 아래 MascotId 에서 같은 줄을 지운다.
// D29 기본값을 Claude Code 캐릭터(옛 D23, 이름 Claude)로 되돌린다. 자체 다섯은 그대로 둔다.
//   이유: 작성자의 선택(이 캐릭터가 더 어울린다고 봤다). 공개판에도 "비공식 팬아트, 요청 시 제거" 고지(NOTICE·README)와
//         함께 넣기로 결정(같은 날 저녁). Anthropic 의 승인을 받은 것이 아니다.
//   비용: 권리자가 요청하면 캐릭터를 빼야 한다(그때는 아래 탈출구 한 줄). 판매용으로는 쓸 수 없다.
//   탈출구: `#define MASCOT_ID Cat` 으로 돌리고, 아래 MascotId 와 pixel_art.cpp 의 "claude" 줄을 지운다.
#ifndef MASCOT_ID
#define MASCOT_ID Claude
#endif

namespace pixelart {

// 글자 하나. 줄마다 한 바이트이고 가장 왼쪽 칸이 최상위 비트다.
struct Glyph {
  uint8_t w;  // 너비(칸)
  uint8_t h;  // 높이(칸)
  uint8_t rows[8];
};

// 그릴 수 있는 글자인가(영문 대소문자, 숫자, ! ? % . -). 빈칸은 글자가 아니다.
bool hasGlyph(char c);

// 굵은 글자(말풍선용): 높이 8, 너비 6. 굵게 하면 뭉개지거나 다른 글자로 보이는 M·W·N·X·K·! 는
// 따로 그린 모양을 쓴다(M·W 는 너비 7, ! 는 너비 2).
// 없는 글자는 '?' 모양이 나온다. 빈칸은 너비 3 의 빈 글자다.
Glyph boldGlyph(char c);
// 작은 글자(위 띠용): 높이 7, 너비 6
Glyph smallGlyph(char c);

// 글자열의 너비(칸). 글자 사이에 한 칸을 둔다.
int boldWidth(const char* s);
int smallWidth(const char* s);

// ---- 마스코트 (D26) ----
struct Rgb {
  uint8_t r, g, b;
};
// 테두리·글자·(대부분의) 눈과 입에 쓰는 검정. 화면 쪽의 color::INK 도 이 값에서 만든다.
constexpr Rgb INK_RGB = {0x14, 0x14, 0x13};
// 몸 격자의 글자 하나가 뜻하는 색
struct Swatch {
  char key;
  Rgb  rgb;
};
struct Mascot {
  const char*        name;         // 빌드 때 고르는 이름의 소문자("cat" 등)
  int8_t             w, h;         // 칸 수
  const char* const* rows;         // h 줄, 줄마다 w 글자. '.' 은 빈칸, 그 밖은 swatches 의 key
  const Swatch*      swatches;
  int8_t             swatchCount;
  Rgb                face;         // 눈·입의 색
  int8_t             faceDy;       // 표정 표(아래 Eyes·Mouth, 18칸 너비 기준)를 아래로 내리는 칸 수
};
// Claude 는 맨 뒤에 둔다(자체 다섯의 번호가 안 바뀌고, 공개판에서 줄 하나만 지우면 된다 — D29)
enum class MascotId : uint8_t { Stick, Keycap, Cat, Robot, Sprout, Claude };
const int MASCOT_COUNT = 6;
static_assert((int)MascotId::Claude + 1 == MASCOT_COUNT, "MASCOT_COUNT 가 마스코트 수와 다르다");
// 화면의 마스코트 자리에 들어가는 가장 큰 격자(칸)
const int MASCOT_MAX_W = 18;
const int MASCOT_MAX_H = 14;

// 이 빌드가 쓰는 마스코트
const Mascot& mascot();
// i 번째 마스코트(MascotId 순서). 범위 밖이면 mascot() 과 같다.
const Mascot& mascotAt(int i);
// (col,row) 칸의 색을 out 에 담는다. 빈칸이거나 격자 밖이면 false.
bool mascotCell(const Mascot& m, int col, int row, Rgb& out);

enum class Eyes : uint8_t { Dot, Wide, Happy, Sleep, Cross, Up, Down, Left, Right, Heart, Sad };
enum class Mouth : uint8_t { None, Open, Flat };

// 표정을 이루는 사각형(마스코트 칸 단위)
struct Cell {
  int8_t col, row, w, h;
};
int eyeCells(Eyes e, const Cell*& out);    // 개수를 돌려준다
int mouthCells(Mouth m, const Cell*& out);

}  // namespace pixelart

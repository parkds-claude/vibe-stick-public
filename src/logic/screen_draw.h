#pragma once
#include <stdint.h>

#include "link_state.h"
#include "pixel_art.h"
#include "scene.h"

// 화면 한 장을 "사각형 채우기"만으로 그린다(D22). 하드웨어를 모른다.
// 기기에서는 Canvas 가 LCD 에, 컴퓨터에서는 그림 파일에 연결된다 → 미리보기와 기기 화면이 같다.
class Canvas {
 public:
  virtual ~Canvas() {}
  // color 는 RGB565
  virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
};

namespace screen {

// 화면과 각 부분의 자리(픽셀)
const int W = 135;
const int H = 240;
const int BAR_H = 20;                                        // 위 띠: 연결 상태 + 배터리
const int MASCOT_SCALE = 6;                                  // 마스코트 한 칸의 크기(18칸 → 108)
const int BUBBLE_X = 6, BUBBLE_Y = 150, BUBBLE_W = 123, BUBBLE_H = 62;
const int BUBBLE_PAD = 4;                                    // 글자가 넘지 말아야 할 말풍선 안쪽 여백
const int STRIP_Y = 220, STRIP_H = 20;                       // 맨 아래 띠: 조이스틱 없음 안내

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace color {
const uint16_t INK    = rgb565(pixelart::INK_RGB.r, pixelart::INK_RGB.g, pixelart::INK_RGB.b);  // 테두리·글자·위 띠
const uint16_t WHITE  = rgb565(0xFF, 0xFF, 0xFF);
const uint16_t CREAM  = rgb565(0xF5, 0xEF, 0xE0);
const uint16_t PINK   = rgb565(0xFF, 0x5F, 0xA2);
const uint16_t YELLOW = rgb565(0xFF, 0xD8, 0x4D);
const uint16_t CORAL  = rgb565(0xFF, 0x7A, 0x66);
const uint16_t MINT   = rgb565(0x4F, 0xE3, 0xC1);
const uint16_t LILAC  = rgb565(0xB7, 0x9C, 0xFF);
const uint16_t LIME   = rgb565(0xB8, 0xF0, 0x60);
const uint16_t SKY    = rgb565(0x5C, 0xC8, 0xFF);
const uint16_t GREY   = rgb565(0xBD, 0xBA, 0xB2);
const uint16_t GREEN  = rgb565(0x57, 0xE3, 0x89);  // 위 띠의 "READY"
const uint16_t RED    = rgb565(0xFF, 0x4D, 0x4D);  // 배터리 부족
}  // namespace color

// 장면 하나의 모습
struct SceneSpec {
  const char*     line1;  // 말풍선 첫 줄
  const char*     line2;  // 둘째 줄(없으면 nullptr)
  uint16_t        bg;     // 바탕색
  pixelart::Eyes  eyes;
  pixelart::Mouth mouth;
};
const SceneSpec& specFor(Scene s);

// 위 띠에 쓰는 연결 상태 낱말
const char* linkWord(LinkState l);
// 맨 아래 띠의 글자
extern const char* const NO_STICK_TEXT;

// 말풍선 글자의 배율. 한 줄이면 4, 두 줄이면 3 에서 시작해 넘치면 줄인다.
int bubbleScale(const SceneSpec& spec);

// 화면 한 장에 담기는 것 전부
struct Frame {
  Scene     scene;
  LinkState link;
  int       batteryPct;  // 0~100, 모르면 -1
  bool      noStick;     // 조이스틱 없음 안내를 띄울 것인가
  bool operator==(const Frame& o) const {
    return scene == o.scene && link == o.link && batteryPct == o.batteryPct && noStick == o.noStick;
  }
  bool operator!=(const Frame& o) const { return !(*this == o); }
};

// 배터리 그림의 채움 너비(0~16칸). 15% 이하는 빨강이고, 거의 비었어도 보이게 최소 3칸을 채운다.
// 모르면(-1) 0.
int batteryFill(int pct);
inline bool batteryLow(int pct) { return pct >= 0 && pct <= 15; }

// 마스코트의 자리: 가로는 가운데, 세로는 바닥 줄(MASCOT_GROUND)에 발을 맞춘다.
const int MASCOT_GROUND = 116;
inline int mascotX(const pixelart::Mascot& m) { return (W - m.w * MASCOT_SCALE) / 2; }
inline int mascotY(const pixelart::Mascot& m) { return MASCOT_GROUND - m.h * MASCOT_SCALE; }

// 화면 전체를 그린다(빈 곳을 남기지 않는다). 마스코트는 이 빌드의 것(pixelart::mascot()).
void drawFrame(Canvas& c, const Frame& f);
// 마스코트를 골라 그린다(미리보기 그림과 시험에서 다섯을 모두 그려 볼 때).
void drawFrame(Canvas& c, const Frame& f, const pixelart::Mascot& m);

// 화면을 가로 띠 BANDS 개로 나눠 그 가운데 하나만 그린다(band = 0..BANDS-1, 위에서부터).
// 한 번에 다 그리면 그동안(수십 ms) 버튼과 조이스틱을 읽지 못하므로, 기기에서는 틱마다
// 한 띠씩 그린다. 모든 띠를 그리면 drawFrame 과 점 하나까지 같다.
const int BANDS = 4;
void drawFrameBand(Canvas& c, const Frame& f, int band);

}  // namespace screen

#pragma once
#include <stdint.h>

#include "logic/clock.h"

// 화면을 언제 밝히고, 어둡게 하고, 끄는가. 하드웨어를 모른다(컴퓨터에서 시험).
//
// D60·D61 "쓰고 있다"는 신호를 손가락 말고도 받는다: 손이 가까이 왔다(근접 센서), 기기가 움직였다(움직임 센서).
//   이유: 어두워진 화면을 깨우려고 화면을 누르면 그 누름이 버튼으로도 들어간다. 손을 뻗거나 집어 드는 것만으로
//         켜지면 그럴 일이 없다. 그리고 선 없이 쓸 때는 가만히 두면 화면을 꺼 배터리를 아낀다.
//   비용: 센서를 계속 읽는다. 책상의 떨림이나 지나가는 손에 켜질 수 있다(문턱은 실측으로 — config.h).
//   탈출구: moving·near 를 늘 false 로 넘기면 예전처럼 손가락과 장면 바뀜에만 반응한다.
namespace touchui {

enum class Light : uint8_t { Bright, Dim, Off };

struct PowerInput {
  bool touching;      // 화면에 손가락이 닿아 있다
  bool micLive;       // 컴퓨터가 듣는 중(말하는 동안에는 어둡게 하지 않는다)
  bool sceneChanged;  // 장면이 바뀌었다(연결됨, 동작 장면, 알림)
  bool moving;        // 기기가 움직이고 있다(쥐고 있다·집어 들었다)
  bool near;          // 손이 가까이 있다
  bool usable;        // 키를 보낼 수 있다(연결됨)
  bool onBattery;     // 선 없이 쓰는 중
};

class ScreenPower {
 public:
  ScreenPower(uint32_t dimAfterMs, uint32_t offAfterMs) : dimAfter_(dimAfterMs), offAfter_(offAfterMs) {}

  void begin(uint32_t nowMs) {
    lastActiveMs_ = nowMs;
    light_ = Light::Bright;
  }

  // 매 틱 부른다. 돌려주는 값 = 지금의 화면 단계.
  //   쓰고 있으면 밝게. 한동안 쓰지 않으면 어둡게. 쓸 수 없거나(컴퓨터 없음) 배터리로 도는 중이면 더 지나 아예 끈다.
  //   선이 꽂혀 쓸 수 있는 동안에는 끄지 않는다 — 책상 위에서 상태와 시각을 보는 화면이다.
  Light poll(uint32_t nowMs, const PowerInput& in) {
    if (in.touching || in.micLive || in.sceneChanged || in.moving || in.near) {
      lastActiveMs_ = nowMs;
      light_ = Light::Bright;
    } else if ((!in.usable || in.onBattery) && elapsedMs(nowMs, lastActiveMs_, offAfter_)) {
      light_ = Light::Off;
    } else if (elapsedMs(nowMs, lastActiveMs_, dimAfter_)) {
      light_ = Light::Dim;
    }
    return light_;
  }
  Light light() const { return light_; }

 private:
  const uint32_t dimAfter_, offAfter_;
  uint32_t       lastActiveMs_ = 0;
  Light          light_ = Light::Bright;
};

// 손이 가까이 있는가(근접 센서의 값 0~2047, 가까울수록 크다). 경계에서 떨지 않게 문턱 둘을 쓴다.
class Proximity {
 public:
  Proximity(uint16_t nearAt, uint16_t farAt) : nearAt_(nearAt), farAt_(farAt) {}
  bool feed(uint16_t ps) {
    if (ps >= nearAt_) near_ = true;
    else if (ps <= farAt_) near_ = false;
    return near_;
  }
  bool near() const { return near_; }

 private:
  const uint16_t nearAt_, farAt_;
  bool           near_ = false;
};

// 주변 밝기(센서의 날 값, 밝을수록 크다) → 화면 밝기의 세 단계. 경계에서 깜빡이지 않게 오르내리는 문턱이 다르다.
class AutoBright {
 public:
  enum Level : uint8_t { Dark, Normal, Sunny };
  struct Limits {
    uint16_t darkBelow, darkAbove;    // darkBelow 이하면 어두운 방, darkAbove 이상이면 다시 보통
    uint16_t sunnyAbove, sunnyBelow;  // sunnyAbove 이상이면 아주 밝은 곳, sunnyBelow 이하면 다시 보통
  };
  explicit AutoBright(const Limits& l) : l_(l) {}
  Level feed(uint16_t als) {
    switch (level_) {
      case Dark:
        if (als >= l_.darkAbove) level_ = Normal;
        break;
      case Normal:
        if (als <= l_.darkBelow) level_ = Dark;
        else if (als >= l_.sunnyAbove) level_ = Sunny;
        break;
      case Sunny:
        if (als <= l_.sunnyBelow) level_ = Normal;
        break;
    }
    return level_;
  }
  Level level() const { return level_; }

 private:
  const Limits l_;
  Level        level_ = Normal;
};

}  // namespace touchui

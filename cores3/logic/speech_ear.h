#pragma once
#include <stdint.h>

// 방의 소리 속에서 "지금 말소리가 들린다"를 가려낸다. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
// D65 의 "미루어 듣는 중"을 얼마나 이어 갈지 정하는 데 쓴다: 말소리가 한동안 없으면 내린다.
//   앞선 판은 입을 여닫는 데 쓰던 문턱(켜짐 300 · 꺼짐 150)을 그대로 썼다. 그 방의 평소 소리가
//   60~250 이라, 한 번 켜지면 꺼짐 문턱 위에 계속 걸려 "조용하다"가 오지 않았다 → 빨간 화면이 2분까지 남았다
//   (빨간 화면이 켜진 채로 남는다는 지적).
//   지금: 방의 바닥 소리(조용한 순간의 크기)를 따라가며, 그보다 ratio 배 크고 minLevel 이상인 소리만 말소리로 친다.
//         바닥은 내려갈 때는 곧바로, 올라갈 때는 천천히(약 5초) 따라간다 — 말에는 낱말 사이의 조용한 틈이 있어
//         말하는 동안에도 바닥은 낮게 남는다.
//   비용: 늘 시끄러운 곳(환풍기·음악)에서는 문턱이 올라가 작은 목소리를 놓친다 → 말하는 중에도 미룸이 15초에 내려갈 수 있다.
//   탈출구: ratio 를 0 으로 주면 minLevel 하나만 쓰는 고정 문턱이다.
//
// D70 큰 소리가 "이어져야" 말소리로 친다.
//   이유: 문턱을 넘는 덩어리 하나만으로 말소리로 쳤더니, 그 방에서 몇 초에 한 번 튀는 소리(기기의 기록: 5초마다의
//         가장 큰 값이 349~702, 바닥은 48~139, 문턱 400)가 번번이 "말소리"가 되어 15초 조용함이 오지 않았다
//         → 말을 멈추고 70초가 지나도 빨간 화면이 남았다.
//         말은 한 음절이 0.1초쯤 이어지고, 툭·딸깍 하는 소리는 한두 덩어리(0.01~0.02초)로 끝난다.
//   규칙: 문턱을 넘은 틱에는 셈을 하나 올리고(위로 need+2 까지), 넘지 못한 틱에는 하나 내린다.
//         셈이 need 이상인 동안만 "들린다"(틱이 10ms 면 need 4 = 0.04초).
//   비용: 말소리를 0.03초 늦게 알아챈다. 0.04초 넘게 이어지는 소음(박수, 자판을 세게 두드리는 소리)은 여전히 말소리로 친다.
//   탈출구: need 를 1 로 주면 예전처럼 덩어리 하나로 정한다.
namespace touchui {

class SpeechEar {
 public:
  SpeechEar(uint16_t minLevel, uint8_t ratio, uint8_t need = 4)
      : min_(minLevel), ratio_(ratio), need_(need ? need : 1) {}

  // 한 틱에 한 번. rms: 지금의 소리 크기(마이크의 날 값). 반환: 말소리가 들리는가
  bool feed(uint16_t rms) {
    // 0 은 "아주 조용하다"가 아니라 "마이크가 쉬는 중"이다(알림음을 내느라 내려 둠 · 멎어서 되살리는 중 — mic.cpp).
    // 살아 있는 마이크의 덩어리는 0 이 되지 않는다(가장 조용한 방도 20~100). 바닥으로 삼으면 문턱이 바닥값까지
    // 떨어져, 시끄러운 방에서는 알림음이 날 때마다 그 방의 소음이 몇 초 동안 말소리가 된다(독립 검토).
    if (rms == 0) {
      loud_ = false;
      if (run_ > 0) --run_;
      return run_ >= need_;
    }
    const uint32_t v = (uint32_t)rms << 8;
    if (!primed_ || v < floorQ8_) {
      floorQ8_ = v;  // 더 조용한 순간이 왔다: 바닥을 곧바로 내린다
      primed_ = true;
    } else {
      floorQ8_ += (v - floorQ8_) >> RISE_SHIFT;  // 천천히 올린다(틱이 10ms 면 약 5초)
    }
    loud_ = rms >= threshold();
    if (loud_) {
      if (run_ < need_ + RUN_SLACK) ++run_;
    } else if (run_ > 0) {
      --run_;
    }
    return run_ >= need_;
  }

  bool     loud() const { return loud_; }  // 방금의 덩어리가 문턱을 넘었는가(이어졌는지는 따지지 않는다 — 기록용)
  uint16_t floor() const { return (uint16_t)(floorQ8_ >> 8); }
  uint16_t threshold() const {
    const uint32_t t = (floorQ8_ >> 8) * ratio_;
    return (uint16_t)(t < min_ ? min_ : t > 65535u ? 65535u : t);
  }

 private:
  static const int RISE_SHIFT = 9;
  static const int RUN_SLACK = 2;  // 셈의 위쪽 여유: 음절 안에서 잠깐 꺼지는 덩어리 두엇은 넘어간다
  const uint16_t min_;
  const uint8_t  ratio_;
  const uint8_t  need_;
  uint32_t       floorQ8_ = 0;  // 바닥 소리(256배 한 값)
  bool           primed_ = false;
  bool           loud_ = false;
  uint16_t       run_ = 0;      // 문턱을 넘은 틱의 셈(넘으면 +1, 아니면 -1)
};

// 기록용 셈: 한 구간에서 "크다"였던 틱의 수(count)와 가장 길게 이어진 틱 수(longest).
//   D70 의 값(문턱 · 이어져야 하는 길이)은 그 방과 그 사람의 목소리에 맞춰야 하는데, 기기의 기록에 "가장 큰 값"만
//   있어서는 툭 튀는 소리가 몇 틱 이어지는지, 말소리가 문턱 위에 얼마나 머무는지 알 수 없었다.
struct LoudStats {
  uint16_t count = 0;    // 크다였던 틱
  uint16_t longest = 0;  // 가장 길게 이어진 틱
  void feed(bool loud) {
    if (!loud) {
      run_ = 0;
      return;
    }
    if (count < 65535) ++count;
    if (run_ < 65535) ++run_;
    if (run_ > longest) longest = run_;
  }
  void reset() { count = longest = run_ = 0; }

 private:
  uint16_t run_ = 0;
};

}  // namespace touchui

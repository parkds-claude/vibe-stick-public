#pragma once
#include <stdint.h>

#include "logic/clock.h"

// 움직임 센서(가속도, 1000 = 1g)로 아는 두 가지: 기기가 움직이고 있는가, 흔들었는가. 하드웨어를 모른다.
namespace touchui {

// 기기가 움직이고 있는가(쥐고 있다·집어 들었다). 책상에 가만히 놓이면 한동안 뒤에 false.
class MotionDetector {
 public:
  // moveMg: 두 번의 읽기 사이에 세 축의 변화가 합쳐 이만큼 넘으면 "움직였다"(가만히 놓인 기기의 잡음보다 크게)
  // holdMs: 마지막 움직임 뒤 이만큼은 움직이는 중으로 본다
  MotionDetector(int moveMg, uint32_t holdMs) : moveMg_(moveMg), holdMs_(holdMs) {}

  bool feed(int16_t ax, int16_t ay, int16_t az, uint32_t nowMs) {
    if (have_) {
      const int d = absd(ax, px_) + absd(ay, py_) + absd(az, pz_);
      if (d > moveMg_) {
        moved_ = true;
        lastMoveMs_ = nowMs;
      }
    }
    px_ = ax;
    py_ = ay;
    pz_ = az;
    have_ = true;
    if (moved_ && elapsedMs(nowMs, lastMoveMs_, holdMs_)) moved_ = false;
    return moved_;
  }
  bool moving() const { return moved_; }

 private:
  static int absd(int a, int b) { return a > b ? a - b : b - a; }
  const int      moveMg_;
  const uint32_t holdMs_;
  int16_t        px_ = 0, py_ = 0, pz_ = 0;
  bool           have_ = false, moved_ = false;
  uint32_t       lastMoveMs_ = 0;
};

// D61 흔들기: 짧은 시간 안에 센 가속이 방향을 바꿔 가며 여러 번 → 취소(Esc).
//   이유: 손에 쥔 채 "아니, 그만"을 가장 빨리 말하는 몸짓이다.
//   비용: 잘못 알아보면 진행 중인 일이 멈춘다 → 문턱을 높게(가만히 들고 걷거나 내려놓는 정도로는 넘지 않게),
//         한 번 알아본 뒤에는 한동안 쉰다. 기기를 떨어뜨리면 한두 번의 큰 충격은 나지만 방향이 번갈아 여러 번은 아니다.
//   탈출구: config.h 의 SHAKE_CANCEL 을 false 로.
class ShakeDetector {
 public:
  struct Limits {
    int      strongMg;    // 중력을 뺀 가속이 이만큼 넘어야 "센 흔들림"
    int      swings;      // 방향이 바뀐 센 흔들림이 이만큼 이어져야 흔들기
    uint32_t windowMs;    // 그것이 이 시간 안에 일어나야 한다
    uint32_t cooldownMs;  // 알아본 뒤 이만큼은 다시 알아보지 않는다
  };
  explicit ShakeDetector(const Limits& l) : l_(l) {}

  // 가속도 한 번. 흔들기를 알아본 그 순간에만 true.
  bool feed(int16_t ax, int16_t ay, int16_t az, uint32_t nowMs) {
    const int a[3] = {ax, ay, az};
    if (!have_) {
      for (int i = 0; i < 3; i++) g_[i] = a[i] * 16;
      have_ = true;
      return false;
    }
    bool fired = false;
    for (int i = 0; i < 3; i++) {
      const int hp = a[i] - g_[i] / 16;  // 중력(천천히 변하는 부분)을 뺀 것
      g_[i] += hp;                       // 천천히 따라간다: g += (a - g) / 16
      const int sign = hp > l_.strongMg ? 1 : hp < -l_.strongMg ? -1 : 0;
      if (sign == 0 || sign == lastSign_[i]) continue;
      // 이 축에서 센 흔들림의 방향이 바뀌었다.
      if (count_[i] == 0 || elapsedMs(nowMs, firstMs_[i], l_.windowMs)) {
        count_[i] = 0;
        firstMs_[i] = nowMs;
      }
      lastSign_[i] = (int8_t)sign;
      if (++count_[i] >= l_.swings) {
        count_[i] = 0;
        if (!cooling_ || elapsedMs(nowMs, firedMs_, l_.cooldownMs)) {
          cooling_ = true;
          firedMs_ = nowMs;
          fired = true;
        }
      }
    }
    return fired;
  }

 private:
  const Limits l_;
  bool         have_ = false, cooling_ = false;
  int          g_[3] = {0, 0, 0};  // 중력의 어림값 x16
  int8_t       lastSign_[3] = {0, 0, 0};
  int          count_[3] = {0, 0, 0};
  uint32_t     firstMs_[3] = {0, 0, 0};
  uint32_t     firedMs_ = 0;
};

}  // namespace touchui

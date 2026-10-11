#pragma once
#include <stdint.h>

#include "logic/clock.h"

// D42 개발판의 안전장치: "지금 스스로 내려받기 모드로 물러나야 하는가"를 정한다.
// 하드웨어를 모른다(컴퓨터에서 시험 가능). 평소 판에는 들어가지 않는다(usb_dev.cpp 의 #if DEV_BUILD).
//
// usb_dev 는 이렇게 쓴다:
//   begin(지금)                     USB 를 켠 직후에 한 번
//   onCommand(이번 틱의 시각)       로그 포트로 명령(vibe:...)을 받을 때마다
//   poll(이번 틱의 시각, 컴퓨터가 말을 걸어 왔는가, 쓰고 있는가, 잡힌 횟수)   매 틱 → 판정
//
// 판정(위에서부터 먼저 맞는 것):
//   NoHost        한 번도 잡히지 않았고, 컴퓨터가 mountWaitMs 동안 말조차 걸어 오지 않았다.
//   NeverMounted  한 번도 잡히지 않은 채 켠 지 allowWaitMs 가 지났다(말은 걸어 왔는데 쓰지 않는다 —
//                 컴퓨터가 잠겨 있거나, 사람이 "허용"을 눌러야 하는 때를 넉넉히 기다린 뒤다).
//   NoCommand     잡힌 적이 있고, 명령이 rescueMs 동안 없었다.
//
// 이 자리에서 탈이 여러 번 났다(모두: (1)은 기기에 올리기 전 검토에서, (2)·(3)은 기기에서).
// 규칙마다 그 사연이 있고, 시험이 붙잡고 있다(test_touch.cpp 의 test_rescue_*):
//   (1) 시각은 "마감에 이르렀나"(reachedMs)로만 견준다.
//       begin()·onCommand() 가 받는 시각은 시계를 따로 읽어 적은 값이라 poll() 의 시각보다 조금 뒤일
//       수 있다. "얼마나 지났나"(elapsedMs)로 세면 그 차이가 아주 큰 값으로 뒤집힌다 — 개발판이 첫
//       명령을 받자마자 물러나게 돼 있었다(logic/clock.h 의 함정, 설계서 7.1).
//   (2) "말이 없음"은 mountWaitMs 동안 이어져야 인정한다.
//       컴퓨터가 버스를 초기화하고 다시 알아보는 짧은 순간에도 connected 는 거짓이 된다. 그 순간에
//       물러나면 막 받아들여지려던 참에 사라진다. 그리고 말은 걸어 왔으면 allowWaitMs 까지 기다린다 —
//       맥북이 잠겨 있어 15초 만에 물러난 일이 있었다.
//   (3) "명령이 없었던 시간"은 컴퓨터가 기기를 (다시) 잡은 때부터 센다.
//       켠 때부터 세면, 컴퓨터가 한참 뒤에(잠금을 풀고서) 받아들인 바로 그 순간에 "명령이 3분
//       없었다"가 참이 되어 물러난다(블랙박스: 6분 29초, 까닭 4).
//   (4) 다시 잡혔는지는 "잡힌 횟수"로도 본다.
//       이유: 직전 틱과 지금의 mounted 만 견주면, loop 가 막혀 있는 동안(키 보내기·화면 그리기) 떼였다가
//             다시 잡힌 것을 놓친다 — 두 틱 모두 mounted 가 참이다. 그러면 새로 잡힌 직후에 옛 마감으로
//             물러난다(코덱스 검토).
//       비용: USB 태스크가 올리는 횟수 하나(usb_dev.cpp 의 mountCount).
//       탈출구: poll() 에 늘 같은 횟수를 넘기면 예전 방식(mounted 가 거짓 → 참으로 바뀐 틱만)이다.
enum class DevRescueVerdict : uint8_t {
  Stay,          // 그대로 돈다
  NoHost,        // 컴퓨터가 말조차 걸어 오지 않는다
  NeverMounted,  // 말은 걸어 왔지만 끝내 쓰기 시작하지 않았다
  NoCommand,     // 쓰기 시작한 뒤 명령이 없다
};

class DevRescue {
 public:
  constexpr DevRescue(uint32_t mountWaitMs, uint32_t allowWaitMs, uint32_t rescueMs)
      : mountWaitMs_(mountWaitMs), allowWaitMs_(allowWaitMs), rescueMs_(rescueMs) {}

  // 켠 때를 적는다. 세 마감 모두 여기서부터 센다.
  void begin(uint32_t nowMs) { startMs_ = lastHostMs_ = lastCmdMs_ = nowMs; }

  // 명령을 받았다. 넘기는 시각은 그 틱의 시각이면 된다(조금 뒤의 시각이어도 뒤집히지 않는다 — (1)).
  void onCommand(uint32_t nowMs) { lastCmdMs_ = nowMs; }

  // connected: 컴퓨터가 말을 걸어 왔다(tud_connected). mounted: 컴퓨터가 기기를 쓰고 있다(tud_mounted).
  // mountCount: 컴퓨터가 기기를 잡은 횟수(tud_mount_cb 가 불릴 때마다 1씩 는다. 처음은 0).
  DevRescueVerdict poll(uint32_t nowMs, bool connected, bool mounted, uint32_t mountCount) {
    const bool counted = mountCount != mountCount_;  // 직전 틱 뒤로 (다시) 잡혔다 — (4)
    mountCount_ = mountCount;
    if (counted || (mounted && !wasMounted_)) lastCmdMs_ = nowMs;  // (3)
    if (counted || mounted) everMounted_ = true;
    wasMounted_ = mounted;
    if (connected) lastHostMs_ = nowMs;

    if (!everMounted_ && reachedMs(nowMs, lastHostMs_ + mountWaitMs_)) return DevRescueVerdict::NoHost;
    if (!everMounted_ && reachedMs(nowMs, startMs_ + allowWaitMs_)) return DevRescueVerdict::NeverMounted;
    if (everMounted_ && reachedMs(nowMs, lastCmdMs_ + rescueMs_)) return DevRescueVerdict::NoCommand;
    return DevRescueVerdict::Stay;
  }

 private:
  uint32_t mountWaitMs_;
  uint32_t allowWaitMs_;
  uint32_t rescueMs_;
  uint32_t startMs_ = 0;     // 켠 때
  uint32_t lastHostMs_ = 0;  // 컴퓨터가 마지막으로 말을 걸어 온 때(켠 뒤로 없었으면 켠 때)
  uint32_t lastCmdMs_ = 0;   // 마지막으로 명령을 받은 때, 또는 그 뒤에 (다시) 잡힌 때
  uint32_t mountCount_ = 0;  // 직전 틱에 본 "잡힌 횟수"
  bool     everMounted_ = false;
  bool     wasMounted_ = false;  // 직전 틱에 컴퓨터가 기기를 쓰고 있었는가
};

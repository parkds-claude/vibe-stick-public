#pragma once
#include <stdint.h>

#include "logic/clock.h"

// D71 보는 곳의 톡(logic/touch_map.h 의 takeViewTap)을 받아도 되는 때인가. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
//   톡은 "지금의 표시가 실제와 다르다"는 것을 사람이 보고 치는 것이다. 그래서 이런 톡은 받지 않는다:
//   (가) 화면이 밝게 켜진 지 SEEN_MS 가 지나기 전에 닿기 시작한 손가락 — 어두워졌거나 꺼진 화면을 깨우려는 손이다.
//        손이 다가오는 것만으로 화면이 켜지는 때(근접 센서·집어 듦)도 같다: 켜지자마자 닿은 손가락은 표시를 보고 친 것이 아니다.
//        까닭: 어두워진 화면을 깨우려고 캐릭터를 친 것이 빨간 "듣는 중"을 켜면, 바로잡으려던 어긋남을 도리어 만든다
//              (독립 검토 — 꺼진 화면만 뜸 D69 로 걸렀고, 어두워지기만 한 화면은 빠져 있었다).
//   (나) 닿아 있는 동안, 또는 떼는 순간에 한 번이라도 막혀 있던 손가락 — 뜸(D69) · 펌웨어 물음(D51) 과 그 직후.
//   비용: 화면이 켜지고 0.5초 안에 친 톡은 듣지 않는다(한 번 더 친다).
//   탈출구: SEEN_MS 를 0 으로 두면 (가)는 "닿는 순간 밝게 켜져 있었는가"만 본다.
namespace touchui {

class ViewTapGuard {
 public:
  static const uint32_t SEEN_MS = 500;  // 화면이 밝게 켜진 뒤 이만큼은 지나야 "보고 쳤다"로 친다

  // 터치를 읽은 뒤 매 틱 한 번 부른다.
  //   touching: 지금 화면에 손가락이 닿아 있는가(자리가 아닌 곳 포함)
  //   lit:      화면이 밝게 켜져 있는가 — 이번 틱에 화면을 켜기 "전"의 상태(닿은 그 틱에는 아직 어둡다)
  //   blocked:  지금 누름을 받지 않는 때인가(뜸 · 펌웨어 물음)
  //   tap:      이번 틱에 보는 곳 톡이 알려졌는가(손을 뗀 뒤 몇 틱 지나 알려진다 — 그때 touching 은 이미 거짓이다)
  // 반환: 그 톡으로 표시를 뒤집어도 되는가
  bool feed(uint32_t nowMs, bool touching, bool lit, bool blocked, bool tap) {
    // 화면이 밝게 켜진 채로 SEEN_MS 가 지났는가. 한 번 지나면 꺼질 때까지 그대로 둔다: 켜진 시각을 들고 있다가
    // 시계가 반 바퀴(24.8일) 돌면 "아직 안 지났다"로 거꾸로 읽힌다.
    if (!lit) {
      litKnown_ = seen_ = false;
    } else if (!litKnown_) {
      litKnown_ = true;
      litAtMs_ = nowMs;
      seen_ = (SEEN_MS == 0);
    } else if (!seen_ && elapsedMs(nowMs, litAtMs_, SEEN_MS)) {
      seen_ = true;
    }
    // 새 손가락이 닿는 순간에 새로 정한다: 켜진 화면을 보고 닿았는가. (그래서 앞 손가락의 값은 다음 손가락에 남지 않는다.)
    if (touching && !wasTouching_) spoiled_ = !seen_;
    // 닿아 있는 동안, 또는 톡이 알려지는 바로 그 틱에 막혀 있었다(그 틱에 연결이 바뀌어 뜸이 걸릴 수 있다).
    if (blocked && (touching || tap)) spoiled_ = true;
    wasTouching_ = touching;
    return tap && !spoiled_;
  }

 private:
  bool     litKnown_ = false;  // litAtMs_ 에 값이 들어 있다
  bool     seen_ = false;      // 밝게 켜진 채로 SEEN_MS 가 지났다
  uint32_t litAtMs_ = 0;       // 밝게 켜진 시각
  bool     wasTouching_ = false;
  bool     spoiled_ = false;   // 지금 닿아 있는 손가락의 톡은 받지 않는다
};

}  // namespace touchui

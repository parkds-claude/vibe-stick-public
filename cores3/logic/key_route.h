#pragma once
#include <stdint.h>

#include "logic/link_state.h"

// D59 키를 어느 길로 보낼까: USB 가 쓸 수 있으면 USB, 아니면 블루투스.
//   이유: 손에 쥐고 쓰는 기기라 선 없이도 버튼은 눌려야 한다(키만 무선으로 — 소리는 선으로).
//         소리는 USB 로만 간다 — 이 칩(ESP32-S3)은 블루투스로 마이크 노릇을 못 한다(저전력 블루투스뿐).
//   비용: 블루투스로 쓰는 동안에는 "컴퓨터가 마이크를 열었는가"를 알 수 없다(그 사실은 USB 로만 온다).
//         블루투스 키보드는 잠긴 컴퓨터에도 키를 친다(평범한 블루투스 키보드와 같다).
//   탈출구: pickRoute 가 늘 Usb 를 돌려주게 하고 main 에서 blehid::begin 을 부르지 않는다.
// 하드웨어를 모른다(컴퓨터에서 시험).
namespace touchui {

enum class Route : uint8_t { Usb, Ble };

// 화면에 보일 연결 상태와, 그것이 블루투스의 것인가
struct LinkView {
  LinkState link;
  bool      wireless;
};

inline Route pickRoute(LinkState usb) { return usb == LinkState::Ready ? Route::Usb : Route::Ble; }

// usb: USB 의 상태(Ready / Connecting = 컴퓨터가 알아봤지만 아직 / Searching = 없음)
// ble: 블루투스의 상태(Ready / Connecting / Searching = 짝은 있는데 연결이 없다 / Pairing = 짝이 없다)
inline LinkView viewLink(LinkState usb, LinkState ble) {
  if (usb == LinkState::Ready) return {LinkState::Ready, false};
  if (ble == LinkState::Ready) return {LinkState::Ready, true};
  // 선이 꽂혀 있고 컴퓨터가 허용을 기다리는 중이면 그것을 알린다(사람이 할 일이 있다 — D50).
  if (usb == LinkState::Connecting) return {LinkState::Connecting, false};
  return {ble, true};
}

}  // namespace touchui

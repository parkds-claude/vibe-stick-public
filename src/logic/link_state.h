#pragma once
#include <stdint.h>

// 컴퓨터와의 연결 상태. 전송층(블루투스)이 알려 주고, 화면과 로그가 쓴다.
// 전송 방식이 바뀌어도(예: USB) 화면 쪽이 따라 바뀌지 않게 여기에 둔다.
enum class LinkState : uint8_t {
  Pairing,     // 결합된 컴퓨터 없음 — 짝 맺기 대기
  Searching,   // 결합된 컴퓨터를 찾는 중
  Connecting,  // 연결됨, 암호화(와 컴퓨터 쪽 준비) 대기
  Ready,       // 키를 보낼 수 있음
};

// 로그용 이름
inline const char* linkName(LinkState s) {
  switch (s) {
    case LinkState::Pairing:    return "PAIRING";
    case LinkState::Searching:  return "SEARCHING";
    case LinkState::Connecting: return "CONNECTING";
    case LinkState::Ready:      return "READY";
  }
  return "?";
}

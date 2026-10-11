#pragma once
#include <stdint.h>

#include "events.h"
#include "link_state.h"

// 화면에 보여줄 장면 (설계서 2.2)
enum class Scene : uint8_t {
  // 대기 장면: 연결 상태에 따라 정해진다
  Ready,       // 연결되어 대기
  Pairing,     // 짝 맺은 컴퓨터 없음
  Searching,   // 컴퓨터를 찾는 중
  Connecting,  // 연결하는 중
  // 동작 장면: 잠깐 보였다가 대기 장면으로 돌아간다
  Talk,    // 말하기(조이스틱 누름)
  Ok,      // 확인(Enter)
  Nope,    // 취소(Esc)
  Up,
  Down,
  Next,    // 다음 창
  Back,    // 이전 창
  NoLink,  // 보내려 했으나 연결이 없음
  Bye,     // 짝 초기화 성공
  Oops,    // 짝 초기화 실패
};
const int SCENE_COUNT = 14;
// 장면을 더하면 SCENE_COUNT 와 screen_draw.cpp 의 표(SPECS)도 함께 늘려야 한다.
static_assert((int)Scene::Oops + 1 == SCENE_COUNT, "SCENE_COUNT 가 장면 수와 다르다");

// 연결 상태에 맞는 대기 장면
inline Scene idleSceneFor(LinkState l) {
  switch (l) {
    case LinkState::Pairing:    return Scene::Pairing;
    case LinkState::Searching:  return Scene::Searching;
    case LinkState::Connecting: return Scene::Connecting;
    case LinkState::Ready:      return Scene::Ready;
  }
  return Scene::Searching;
}

// 사건을 실제로 보냈을 때 보여줄 장면. 보여줄 것이 없으면(뗌 등) false.
inline bool sceneFor(Ev e, Scene& out) {
  switch (e) {
    case Ev::TalkDown:
    case Ev::TalkTap:    out = Scene::Talk; return true;
    case Ev::Confirm:    out = Scene::Ok;   return true;  // Enter 는 화면에 따라 전송·승인·선택 확정
    case Ev::Cancel:     out = Scene::Nope; return true;
    case Ev::Up:         out = Scene::Up;   return true;
    case Ev::Down:       out = Scene::Down; return true;
    case Ev::WindowNext: out = Scene::Next; return true;
    case Ev::WindowPrev: out = Scene::Back; return true;
    case Ev::PairReset:  out = Scene::Bye;  return true;
    case Ev::TalkUp:
    case Ev::None:       return false;
  }
  return false;
}

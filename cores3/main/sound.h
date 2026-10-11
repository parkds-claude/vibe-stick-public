#pragma once
#include <stdint.h>

// D63 스피커: 누름 소리와 Claude 알림음.
//   스피커와 마이크는 같은 소리 통로(I2S)를 쓴다 → 소리를 낼 때만 마이크를 잠깐 내렸다 올린다.
//   컴퓨터가 듣는 중(마이크가 열려 있다)에는 소리를 내지 않는다 — 끊기면 받아쓰기가 망가지고, 소리가 마이크로 되돌아간다.
//   비용: 소리 하나에 0.05~0.4초 동안 마이크가 내려가 있다. 그 사이 컴퓨터가 마이크를 열면 첫 순간이 비어 있다
//         → 말하기(Talk)에는 누름 소리를 내지 않는다.
//   탈출구: config.h 의 SOUND_ON 을 false 로.
namespace sound {

enum class Kind : uint8_t { Click, Done, Ask };  // 뒤로 갈수록 중요하다(겹치면 중요한 것이 이긴다)

void play(Kind k);
// loop 에서 매 틱 부른다. micLive: 컴퓨터가 듣는 중인가.
void poll(uint32_t nowMs, bool micLive);

}  // namespace sound

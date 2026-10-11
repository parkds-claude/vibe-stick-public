#pragma once
#include <stddef.h>
#include <stdint.h>

// (개발판) 컴퓨터가 보낸 화면 전체 그림을 그대로 띄운다 — 디자인 시안을 실물 화면에서 볼 때 쓴다.
// 쓰는 법: tools/vibectl.py show <파일.rgb565>. 화면을 누르거나 `vibectl.py show off` 로 거둔다.
// 평소 판에는 들어가지 않는다(컴퓨터의 프로그램이 화면을 마음대로 바꿀 수 있게 두지 않는다).
namespace devshow {

const size_t IMAGE_BYTES = 320 * 240 * 2;  // RGB565, 점마다 2바이트(큰 쪽 먼저 = 화면이 받는 순서)

// 그림을 받기 시작한다. rotation: 화면 방향(0~3, 평소 화면은 1 = 가로·USB 가 왼쪽).
// 세로로 쥐는 시안(240x320)은 0 이나 2 로 띄운다 — 받는 바이트 수는 어느 방향이든 같다.
void   begin(int rotation);
bool   active();     // 그림을 받는 중이거나 띄워 둔 상태인가
size_t remaining();  // 아직 받아야 할 바이트 수(0 이면 다 받았다)
// 받은 바이트를 넘긴다(한 줄이 찰 때마다 화면에 보낸다). loop 에서만 부른다.
void   feed(const uint8_t* data, size_t n);
void   end();        // 그림을 거둔다. 화면을 다시 그리는 것은 부르는 쪽의 일이다.

}  // namespace devshow

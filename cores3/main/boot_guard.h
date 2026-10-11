#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// D48 켜지자마자 죽기를 되풀이하면 내려받기 모드로 물러난다.
//   이 기기는 USB 를 키보드·마이크로 켠 뒤에야 컴퓨터에 보인다. 펌웨어가 그 전에 죽으면(화면·전원
//   초기화 등) 재시작해도 또 죽고, 그동안 컴퓨터에는 아무것도 보이지 않는다 — 사람이 RST 를 3초
//   눌러야만 다시 구울 수 있다. 그래서 "죽어서 재시작"이 연달아 limit 번이면 USB 를 켜 보지도 않고
//   내려받기 모드로 간다. 그러면 tools/flash.sh 로 손 없이 고친 펌웨어를 올릴 수 있다.
//   세는 것: 프로그램 오류(panic)와 멈춤 감시(watchdog)로 인한 재시작. 전원을 켠 것, RST 를 누른 것,
//           굽기 도구가 내보낸 것은 세지 않고 횟수를 지운다.
//   비용: 정말로 계속 죽는 펌웨어는 화면이 꺼진 채(내려받기 모드)로 남는다 — 어차피 쓸 수 없는 상태다.
//   탈출구: app_main 맨 앞의 호출 한 줄을 지운다.
//   D52: 개발판에서만 부른다. 평소 판은 사람 손 없이 내려받기 모드로 가지 않는다(main.cpp).

// app_main 맨 처음에 부른다. true 면 내려받기 모드로 가야 한다.
bool boot_guard_tripped(uint32_t limit);
// 한동안 멀쩡히 돌았을 때 부른다(센 횟수를 지운다).
void boot_guard_healthy(void);

#ifdef __cplusplus
}
#endif

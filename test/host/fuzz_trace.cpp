// 무작위 입력을 InputLogic·JoyFilter 에 흘려 넣고 결과(사건, 상태)를 줄마다 찍는다.
// 구조를 정리(리팩토링)하기 전과 뒤의 출력을 비교해 "동작이 그대로인가"를 확인하는 데 쓴다.
// 쓰는 법은 test/host/compare_refactor.sh 참고.
#include <cstdio>
#include <cstdlib>

#include "logic/input_logic.h"
#include "logic/joy_filter.h"

static uint32_t rs;
static uint32_t rnd() {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}
static bool oneIn(uint32_t n) { return rnd() % n == 0; }

// 임계값(40, 90) 바로 안팎과 양 끝을 골고루 넣는다
static const int VALS[] = {0,  0,   0,  5,   -5, 30,  -30, 39,  -39, 40,  -40, 41,   -41, 60,
                           -60, 85, -85, 89, -89, 90, -90, 95,  -95, 120, -120, 127, -128};

int main(int argc, char** argv) {
  const int seeds = (argc > 1) ? atoi(argv[1]) : 200;
  for (int sd = 1; sd <= seeds; sd++) {
    rs = (uint32_t)sd * 2654435761u | 1u;
    InputLogic lg;
    JoyFilter  jf;
    if (sd % 6 == 0) lg.setTalkLeadMs(330);  // 초점 키를 쓰는 경우
    bool a = false, b = false, jb = false, ok = true;
    int  x = 0, y = 0;
    uint32_t t = (sd % 3 == 0) ? 0xFFFFF000u : 1000u;  // millis() 가 한 바퀴 도는 구간 포함
    // 시드마다 성향을 바꾼다: 버튼을 오래 쥐는 시드(짝 초기화·길게 누름), 자주 흔드는 시드(떨림)
    const uint32_t btnRate = (sd % 4 == 0) ? 400 : (sd % 4 == 1 ? 3 : 25);
    const uint32_t joyRate = (sd % 5 == 0) ? 2 : 15;
    for (int i = 0; i < 4000; i++) {
      if (oneIn(btnRate)) a = !a;
      if (oneIn(btnRate + 3)) b = !b;
      if (oneIn(joyRate)) jb = !jb;
      if (oneIn(joyRate)) x = VALS[rnd() % 27];
      if (oneIn(joyRate)) y = VALS[rnd() % 27];
      if (oneIn(sd % 2 ? 300 : 9)) ok = !ok;  // 조이스틱 읽기 실패
      jf.feed(ok, (int8_t)x, (int8_t)y, jb);
      RawInput in{};
      in.btnA = a;
      in.btnB = b;
      if (!oneIn(sd % 7 == 0 ? 3 : 200)) jf.fill(in);  // 가끔 조이스틱이 없다고 본다
      if (oneIn(250)) {
        const bool was = lg.reset();  // 연결 끊김
        printf("%d R%d\n", i, (int)was);
      }
      if (oneIn(400)) jf.reset();
      Ev out[InputLogic::MAX_EVENTS];
      const int n = lg.update(in, t, out);
      printf("%d %d%d%d%d %d %d:", i, (int)in.joyOk, (int)in.joyBtn, (int)a, (int)b, (int)in.x,
             (int)in.y);
      for (int k = 0; k < n; k++) printf(" E%d", (int)out[k]);
      printf(" T%d L%d\n", (int)lg.talking(), (int)jf.lost());
      t += 10 + (oneIn(6) ? rnd() % 60 : 0);  // 키 전송으로 틱이 늦어지는 경우
    }
  }
  return 0;
}

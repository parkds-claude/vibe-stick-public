// 기기의 loop 순서를 그대로 흉내 낸 통합 시험 (dev_sim.h).
// 단위 시험이 못 잡는 "틱 시작 시각과 나중 시각이 섞이는" 결함과, 키 전송·화면 그리기로
// loop 가 잠깐 막히는 것이 입력 판독에 주는 영향을 본다.
#include "check.h"
#include "dev_sim.h"

using namespace inputcfg;

static const int PUSH = 127;

// 앞 버튼을 누르면 OK! 가 ACTION_SHOW_MS 쯤 보였다가 대기 화면으로 돌아간다.
// (실물 결함: 동작 장면이 0초 만에 사라져 HI! 만 보였다)
static void test_action_scene_stays_visible() {
  DevSim d;
  d.run([](uint32_t t) {
    Phys p;
    p.a = (t >= 1000 && t < 1100);
    return p;
  }, 4000);
  CHECK(d.count(Ev::Confirm) == 1);
  long okAt = -1, backAt = -1;
  for (const auto& dr : d.draws) {
    if (dr.second == Scene::Ok && okAt < 0) okAt = (long)dr.first;
    if (dr.second == Scene::Ready && okAt >= 0 && backAt < 0) backAt = (long)dr.first;
  }
  CHECK(okAt >= 0 && backAt >= 0);
  const long shown = backAt - okAt;
  CHECK(shown >= (long)ScreenState::ACTION_SHOW_MS - 100);
  CHECK(shown <= (long)ScreenState::ACTION_SHOW_MS + 200);
}

// 말하기를 켜면 TALK 가 남아 있다가, 다시 누르면 대기 화면으로 돌아간다(D25)
static void test_talk_scene_follows_toggle() {
  DevSim d;
  d.run([](uint32_t t) {
    Phys p;
    p.joyBtn = (t >= 1000 && t < 1150) || (t >= 6000 && t < 6150);
    return p;
  }, 8000);
  CHECK(d.count(Ev::TalkTap) == 2);
  // 그린 장면의 순서: (처음 대기) → Talk → Ready, 그 사이에 다른 것이 없어야 한다
  std::vector<Scene> seq;
  for (const auto& dr : d.draws) seq.push_back(dr.second);
  CHECK(seq.size() == 3 && seq[0] == Scene::Ready && seq[1] == Scene::Talk && seq[2] == Scene::Ready);
  if (seq.size() == 3) {
    CHECK(d.draws[2].first >= 6000);  // 5초 동안 TALK 가 유지됐다
  }
}

// 어떤 조작 순서에서도 "나중 시각이 기준 시각보다 앞서는" 시간 비교가 생기지 않는다.
// (그런 비교는 elapsedMs 에서 "아주 오래 지남"으로 계산된다 — 오늘 세 번 나온 결함의 뿌리)
static void test_no_inverted_time_comparisons() {
  const unsigned long before = clockInversions();
  DevSim d;
  d.run([](uint32_t t) {
    Phys p;
    const uint32_t k = t / 100;      // 0.1초마다 조작이 바뀐다: 버튼·방향·누름을 고루 섞는다
    p.a = (k % 7 == 1);
    p.b = (k % 11 == 3);
    p.joyBtn = (k % 13 == 5);
    p.y = (k % 5 == 2) ? PUSH : ((k % 9 == 4) ? -PUSH : 0);
    p.x = (k % 17 == 6) ? PUSH : 0;
    return p;
  }, 60000);
  CHECK(d.events.size() > 50);  // 조작이 실제로 일어났다
  CHECK(clockInversions() == before);
}

// 누르려고 힘을 줄 때 스틱이 먼저 기울어지는 것을 걸러야 한다 — 바로 앞에 다른 버튼을 눌러
// loop 가 키 전송과 화면 그리기로 잠깐 막혀 있었더라도. (막힌 동안에는 조이스틱을 읽지 못해
// 누름을 알 기회가 없는데, 머무는 시간만 흘러가면 방향이 먼저 나가 버린다 → 창이 넘어간 뒤
// 말하기가 켜진다.)
static void test_tilt_before_click_survives_loop_stall() {
  int cases = 0, leaked = 0, talked = 0;
  for (uint32_t release = 1980; release <= 2060; release += 5) {      // 옆 버튼을 떼는 시각
    for (uint32_t lead = 0; lead <= TILT_LEAD_MS; lead += 10) {       // 기울임이 누름보다 앞서는 시간
      for (uint32_t phase = 0; phase < 20; phase += 3) {              // 틱·읽기 주기와의 어긋남
        DevSim d;
        d.t = phase;
        d.run([=](uint32_t t) {
          Phys p;
          p.b = (t >= 1800 && t < release);
          p.x = (t >= 2000) ? PUSH : 0;
          p.joyBtn = (t >= 2000 + lead);
          return p;
        }, 2600);
        ++cases;
        const long talk = d.firstAt(Ev::TalkTap);
        talked += (talk >= 0);
        for (const auto& ev : d.events) {
          if (isDirection(ev.second)) {
            ++leaked;
            break;
          }
        }
      }
    }
  }
  CHECK(leaked == 0);      // 방향 사건이 한 번도 새지 않았다
  CHECK(talked == cases);  // 말하기는 매번 켜졌다
}

// 앞 버튼을 빠르게 두 번 눌러도(질문 창에서 "고르고 → 제출") 둘째 누름이 사라지지 않는다.
// 첫 누름의 키를 보내는 동안(42ms)과 화면을 그리는 동안에는 버튼을 읽지 못하므로,
// 그 시간이 너무 길면 짧은 둘째 누름이 통째로 묻힌다.
static void test_quick_second_press_is_not_lost() {
  int cases = 0, lost = 0;
  for (uint32_t gap = 20; gap <= 120; gap += 4) {       // 첫 누름을 뗀 뒤 둘째 누름까지
    for (uint32_t phase = 0; phase < 10; phase += 3) {
      DevSim d;
      d.t = phase;
      d.run([=](uint32_t t) {
        Phys p;
        p.a = (t >= 1000 && t < 1100) || (t >= 1100 + gap && t < 1100 + gap + 80);  // 둘째는 0.08초
        return p;
      }, 2000);
      ++cases;
      lost += (d.count(Ev::Confirm) != 2);
    }
  }
  CHECK(cases > 50);
  CHECK(lost == 0);
}

int main() {
  RUN(test_action_scene_stays_visible);
  RUN(test_talk_scene_follows_toggle);
  RUN(test_no_inverted_time_comparisons);
  RUN(test_tilt_before_click_survives_loop_stall);
  RUN(test_quick_second_press_is_not_lost);
  return finish();
}

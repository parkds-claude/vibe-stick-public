#pragma once
#include <stdint.h>

#include "touch_draw.h"
#include "logic/clock.h"
#include "logic/events.h"
#include "logic/link_state.h"

// 지금 화면에 무엇을 보여야 하는가. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
// D35 말하기 표시는 "컴퓨터가 이 기기의 마이크를 실제로 열었는가"를 따른다.
//   StickC 는 자기가 ⌘D 를 보낸 횟수로 녹음 중인지 추측했다(컴퓨터가 15초 조용해 스스로 끄면 어긋났다).
//   이 기기는 USB 마이크라서, 컴퓨터가 마이크를 열고 닫는 순간을 USB 로 안다(setMicLive).
//   - 말하기를 누르면(⌘D 를 보내면) 곧바로 TALK 를 보인다 — 손끝의 반응이 늦으면 안 된다.
//   - 컴퓨터가 마이크를 열면 그때부터는 사실을 따른다: 열려 있는 동안 TALK, 닫히면 내린다.
//   - 열지 않을 때의 일은 아래 D65(앞선 판은 3초 뒤 `Mic didn't open` 을 띄웠다).
//   - 키보드로 ⌘D 를 눌러 켠 경우에도 마이크가 열리면 TALK 를 보인다.
//   비용: 다른 프로그램(화상 회의, 소리 설정 창)이 이 마이크를 열어 두면 그동안 내내 TALK 로 보인다.
//
// D65 사실을 알 수 없을 때는 "말하기를 눌렀다"로 미루어 듣는 중을 보인다.
//   써 보니: 받아쓰는 중이라는 것이 눈에 띄어야 한다 → (빨갛게 고친 뒤에도) 말하기를 눌렀는데 빨갛게 바뀌지 않는다는
//           지적이 거듭 나왔다.
//   까닭: 컴퓨터의 소리 입력이 이 기기가 아니거나(그 컴퓨터의 마이크·에어팟으로 듣는다) 블루투스로 쓰면
//         컴퓨터는 이 기기의 마이크를 열지 않는다. 받아쓰기는 되는데 화면은 영영 듣는 중이 되지 않았다
//         (앞선 판: 3초 뒤 "Mic didn't open"). 쓰는 사람에게는 "눌렀는데 표시가 없다"일 뿐이다.
//   규칙: - 말하기를 누르면 곧바로 듣는 중(미룸). 컴퓨터가 이 기기의 마이크를 열면 그때부터 사실을 따른다.
//         - 미룸은 이럴 때 내린다: 말하기를 다시 누름 · 확인(Enter)을 누름 · GUESS_QUIET_MS 동안 말소리가 없음
//           (컴퓨터의 받아쓰기는 조용하면 스스로 꺼진다 — 기기의 마이크가 방의 소리에서 말소리를 가려 가늠한다:
//           logic/speech_ear.h) · GUESS_MAX_MS 가 지남 · 연결이 끊김 · 길이 바뀜(선 ↔ 블루투스) · 키가 나가지 않음.
//         - 그 밖의 버튼(▲▼ · 창 넘기기 · 취소)으로는 내리지 않는다: 받아쓰기가 끝났는지 모르면 켜진 쪽으로
//           둔다(꺼진 줄 알고 말하는 것보다 켜진 줄 알고 조심하는 편이 낫다).
//   화면: 미룸도 사실과 같은 빨강이고, 상태 막대의 점만 다르다(사실 = 채운 점, 미룸 = 빈 고리).
//   비용: 미룸은 틀릴 수 있다 — 누른 ⌘D 가 Claude 입력창에 닿지 않았어도, 컴퓨터가 먼저 받아쓰기를 껐어도
//         한동안(길어야 15초 조용할 때까지) 빨갛게 남는다. "Mic didn't open" 안내는 더 뜨지 않으므로,
//         기기 마이크로 말이 들어가고 있는지는 점의 모양으로만 알 수 있다.
//   탈출구: onSent 의 말하기 갈래에서 guess_ 를 켜지 않으면 앞선 판(사실만 보인다)이다.
//
// D71 미룸이 실제와 어긋났을 때, 키를 보내지 않고 화면만 바로잡는 길을 둔다(flipGuess).
//   써 보니: 빨간데 꺼져 있고, 빨갛지 않은데 받아쓰는 중인 "반대" 상태가 생겼다.
//   까닭: 미룸은 "말하기를 누른 횟수"로 센다. 컴퓨터가 먼저 받아쓰기를 끄거나(조용함) 키보드로 켜고 끄면
//         한 번 어긋나고, 그 뒤로는 말하기를 누를 때마다 반대로 보인다. 말하기를 눌러서는 고칠 수 없다
//         (누르면 실제도 함께 뒤집힌다).
//   규칙: 보는 곳(캐릭터가 있는 위쪽)을 톡 치면 듣는 중 표시만 뒤집는다 — 키는 나가지 않는다.
//         컴퓨터가 이 기기의 마이크를 연 동안(사실을 안다)·연결이 없을 때·펌웨어 물음이 뜬 동안에는 듣지 않는다.
//         Claude 의 알림이 떠 있으면 그 알림만 거둔다. 어두워졌거나 꺼진 화면을 깨운 톡, 뜸 동안의 톡은
//         여기까지 오지 않는다(logic/view_tap_guard.h).
//         톡으로 켠 미룸도 말하기로 켠 것과 똑같이 내려간다(조용함 15초 · 2분 · 확인 · 말하기).
//   비용: 위쪽에 잘못 닿은 손가락이 표시를 뒤집을 수 있다(그때는 한 번 더 톡). 선으로 쓰며 기기의 마이크가
//         컴퓨터의 입력일 때(사실을 따르는 때)에도, 쉬는 화면을 톡 치면 15초까지 빈 고리의 빨강이 뜬다.
//   탈출구: main.cpp 에서 takeViewTap() 을 읽지 않으면 앞선 판이다.
//
// D74 사실을 따르는 중에는 톡이 빨강을 "켜지" 않는다.
//   D71 을 기기에서 써 본 직후: 캐릭터를 눌러도 받아쓰기가 켜지는 것으로 읽혔다 — 선으로 쓰고 맥의 소리 입력이 이
//   기기라서 화면이 이미 사실을 따르고 있었는데(말하기 → 0.15초 뒤 마이크 열림 → 맥이 끄자 화면도 꺼짐), 캐릭터를
//   치자 녹음도 안 되는데 화면이 빨개졌다. 빨강은 "듣는 중"이라는 뜻이라, 사람은 녹음이 켜졌다고 읽는다.
//   규칙: 말하기를 눌러 미룸을 켠 뒤 TRUTH_WINDOW_MS 안에 컴퓨터가 이 기기의 마이크를 열면 "이 컴퓨터는 이 기기의
//         마이크로 받아쓴다"가 확인된 것이다(truthKnown_). 그동안에는 쉬는 화면의 톡이 미룸을 켜지 않는다 —
//         꺼져 있다는 것도 사실이기 때문이다. 켜져 있는 미룸을 끄는 톡, 알림을 거두는 톡은 그대로 받는다.
//         확인은 이럴 때 지운다: 말하기를 눌렀는데 그 시간 안에 마이크가 열리지 않음(컴퓨터의 입력이 바뀌었다) ·
//         연결이 끊김 · 길이 바뀜(선 ↔ 블루투스). 무선에서는 확인이 생기지 않는다.
//   비용: 말하기가 Claude 입력창에 닿지 않아(커서가 다른 곳) 마이크가 열리지 않아도 확인이 지워진다 → 그 뒤로는
//         다음 말하기가 확인될 때까지 D71 그대로다(톡이 빨강을 켠다). 선을 꽂은 뒤 첫 말하기 전에도 그렇다.
//         확인이 틀린 채 남는 길(독립 검토 — 둘 다 다음 말하기에서 3초 안에 스스로 풀린다):
//         (가) 확인된 뒤 컴퓨터의 소리 입력이 다른 마이크로 바뀌었고(에어팟을 끼움 등), 그 받아쓰기를 이 기기가 아니라
//              컴퓨터의 키보드로 켰다 → 빨갛지 않은데 받아쓰는 중이고, 톡으로 빨강을 켤 수 없다.
//         (나) 컴퓨터가 다른 마이크로 받아쓰는데, 말하기를 누른 뒤 3초 안에 다른 프로그램이 이 기기의 마이크를 열었다.
//   탈출구: flipGuess 의 `if (truthKnown_) return Ignored;` 한 줄을 지우면 D71 그대로다.
namespace touchui {

class TouchState {
 public:
  static const uint32_t ACTION_SHOW_MS = 1500;   // 동작 장면을 보이는 시간(D24 와 같다)
  // D65 미루어 듣는 중을 내리는 때
  static const uint32_t GUESS_QUIET_MS = 15000;   // 이만큼 조용하면 컴퓨터의 받아쓰기가 스스로 꺼졌다고 본다
  static const uint32_t GUESS_MAX_MS   = 120000;  // 말이 끊이지 않아도 여기까지(받아쓰기의 가장 긴 길이)
  // D74 말하기를 누른 뒤 이 안에 컴퓨터가 이 기기의 마이크를 열면 "이 기기의 마이크로 받아쓴다"로 확인한다.
  // (맥북: 누른 뒤 0.14초에 열렸다. 넉넉히 잡는다 — 그보다 한참 뒤에 열린 것은 다른 프로그램이다.)
  static const uint32_t TRUTH_WINDOW_MS = 3000;
  static const uint32_t WAVE_STEP_MS   = 90;     // 목소리가 들어오는 동안 파형이 한 칸 흐르는 시간(초당 11번)
  static const uint32_t UPDATE_WAIT_MS = 20000;  // 펌웨어 물음(`Update firmware?`)에 답하기를 기다리는 시간
  // 펌웨어 물음이 뜬 뒤 이만큼은 확인을 받지 않는다. 다른 일로 확인을 누르려던 손가락이, 그 순간에
  // 뜬 물음에 "예"라고 답해 버리는 것을 막는다(사람이 화면을 보고 알아챌 시간).
  static const uint32_t UPDATE_ARM_MS  = 1500;

  // D47 컴퓨터가 펌웨어를 바꾸겠다고 하면(vibe:boot) 화면에서 사람이 확인(OK)을 눌러야 진행한다.
  //   이유: 그냥 따르면 컴퓨터의 아무 프로그램이나 이 기기를 조용히 다시 구울 수 있다. 이 기기는
  //         키보드라서, 다시 구운 펌웨어는 무엇이든 칠 수 있게 된다(D43 으로 막은 것이 뚫린다).
  //   비용: 펌웨어를 올릴 때마다 화면을 한 번 눌러야 한다(개발판은 묻지 않는다 — config.h).
  //   탈출구: 기기의 RST 버튼 3초는 언제나 된다.
  // 물음이 끝난 뒤(거절·시간 초과) 이만큼은 다시 묻지 않는다. 그리고 묻는 중에는 새 요청을 무시한다.
  // 이것이 없으면 컴퓨터의 프로그램이 요청을 계속 보내 화면을 물음에 묶어 둘 수 있다
  // (묻는 동안에는 어떤 누름도 키로 나가지 않으므로 기기를 못 쓰게 된다).
  static const uint32_t UPDATE_COOLDOWN_MS = 10000;

  // 묻기 시작한다. 묻는 동안에는 확인·취소가 키로 나가지 않는다(main 이 가로챈다).
  // 반환: 물음을 띄웠으면 true. 이미 묻는 중이거나 방금 끝난 직후라 무시했으면 false.
  bool askUpdate(uint32_t nowMs) {
    if (updateAsking(nowMs)) return false;  // 시계를 다시 돌리지 않는다
    if (updateEnded_ && !reachedMs(nowMs, updateEndedMs_ + UPDATE_COOLDOWN_MS)) return false;
    updateAsk_ = true;
    updateAtMs_ = nowMs;
    actionOn_ = false;
    return true;
  }
  void cancelUpdate(uint32_t nowMs) {
    if (updateAsk_) endUpdate(nowMs);
  }
  // 지금 확인(OK)을 "예"로 받아도 되는가: 묻는 중이고, 뜬 지 UPDATE_ARM_MS 가 지났다.
  bool updateArmed(uint32_t nowMs) {
    return updateAsking(nowMs) && reachedMs(nowMs, updateAtMs_ + UPDATE_ARM_MS);
  }
  // 지금 묻고 있는가(시간이 다 되면 저절로 거둔다)
  bool updateAsking(uint32_t nowMs) {
    if (updateAsk_ && reachedMs(nowMs, updateAtMs_ + UPDATE_WAIT_MS)) endUpdate(updateAtMs_ + UPDATE_WAIT_MS);
    return updateAsk_;
  }

  // 컴퓨터가 기기를 알아보고도 이만큼 쓰기 시작하지 않으면 `Allow on your Mac` 을 띄운다.
  // (평소에는 알아본 뒤 1초 안에 쓰기 시작한다. 그보다 오래 걸리면 컴퓨터가 사람의 허락을 기다리는 것이다:
  //  맥북은 처음 보는 USB 액세서리를 꽂으면 "연결을 허용하겠습니까?" 를 묻고, 허용 전에는 쓰지 않는다.)
  static const uint32_t ALLOW_AFTER_MS = 2500;

  // 컴퓨터와의 연결 상태:
  //   Ready      키를 보낼 수 있다
  //   Connecting 컴퓨터가 기기를 알아봤지만(설명자를 읽어 갔다) 아직 쓰기 시작하지 않았다
  //   그 밖      컴퓨터가 없다(뽑힘·잠듦)
  // wireless: 그 상태가 블루투스의 것인가(D59). 블루투스로 쓰는 동안에는 컴퓨터가 마이크를 열었는지 알 수 없다.
  void setLink(LinkState link, uint32_t nowMs, bool wireless = false) {
    const bool ready = (link == LinkState::Ready);
    if (wireless != wireless_) endGuess(GuessEnd::RouteChanged);  // 길이 바뀌었다(선을 뽑았다·꽂았다): 받아쓰기가 어찌 됐는지 모른다
    // D74 길이 바뀌었거나 끊겼다: 그 컴퓨터가 어느 마이크로 듣는지 다시 모른다.
    if (wireless != wireless_ || !ready) truthKnown_ = talkFresh_ = false;
    wireless_ = wireless;
    if (link == LinkState::Connecting && link_ != LinkState::Connecting) connectingAtMs_ = nowMs;
    link_ = link;
    ready_ = ready;
    if (!ready) endGuess(GuessEnd::LinkDown);  // 끊기면 듣는 중도 끝이다
    // 연결됐는데 "키가 나가지 않았다"(`Key not sent`) 장면이 남아 있으면 어색하다 → 바로 내린다.
    if (ready && actionOn_ && action_ == Face::NoLink) actionOn_ = false;
  }
  // (시험과 옛 코드용) 쓸 수 있음 / 컴퓨터 없음 둘 중 하나로 정한다.
  void setReady(bool ready) { setLink(ready ? LinkState::Ready : LinkState::Searching, 0); }

  // 지금 눌려 있는 자리들(bit(Zone))
  void setPressed(uint8_t mask) { pressed_ = mask; }
  // 개발판인가(화면 맨 위에 표시 띠를 그린다 — touch_draw.h 의 Frame::dev)
  void setDev(bool dev) { dev_ = dev; }

  // 컴퓨터가 이 기기의 마이크를 열었다/닫았다
  void setMicLive(bool live) {
    micLive_ = live;
    if (!live) return;
    if (talkFresh_) {  // D74 말하기를 누른 직후에 열렸다: 이 컴퓨터는 이 기기의 마이크로 받아쓴다
      truthKnown_ = true;
      talkFresh_ = false;
    }
    endGuess(GuessEnd::MicOpened);  // 이제 사실을 안다(닫히면 내려간다)
  }
  bool micLive() const { return micLive_; }
  // D74 "말하기를 누르면 컴퓨터가 이 기기의 마이크를 연다"가 확인됐는가(그동안에는 화면이 사실만 보인다)
  bool truthKnown() const { return truthKnown_; }

  // 지금 목소리가 들어오고 있는가(입을 여닫는다)
  void setVoice(bool on) { voice_ = on; }
  // 지금 말소리가 들리는가(방의 소리에서 가려낸 것 — logic/speech_ear.h). 미루어 듣는 중(D65)을 이어 가고,
  // 그동안 입·파형을 움직이는 데 쓴다. setVoice 의 문턱(입 여닫기용)은 방의 평소 소리에도 걸려, 이것으로
  // 미룸을 이어 갔더니 빨간 화면이 꺼지지 않았다.
  void setHeard(bool on) { heard_ = on; }
  static const uint32_t HEARD_HOLD_MS = 200;  // 미루는 동안: 말소리가 그친 뒤 입을 닫기까지(낱말 사이의 틈을 잇는다)

  // 미루어 듣는 중이 왜 내려갔는가(기기의 기록에 남긴다). 한 번 읽으면 지워진다.
  //   번호는 기기의 기록(`[guess] end why=N`)에 그대로 찍힌다 — 끝에만 덧붙일 것.
  enum class GuessEnd : uint8_t {
    None, Again, Confirm, TalkUp, Quiet, Max, LinkDown, RouteChanged, MicOpened, NotSent,
    Fixed,  // 10: 보는 곳을 톡 쳐서 화면만 바로잡았다(D71)
  };
  GuessEnd takeGuessEnd() {
    const GuessEnd e = guessEnd_;
    guessEnd_ = GuessEnd::None;
    return e;
  }
  bool guessing() const { return guess_; }

  // 배터리 남은 양(0~100, 모르면 음수)과 충전 중인지. 상태 줄에 보인다(D54).
  // 전원 칩이 엉뚱한 값을 주어도 화면에는 0~100 만 나간다.
  void setBattery(int pct, bool charging) {
    battery_ = (int8_t)(pct < 0 ? -1 : pct > 100 ? 100 : pct);
    charging_ = (pct >= 0) && charging;
  }

  // 이 사건의 키를 보냈다.
  void onSent(Ev e, uint32_t nowMs) {
    switch (e) {
      case Ev::TalkTap:
      case Ev::TalkDown:
        actionOn_ = false;  // 떠 있던 다른 장면을 접어 곧바로 보이게 한다
        if (micLive_) return;  // 사실로 듣는 중이었다 → "꺼 달라"고 한 것이다: 닫히면 저절로 내려간다
        // D74 이 누름 뒤에 곧 마이크가 열리는지 본다(무선에서는 열릴 수 없다). 미룸을 켠 누름이든 끈 누름이든 같다:
        // 기기가 "껐다"로 읽은 누름이 실제로는 받아쓰기를 켠 것일 수 있고(톡으로 켜 둔 미룸 위에서 누름), 그때 열리는
        // 마이크도 같은 확인이다. 정말 끈 것이라면 열리지 않는다 → 그것은 "다른 마이크로 듣는다"는 뜻이다.
        talkFresh_ = !wireless_;
        talkAtMs_ = nowMs;
        if (guess_) {          // 미루어 듣는 중이었다 → 다시 눌렀으니 껐다
          endGuess(GuessEnd::Again);
          return;
        }
        guess_ = true;  // 켰다(D65). 컴퓨터가 이 기기의 마이크를 열면 사실로 바뀐다
        guessAtMs_ = nowMs;
        quietAtMs_ = nowMs;
        return;
      case Ev::TalkUp:  // (누르는 동안만 말하는 방식) 뗐다
        endGuess(GuessEnd::TalkUp);
        return;
      case Ev::Confirm:  // Enter: 녹음 중이었다면 컴퓨터가 녹음을 끄고 글을 보낸다
        endGuess(GuessEnd::Confirm);
        show(Face::Ok, nowMs);
        return;
      case Ev::Cancel:     show(Face::Nope, nowMs); return;
      case Ev::Up:         show(Face::Up, nowMs);   return;
      case Ev::Down:       show(Face::Down, nowMs); return;
      case Ev::WindowNext: show(Face::Next, nowMs); return;
      case Ev::WindowPrev: show(Face::Back, nowMs); return;
      case Ev::PairReset:  // USB 에는 짝이 없다(보내지 않는다)
      case Ev::None:
        return;
    }
  }

  // D71 화면만 바로잡는다(보는 곳을 톡 쳤다): 키는 나가지 않았고, 듣는 중 표시만 뒤집는다.
  //   - 펌웨어 물음이 뜬 동안에는 듣지 않는다(Ignored).
  //   - Claude 의 알림(`Claude is done` · `Claude needs you`)이 떠 있으면 그 알림만 거둔다(NoteCleared) — 알림을
  //     보려고 만진 손이 빨강까지 켜면 안 된다(독립 검토). 그다음 톡부터 뒤집는다.
  //   - 사실을 아는 동안 · 연결이 없을 때에는 뒤집지 않는다(Ignored).
  //   - 잠깐 떠 있는 다른 장면(방금 누른 버튼의 표정 · `Key not sent`)은 건드리지 않는다: 빨강은 그 장면 위에서도
  //     곧바로 바뀌고, 장면은 제 시간에 내려간다.
  enum class ViewTap : uint8_t { Ignored, Flipped, NoteCleared };
  //   - 사실을 따르는 중(D74 — 말하기를 누르면 컴퓨터가 이 기기의 마이크를 여는 것이 확인됐다)에는 켜지 않는다.
  ViewTap flipGuess(uint32_t nowMs) {
    settleTalk(nowMs);
    if (updateAsk_) return ViewTap::Ignored;
    // 알림은 연결이 없어도, 사실로 듣는 중이어도 거둘 수 있다(못 거두면 60초 동안 화면을 가린다 — 통합 점검).
    if (actionOn_ && (action_ == Face::Ask || action_ == Face::Done)) {
      actionOn_ = false;
      return ViewTap::NoteCleared;
    }
    if (!ready_ || micLive_) return ViewTap::Ignored;
    if (guess_) {
      endGuess(GuessEnd::Fixed);
      return ViewTap::Flipped;
    }
    if (truthKnown_) return ViewTap::Ignored;  // 꺼져 있다는 것도 사실이다
    guess_ = true;
    guessAtMs_ = nowMs;
    quietAtMs_ = nowMs;
    return ViewTap::Flipped;
  }

  // 보내려 했으나 연결이 없었다. 키가 나가지 않는다면 받아쓰기를 켜고 끌 길도 없다 → 미루어 듣는 중도 접는다
  // (끄려고 누른 말하기가 나가지 않아, 끌 수 없는 빨강이 남는 일이 없게).
  void onNotSent(uint32_t nowMs) {
    endGuess(GuessEnd::NotSent);
    show(Face::NoLink, nowMs);
  }

  // D62 캐릭터 옆에 보일 시각(자정부터의 분). 음수면 보이지 않는다(시각을 모르거나, 쉬는 중이 아니다).
  void setClock(int minutesOfDay) { clock_ = (int16_t)((minutesOfDay >= 0 && minutesOfDay < 24 * 60) ? minutesOfDay : -1); }

  // D63 컴퓨터의 Claude 가 알려 왔다: 일을 끝냈다(Done) / 사람의 답을 기다린다(Ask).
  //   끝났다는 알림은 잠깐, 기다린다는 알림은 사람이 무언가 누를 때까지 오래 보인다(누르면 그 장면이 대신한다).
  static const uint32_t NOTE_DONE_MS = 6000;
  static const uint32_t NOTE_ASK_MS  = 60000;
  enum class Note : uint8_t { Done, Ask };
  void onNote(Note n, uint32_t nowMs) {
    if (updateAsk_) return;  // 펌웨어 물음이 떠 있는 동안에는 가리지 않는다
    show(n == Note::Done ? Face::Done : Face::Ask, nowMs, n == Note::Done ? NOTE_DONE_MS : NOTE_ASK_MS);
  }

  // 블루투스의 짝을 지우려 했다(확인+취소 3초). ok = 실제로 지워졌다(곧 재시작한다).
  // 지우지 못했으면 `Pairing not cleared` — "키가 나가지 않았다"(NoLink)와는 다른 일이다(D72).
  void onPairReset(bool ok, uint32_t nowMs) { show(ok ? Face::PairCleared : Face::PairFail, nowMs); }

  // 지금 보여야 할 화면이 마지막으로 내준 것과 다르면 out 에 담고, 다시 그릴 곳(dirtyOut)과 함께
  // true 를 돌려준다. 처음에는 전체(DIRTY_ALL)다.
  bool poll(uint32_t nowMs, Frame& out, Dirty& dirtyOut) {
    // nowMs 는 그 틱이 시작될 때의 시각이라, 키를 다 보낸 뒤에 기록한 시각보다 조금 앞설 수 있다.
    // reachedMs 는 그런 경우를 "아직"으로 본다(elapsedMs 를 쓰면 곧바로 끝난다 — clock.h).
    // 지난 시각은 쓸모가 다하면 버린다: 들고 있다가 시계가 반 바퀴(약 24.8일) 돌면 "마감에 이르렀나"가
    // 거꾸로 읽힌다(26일 전의 말소리가 "방금"이 되고, 26일 전에 거절한 물음이 새 물음을 막는다).
    if (heard_) {
      heardAtMs_ = nowMs;
      heardSeen_ = true;
    } else if (heardSeen_ && reachedMs(nowMs, heardAtMs_ + HEARD_HOLD_MS)) {
      heardSeen_ = false;
    }
    if (updateEnded_ && !updateAsk_ && reachedMs(nowMs, updateEndedMs_ + UPDATE_COOLDOWN_MS)) updateEnded_ = false;
    settleTalk(nowMs);
    if (guess_) {
      if (heard_) quietAtMs_ = nowMs;  // 말소리가 들린다
      if (reachedMs(nowMs, guessAtMs_ + GUESS_MAX_MS)) {
        endGuess(GuessEnd::Max);
      } else if (reachedMs(nowMs, quietAtMs_ + GUESS_QUIET_MS)) {
        endGuess(GuessEnd::Quiet);
      }
    }
    const bool live = micLive_ || guess_;  // 듣는 중으로 보이는가(사실이거나 미룸)
    // 입·파형을 움직일 소리: 사실로 듣는 중이면 컴퓨터가 받는 소리의 크기, 미루는 중이면 가려낸 말소리.
    const bool speaking = micLive_ ? voice_ : heardSeen_;
    if (actionOn_ && reachedMs(nowMs, actionAtMs_ + actionMs_)) actionOn_ = false;

    Frame f{};
    if (updateAsking(nowMs)) {
      f.face = Face::Update;  // 다른 무엇보다 앞선다: 답할 때까지 얼굴에 떠 있다
    } else if (actionOn_) {
      f.face = action_;
    } else if (!ready_) {
      if (link_ == LinkState::Pairing) {
        f.face = Face::Pair;  // 선도 없고 짝도 없다: 블루투스 설정에서 연결하라고 알린다(D59)
      } else if (link_ == LinkState::Connecting && wireless_) {
        f.face = Face::Hi;    // 블루투스가 붙는 중(곧 끝난다): 사람이 할 일이 없다
      } else {
        const bool waiting = (link_ == LinkState::Connecting) && reachedMs(nowMs, connectingAtMs_ + ALLOW_AFTER_MS);
        f.face = waiting ? Face::Allow : Face::Plug;
      }
    } else {
      f.face = live ? Face::Talk : Face::Hi;
    }
    f.pressed = pressed_;
    f.micLive = live;
    f.guess = guess_ && !micLive_;
    f.mouthOpen = (f.face == Face::Talk) && live && speaking;
    f.wave = f.mouthOpen ? (uint8_t)((nowMs / WAVE_STEP_MS) % WAVE_STEPS) : 0;
    f.clockMin = (f.face == Face::Hi && !live) ? clock_ : (int16_t)-1;  // 시각은 쉬는 장면에서만
    f.status.link = link_;
    f.status.wireless = wireless_;
    f.status.batteryPct = battery_;
    f.status.charging = charging_;
    f.dev = dev_;
    if (drawn_ && f == last_) return false;
    dirtyOut = drawn_ ? dirty(last_, f) : DIRTY_ALL;
    last_ = f;
    drawn_ = true;
    out = f;
    return true;
  }

 private:
  void endUpdate(uint32_t atMs) {
    updateAsk_ = false;
    updateEnded_ = true;
    updateEndedMs_ = atMs;
  }

  void endGuess(GuessEnd why) {
    if (!guess_) return;
    guess_ = false;
    guessEnd_ = why;
  }

  // D74 말하기를 누른 뒤 TRUTH_WINDOW_MS 가 지나도록 이 기기의 마이크가 열리지 않았다: 컴퓨터는 다른 마이크로
  // 듣는다(또는 그 누름이 받아쓰기에 닿지 않았다) → 확인을 지운다. 지난 시각은 여기서 버린다(들고 있지 않는다).
  void settleTalk(uint32_t nowMs) {
    if (talkFresh_ && reachedMs(nowMs, talkAtMs_ + TRUTH_WINDOW_MS)) {
      talkFresh_ = false;
      truthKnown_ = false;
    }
  }

  void show(Face f, uint32_t nowMs, uint32_t forMs = ACTION_SHOW_MS) {
    action_ = f;
    actionAtMs_ = nowMs;
    actionMs_ = forMs;
    actionOn_ = true;
  }

  bool     ready_      = false;
  bool     wireless_   = false;  // link_ 가 블루투스의 것인가
  LinkState link_      = LinkState::Searching;
  uint32_t connectingAtMs_ = 0;  // "알아봤지만 아직"이 시작된 시각
  uint8_t  pressed_    = 0;
  bool     dev_        = false;  // 개발판(DEV_BUILD=1)
  bool     micLive_    = false;
  bool     voice_      = false;
  int8_t   battery_    = -1;     // 배터리 남은 양(%). 아직 읽지 못했으면 -1
  bool     charging_   = false;
  bool     guess_      = false;  // 말하기를 눌렀으니 듣고 있을 것이라고 미루는 중(D65)
  uint32_t guessAtMs_  = 0;      // 미루기 시작한 시각
  uint32_t quietAtMs_  = 0;      // 마지막으로 말소리가 들린 시각(미루는 동안)
  bool     truthKnown_ = false;  // D74 말하기를 누르면 컴퓨터가 이 기기의 마이크를 여는 것이 확인됐다
  bool     talkFresh_  = false;  // 말하기로 미룸을 켠 뒤 아직 TRUTH_WINDOW_MS 가 지나지 않았다(열리는지 보는 중)
  uint32_t talkAtMs_   = 0;      // 그 말하기를 누른 시각
  GuessEnd guessEnd_   = GuessEnd::None;  // 미룸이 내려간 까닭(아직 읽어 가지 않은 것)
  bool     heard_      = false;  // 지금 말소리가 들린다(setHeard)
  bool     heardSeen_  = false;  // heardAtMs_ 에 값이 들어 있다
  uint32_t heardAtMs_  = 0;      // 마지막으로 말소리가 들린 시각
  Face     action_     = Face::Hi;
  uint32_t actionAtMs_ = 0;
  uint32_t actionMs_   = ACTION_SHOW_MS;  // 지금 떠 있는 장면을 보이는 시간
  int16_t  clock_      = -1;              // 보일 시각(자정부터의 분). 없으면 -1
  bool     actionOn_   = false;
  bool     updateAsk_  = false;  // 펌웨어 물음(`Update firmware?`)을 묻는 중
  uint32_t updateAtMs_ = 0;
  bool     updateEnded_ = false;  // 물음이 끝난 적이 있다(끝난 시각이 updateEndedMs_)
  uint32_t updateEndedMs_ = 0;
  Frame    last_{};
  bool     drawn_      = false;
};

// 목소리가 들어오고 있는지 가른다(입을 여닫는 데 쓴다). 소리 크기는 한 덩어리의 RMS.
// 켜지는 문턱과 꺼지는 문턱을 달리하고, 꺼질 때는 조금 기다려 입이 파르르 떨지 않게 한다.
class VoiceGate {
 public:
  VoiceGate(uint16_t onLevel, uint16_t offLevel, uint32_t holdMs)
      : on_(onLevel), off_(offLevel), holdMs_(holdMs) {}

  // 한 덩어리의 크기를 넣는다. 반환: 지금 목소리가 들어오는가
  bool feed(uint16_t rms, uint32_t nowMs) {
    if (rms >= on_) {
      open_ = true;
      quietSinceMs_ = nowMs;
    } else if (open_) {
      if (rms >= off_) {
        quietSinceMs_ = nowMs;
      } else if (elapsedMs(nowMs, quietSinceMs_, holdMs_)) {
        open_ = false;
      }
    }
    return open_;
  }
  bool open() const { return open_; }
  void reset() { open_ = false; }

 private:
  const uint16_t on_, off_;
  const uint32_t holdMs_;
  bool           open_ = false;
  uint32_t       quietSinceMs_ = 0;
};

}  // namespace touchui

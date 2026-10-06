#include "keymap.h"

KeyAction mapEvent(Ev e, HostOs os) {
  // 말하기 단축키: 맥 ⌘D, 윈도우 Ctrl+D (Claude Code VS Code 확장의 음성 받아쓰기)
  const uint8_t talkMod = (os == HostOs::Mac) ? hid::MOD_LGUI : hid::MOD_LCTRL;
  switch (e) {
    case Ev::TalkDown: return {KeyAction::Press, talkMod, hid::KEY_D};
    case Ev::TalkUp:   return {KeyAction::Release, 0, 0};
    case Ev::TalkTap:  return {KeyAction::Tap, talkMod, hid::KEY_D};
    case Ev::Confirm:  return {KeyAction::Tap, 0, hid::KEY_ENTER};
    case Ev::Cancel:   return {KeyAction::Tap, 0, hid::KEY_ESC};
    case Ev::Up:       return {KeyAction::Tap, 0, hid::KEY_UP};
    case Ev::Down:     return {KeyAction::Tap, 0, hid::KEY_DOWN};
    // D21: 같은 앱의 다음 창·이전 창으로 바로 이동(목록 없이). 맥의 기본 단축키 ⌘` / ⇧⌘`.
    // 윈도우에는 같은 뜻의 기본 단축키가 없어 아직 보내지 않는다.
    case Ev::WindowNext:
      if (os == HostOs::Mac) return {KeyAction::Tap, hid::MOD_LGUI, hid::KEY_GRAVE};
      return {KeyAction::None, 0, 0};
    case Ev::WindowPrev:
      if (os == HostOs::Mac) {
        return {KeyAction::Tap, (uint8_t)(hid::MOD_LGUI | hid::MOD_LSHIFT), hid::KEY_GRAVE};
      }
      return {KeyAction::None, 0, 0};
    case Ev::PairReset:
    case Ev::None:
      return {KeyAction::None, 0, 0};
  }
  return {KeyAction::None, 0, 0};
}

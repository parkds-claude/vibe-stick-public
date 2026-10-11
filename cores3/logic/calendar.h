#pragma once

// 컴퓨터가 알려 온 날짜·시각(vibe:time)을 시계 칩에 쓰기 전에 본다. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//   앞선 판은 "일 1~31"까지만 봐서 2월 31일 같은 날짜가 시계 칩에 들어갈 수 있었다(보안 점검).
//   받는 해는 2026~2099 (시계 칩의 해는 두 자리다).
namespace touchui {

// 그 달의 날 수. 달이 1~12 가 아니면 0.
inline int daysInMonth(int year, int month) {
  if (month < 1 || month > 12) return 0;
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return (month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31;
}

inline bool validDateTime(int year, int month, int day, int hour, int minute, int second) {
  if (year < 2026 || year > 2099) return false;
  if (day < 1 || day > daysInMonth(year, month)) return false;
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

}  // namespace touchui

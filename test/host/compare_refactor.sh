#!/bin/sh
# 구조 정리(리팩토링) 전후에 순수 로직의 동작이 그대로인지 확인한다.
#
#   test/host/compare_refactor.sh [기준 커밋]     (기준을 안 주면 HEAD)
#
# 기준 커밋의 src/logic 과 지금 작업 중인 src/logic 에 같은 무작위 입력(1000시드 x 4000틱)을
# 흘려 넣고 출력이 한 줄도 다르지 않은지 본다. 동작을 일부러 바꾼 경우에는 당연히 다르게 나온다.
# 무작위 입력 도구는 "지금 것"을 양쪽에 쓰므로, 기준 커밋과 InputLogic·JoyFilter 의
# 공개 함수가 같아야 한다(다르면 기준 쪽 빌드가 실패한다).
set -e
cd "$(dirname "$0")/../.."
base="${1:-HEAD}"
# 일부 맥에서는 Xcode(구형 링커)와 명령줄 도구(새 SDK)가 섞여 있다 — run.sh 와 같은 우회
if [ -d /Library/Developer/CommandLineTools/usr/bin ]; then
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi
work=.pio/host/compare
rm -rf "$work"
mkdir -p "$work/base"
git archive "$base" src/logic | tar -x -C "$work/base"
build() {  # $1 = src 폴더, $2 = 결과 파일
  c++ -std=c++17 -O1 -I "$1" test/host/fuzz_trace.cpp "$1/logic/input_logic.cpp" \
      "$1/logic/joy_filter.cpp" -o "$2"
}
build "$work/base/src" "$work/fuzz_base"
build src "$work/fuzz_now"
"$work/fuzz_base" 1000 > "$work/trace_base.txt"
"$work/fuzz_now" 1000 > "$work/trace_now.txt"
if cmp -s "$work/trace_base.txt" "$work/trace_now.txt"; then
  echo "같음: $base 과 지금의 동작이 $(wc -l < "$work/trace_now.txt" | tr -d ' ')줄 모두 일치"
else
  echo "다름: 처음 다른 곳"
  diff "$work/trace_base.txt" "$work/trace_now.txt" | head -6
  exit 1
fi

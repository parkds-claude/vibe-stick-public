#!/bin/sh
# 순수 로직(src/logic)을 컴퓨터에서 빌드해 시험한다. 기기가 필요 없다. (D13)
set -e
cd "$(dirname "$0")/../.."
# 일부 맥에서는 Xcode(구형 링커)와 명령줄 도구(새 SDK)가 섞여 기본 설정으로 링크가 실패한다
# ("tapi error: unknown architecture"). 명령줄 도구가 있으면 그쪽으로 통일한다. 리눅스에서는 이 분기가 비켜 간다.
if [ -d /Library/Developer/CommandLineTools/usr/bin ]; then
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi
mkdir -p .pio/host
FLAGS="-std=c++17 -Wall -Wextra -Werror -I src"
c++ $FLAGS test/host/test_logic.cpp src/logic/*.cpp -o .pio/host/test_logic
.pio/host/test_logic
# 화면 그리기·화면 상태(글자가 말풍선 안에 들어가는지, 화면 밖에 그리지 않는지 등)
c++ $FLAGS test/host/test_screen.cpp src/logic/*.cpp -o .pio/host/test_screen
.pio/host/test_screen
# 같은 시험을 기본이 아닌 마스코트로도 한 번(빌드 때 고르는 길이 살아 있는지, D26)
c++ $FLAGS -DMASCOT_ID=Robot test/host/test_screen.cpp src/logic/*.cpp -o .pio/host/test_screen_robot
.pio/host/test_screen_robot | tail -1
# 기기의 loop 순서를 흉내 낸 통합 시험(시각이 섞이는 결함, loop 가 막힐 때의 입력 판독)
c++ $FLAGS -DCLOCK_AUDIT test/host/test_loop.cpp src/logic/*.cpp -o .pio/host/test_loop
.pio/host/test_loop

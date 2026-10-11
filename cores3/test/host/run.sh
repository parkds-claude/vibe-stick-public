#!/bin/sh
# CoreS3 판의 순수 로직(cores3/logic + 함께 쓰는 src/logic)을 컴퓨터에서 빌드해 시험한다.
# 기기가 필요 없다. (D13 을 잇는다)
set -e
cd "$(dirname "$0")/../../.."
if [ -d /Library/Developer/CommandLineTools/usr/bin ]; then
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi
mkdir -p .pio/host
FLAGS="-std=c++17 -Wall -Wextra -Werror -I src -I cores3/logic -I test/host"
c++ $FLAGS -pthread cores3/test/host/test_touch.cpp cores3/logic/*.cpp src/logic/*.cpp -o .pio/host/test_touch
.pio/host/test_touch
# 같은 시험을 기본이 아닌 마스코트로도 한 번(빌드 때 고르는 길이 살아 있는지, D26)
c++ $FLAGS -pthread -DMASCOT_ID=Cat cores3/test/host/test_touch.cpp cores3/logic/*.cpp src/logic/*.cpp -o .pio/host/test_touch_cat
# (시험 프로그램을 `| tail` 로 잇지 않는다: 그러면 시험이 실패해도 tail 의 성공이 결과가 되어 가려진다.)
.pio/host/test_touch_cat > .pio/host/test_touch_cat.log || { tail -20 .pio/host/test_touch_cat.log; exit 1; }
tail -1 .pio/host/test_touch_cat.log
# USB 설명자(컴퓨터에게 내놓는 자기소개 표)를 뽑아 형식을 검사한다. TinyUSB 헤더가 필요하므로
# cores3 를 한 번이라도 빌드해 부품(managed_components)을 내려받은 뒤에만 돈다.
TUSB=cores3/managed_components/espressif__tinyusb/src
if [ -d "$TUSB" ]; then
  cc -std=gnu11 -Wall -Wextra -Werror -DCFG_TUSB_MCU=OPT_MCU_NONE -DCFG_TUSB_OS=OPT_OS_NONE \
     -DTUP_DCD_ENDPOINT_MAX=8 -I "$TUSB" -I cores3/main \
     cores3/test/host/desc_dump.c cores3/main/usb_desc.c -o .pio/host/desc_dump
  .pio/host/desc_dump > .pio/host/desc_dump.txt
  python3 cores3/test/host/check_desc.py < .pio/host/desc_dump.txt
else
  echo "USB 설명자 검사: 건너뜀(cores3 를 아직 빌드하지 않아 TinyUSB 헤더가 없다)"
fi
# 파이썬 도구(cores3/tools) 가운데 기기 없이 볼 수 있는 것. 도구들이 pyserial 을 쓰므로 있을 때만 돈다.
if python3 -c "import serial" 2>/dev/null; then
  python3 cores3/test/host/test_tools.py
else
  echo "파이썬 도구 시험: 건너뜀(이 python3 에 pyserial 이 없다)"
fi

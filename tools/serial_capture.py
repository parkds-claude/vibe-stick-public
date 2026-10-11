#!/usr/bin/env python3
"""시리얼 로그를 정해진 시간 동안 받아 출력한다 (pio device monitor 는 대화형이라 자동 실행에 못 씀).

사용: pyserial 이 있는 파이썬으로 실행한다. PlatformIO 의 파이썬에는 들어 있다:
  ~/.platformio/penv/bin/python tools/serial_capture.py PORT 초 [--reset]      (공식 설치·VS Code 확장)
  (Homebrew 로 깐 PlatformIO 는 /opt/homebrew/opt/platformio/libexec/bin/python)
  또는 `python3 -m pip install pyserial` 뒤 python3 로.
  --reset : 시작할 때 기기를 재부팅시킨다(RTS 토글) → 부팅 로그부터 받음
            (조이스틱을 꽂은 채로는 쓰지 말 것: 다운로드 모드로 빠질 수 있다)
  --reset 없이 열면 기기는 재시작되지 않는다.
"""
import sys
import time

import serial

port = sys.argv[1]
seconds = float(sys.argv[2])
do_reset = "--reset" in sys.argv[3:]



def open_port(reset=False):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    # 포트를 열 때 기기가 재시작되지 않게 한다.
    # 맥은 여는 순간 두 신호(DTR·RTS)를 모두 켠다. 여기서 DTR 을 먼저 끄면 잠깐 "RTS 만 켜진"
    # 상태가 되어 기기가 리셋된다(실측: 열 때마다 재시작됐다). 그래서 RTS 를 먼저 끄고
    # DTR 을 나중에 끈다. 조이스틱을 꽂은 채 리셋되면 기기가 다운로드 모드로 빠질 수 있어 중요하다.
    # DTR 이 켜져 있는 동안에는 G0(조이스틱 통신선)가 낮게 눌리므로 곧바로 끈다
    # (0.05초 두었을 때는 조이스틱 읽기가 실패해 5초 동안 "조이스틱 없음"이 됐다).
    ser.dtr = True
    ser.rts = False
    ser.open()
    ser.dtr = False
    if reset:
        ser.rts = True  # DTR 꺼짐 + RTS 켜짐 = 리셋
        time.sleep(0.1)
        ser.rts = False
    return ser


# 기기를 만지다 USB 가 잠깐 끊겼다 붙으면 열어 둔 포트가 죽는다(예외 없이 조용히 멈추기도 한다).
# 주기적으로 포트가 살아 있는지 보고, 죽었으면 같은 방법으로 다시 연다.
ser = None
end = time.time() + seconds
first = True
while time.time() < end:
    try:
        if ser is None:
            ser = open_port(reset=do_reset and first)
            first = False
        ser.in_waiting  # 포트가 사라졌으면 여기서 예외가 난다
        data = ser.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
    except (serial.SerialException, OSError):
        try:
            if ser:
                ser.close()
        except Exception:
            pass
        ser = None
        time.sleep(1.0)
if ser:
    ser.close()

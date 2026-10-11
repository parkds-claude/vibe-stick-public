#!/usr/bin/env python3
"""기기의 블랙박스(main/diag.h)를 읽어 풀어 쓴다. 기기가 내려받기 모드일 때 쓴다.

    diag.py [--dev]        --dev 면 개발판 빌드(build-dev)의 주소로 읽는다

USB 가 뜨지 않아 로그를 볼 수 없을 때, 펌웨어가 어디까지 갔고 왜 내려받기 모드로 물러났는지를
여기서 본다. 블랙박스의 자리(주소)는 빌드마다 다를 수 있어 그 빌드의 ELF 파일에서 찾는다.
ESP-IDF 를 켠 셸에서 돌린다(source ~/esp/esp-idf-5.5.4/export.sh).
"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vibectl  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FIELDS = ["magic", "boots", "stage", "uptime_ms", "reset_reason", "download_reason", "board", "flags",
          "desc_device", "desc_config", "desc_string", "bus_resets", "setups", "mic_opens", "mic_level",
          "heap_min"]
STAGES = {1: "app_main 에 들어옴", 2: "화면·전원·터치(M5.begin) 끝", 3: "화면 준비", 4: "USB 통로 켬",
          5: "TinyUSB 켬", 6: "USB 태스크 만듦", 7: "마이크 켬", 8: "loop 가 돌았다"}
DOWNLOADS = {0: "물러난 적 없음", 1: "vibe:boot 명령", 2: "화면에서 확인", 3: "개발판 안전장치: 컴퓨터가 잡지 않음",
             4: "개발판 안전장치: 명령이 없음", 5: "USB 를 켜지 못함", 6: "연달아 죽음"}
RESETS = {1: "전원·RST", 3: "프로그램 재시작", 4: "프로그램 오류(panic)", 5: "인터럽트 멈춤 감시",
          6: "태스크 멈춤 감시", 7: "그 밖의 감시(굽기 도구가 내보냄 포함)", 9: "전압 부족", 11: "USB 리셋"}
FLAGS = [(0x01, "컴퓨터가 잡은 적 있음"), (0x02, "지금 잡혀 있음"), (0x04, "잠듦"),
         (0x08, "로그 포트가 열린 적 있음"), (0x10, "마이크 켜짐"), (0x20, "개발판")]


def main():
    build = "build-dev" if "--dev" in sys.argv else "build"
    elf = os.path.join(HERE, "..", build, "vibe_stick_cores3.elf")
    nm = subprocess.run(["xtensa-esp32s3-elf-nm", elf], capture_output=True, text=True).stdout
    addr = next((int(ln.split()[0], 16) for ln in nm.splitlines() if ln.endswith(" g_vibe_diag")), None)
    if addr is None:
        sys.exit(f"{elf} 에서 블랙박스의 자리를 찾지 못했다")
    port = vibectl.find("download")
    if not port:
        sys.exit(f"내려받기 모드인 기기가 없다(지금 상태: {vibectl.state()})")
    with tempfile.TemporaryDirectory(prefix="vibe-diag-") as tmp:  # 읽은 뒤에는 지운다(임시 폴더에 남기지 않는다)
        out = os.path.join(tmp, "diag.bin")
        r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "--before", "no_reset",
                            "--after", "no_reset", "dump_mem", hex(addr), str(4 * len(FIELDS)), out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            sys.exit("읽지 못했다: " + (r.stdout + r.stderr)[-300:])
        with open(out, "rb") as f:
            raw = f.read()
    v = dict(zip(FIELDS, struct.unpack(f"<{len(FIELDS)}I", raw)))
    print(f"블랙박스 @ {addr:#x} ({build})")
    if v["magic"] != 0x56444941:
        print(f"  표식이 없다(magic={v['magic']:#x}) — 전원을 껐다 켰거나, 이 빌드가 올라가 있지 않다")
        return
    print(f"  켜진 횟수        {v['boots']}")
    print(f"  어디까지 갔나     {v['stage']} — {STAGES.get(v['stage'], '?')}")
    print(f"  돈 시간          {v['uptime_ms']} ms")
    print(f"  켜진 까닭        {v['reset_reason']} — {RESETS.get(v['reset_reason'], '?')}")
    print(f"  물러난 까닭      {v['download_reason']} — {DOWNLOADS.get(v['download_reason'], '?')}")
    print(f"  기종 번호        {v['board']}")
    print(f"  상태            {v['flags']:#04x} — " + (", ".join(n for b, n in FLAGS if v['flags'] & b) or "없음"))
    print(f"  컴퓨터가 물어 온 것  장치 {v['desc_device']}회, 구성 {v['desc_config']}회, 문자열 {v['desc_string']}회")
    print(f"  USB 버스 초기화    {v['bus_resets']}회,  제어 요청 {v['setups']}회")
    print(f"  마이크           열린 횟수 {v['mic_opens']}, 마지막 소리 크기 {v['mic_level']}")
    print(f"  남은 메모리 최저   {v['heap_min']} 바이트")


if __name__ == "__main__":
    main()

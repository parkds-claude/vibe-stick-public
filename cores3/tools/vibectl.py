#!/usr/bin/env python3
"""vibe-stick CoreS3 판과 맥 사이의 심부름꾼: 기기 찾기, 명령 보내기, 로그 보기.

    vibectl.py state                 기기가 지금 어떤 상태인지 (app / download / uiflow / none / ambiguous)
    vibectl.py port [app|download]   그 상태의 시리얼 포트 이름
    vibectl.py wait app|download [초] 그 상태가 될 때까지 기다린다(기본 20초)
    vibectl.py cmd <명령> [초]        로그 포트로 "vibe:<명령>" 을 보내고 답을 찍는다(기본 1.5초)
    vibectl.py log [초]              로그를 본다(기본 10초)
    vibectl.py hist [초]             기기가 스스로 적어 둔 최근 기록을 읽는다 — 선이 뽑혀 있던 동안의 일까지(D67)
    vibectl.py wanted                어느 기기를 "우리 기기"로 적어 두었는지(MAC). 적어 둔 것이 없으면 실패로 끝난다
    vibectl.py boot                  내려받기 모드로 보내고, 새 포트가 뜰 때까지 기다린다
                                     (평소 판은 기기 화면의 `Update firmware?` 에 ✓ 를 눌러야 들어간다)
    vibectl.py keepalive [초]        개발판이 스스로 내려받기 모드로 가지 않게 계속 말을 건다
    vibectl.py confirm <포트>        그 포트의 보드가 "우리 기기"가 맞는지 확인한다(굽기 전에 flash.sh 가 부른다)
    vibectl.py show <파일.rgb565> [방향]  (개발판) 화면 전체 그림(점마다 2바이트·큰 쪽 먼저 = 153,600바이트)을
                                     기기 화면에 띄운다 — 디자인 시안을 실물에서 볼 때. 화면을 누르면 원래대로.
                                     방향: 1(기본, 가로 320x240) · 0 또는 2(세로 240x320)
    vibectl.py show off              띄운 그림을 거둔다
    vibectl.py time                  기기의 시계를 이 컴퓨터의 지금 시각으로 맞춘다(쉴 때 화면에 시각이 보인다)
    vibectl.py note done|ask         기기에 알린다: Claude 가 일을 끝냈다 / 답을 기다린다(화면 + 소리)

어느 기기인가: 환경변수 VIBE_MAC(예: A1B2C3D4E5F6) 또는 cores3/.vibe-mac 파일(한 줄, git 에 넣지 않음)에
기기의 MAC 12자리를 적어 둔다. 적어 두면 다른 ESP32 보드가 함께 꽂혀 있어도 그 기기만 본다.
굽기(flash.sh)와 되돌리기(backup.py --restore)는 이것이 없으면 하지 않는다.

pyserial 이 필요하다. ESP-IDF 를 켠 셸(source ~/esp/esp-idf-5.5.4/export.sh)의 python 에 들어 있다.

주의: UIFlow2 가 돌고 있는 기기(상태 uiflow)에는 esptool 을 쓰지 말 것. USB 에서 사라져 RST 를
눌러야 돌아온다(개발 기록(공개판에는 없음)).
"""
import os
import sys
import time

import serial
from serial.tools import list_ports

VID = 0x303A
PID_APP = 0x4015       # 우리 펌웨어(usb_desc.h)
PID_DOWNLOAD = 0x1001  # 칩의 내려받기 모드(USB JTAG/serial debug unit)
PID_UIFLOW = 0x811A    # UIFlow2 가 도는 M5Stack 기기


MAC_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".vibe-mac")


def _mac(serial_number):
    return "".join(ch for ch in (serial_number or "").upper() if ch in "0123456789ABCDEF")[:12]


def parse_mac(text, where):
    """사람이 적은 MAC(구분 기호 : - 는 있어도 된다)을 12자리로. 잘못 적었으면 멈춘다.

    조용히 넘어가면 안 된다: 예전에는 16진수가 아닌 값을 빈 글로 바꿔 "아무 기기나"로 받아들였다
    (오타 하나로 기기를 가려내는 일이 꺼졌다 — 독립 검토).
    """
    raw = text.strip().upper().replace(":", "").replace("-", "")
    if len(raw) != 12 or any(ch not in "0123456789ABCDEF" for ch in raw):
        sys.exit(f"{where} 의 값 {text.strip()!r} 은 MAC 12자리(16진수)가 아니다. 예: A1B2C3D4E5F6")
    return raw


def wanted_mac():
    """우리 기기의 MAC. 환경변수 VIBE_MAC 이 먼저, 없으면 cores3/.vibe-mac. 둘 다 없으면 빈 글."""
    env = os.environ.get("VIBE_MAC", "")
    if env.strip():
        return parse_mac(env, "VIBE_MAC")
    try:
        with open(MAC_FILE, encoding="utf-8") as f:
            for line in f:
                if line.strip() and not line.lstrip().startswith("#"):
                    return parse_mac(line, MAC_FILE)
    except FileNotFoundError:
        pass
    return ""


def ports(kind):
    """그 상태로 보이는 (포트, MAC) 들. 우리 기기의 MAC 을 적어 두었으면 그 기기만."""
    want = {"app": PID_APP, "download": PID_DOWNLOAD, "uiflow": PID_UIFLOW}[kind]
    only = wanted_mac()
    out = []
    for p in list_ports.comports():
        if p.vid == VID and p.pid == want and p.device.startswith("/dev/cu."):
            if only and _mac(p.serial_number) != only:
                continue
            out.append((p.device, _mac(p.serial_number)))
    return out


def candidates(kind):
    return [dev for dev, _ in ports(kind)]


def mac_of(port):
    """그 포트의 보드가 USB 로 알려 준 일련번호(= MAC). 모르면 빈 글."""
    for p in list_ports.comports():
        if p.device == port:
            return _mac(p.serial_number)
    return ""


def confirm(port, read_mac=None):
    """굽기 전의 마지막 확인: 이 포트의 보드가 정말 우리 기기인가. 아니면 멈춘다.

    내려받기 모드의 USB 번호(303A:1001)는 ESP32-S3 보드라면 모두 같다. 우리 기기가 빠져 있고 옆의
    다른 보드만 내려받기 모드라면 "하나뿐이니 이것"으로 골라 엉뚱한 보드에 굽게 된다(독립 검토).
    그래서 (1) 우리 기기의 MAC 이 적혀 있어야 하고, (2) USB 일련번호가 그것과 같아야 하며,
    (3) 칩에게 직접 물은 MAC(esptool read_mac)도 같아야 한다.
    read_mac: 시험에서 칩의 답을 바꿔 끼우는 자리(포트를 받아 MAC 글을 돌려주는 함수).
    """
    want = wanted_mac()
    seen = mac_of(port)
    if not want:
        sys.exit(f"어느 기기에 구울지 정해져 있지 않다. 이 포트({port})의 보드는 MAC {seen or '모름'} 이다.\n"
                 f"이 기기가 맞으면 한 번만 적어 둔다:  echo {seen or '<MAC 12자리>'} > {os.path.normpath(MAC_FILE)}")
    if seen != want:
        sys.exit(f"이 포트({port})의 보드는 MAC {seen or '모름'} 이다 — 우리 기기({want})가 아니다. 굽지 않는다.")
    chip = _mac((read_mac or _esptool_mac)(port))
    if chip != want:
        sys.exit(f"칩이 답한 MAC({chip or '답 없음'})이 우리 기기({want})와 다르다. 굽지 않는다.")
    return want


def _esptool_mac(port):
    import re
    import subprocess
    try:
        r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "--before", "no_reset",
                            "--after", "no_reset", "read_mac"], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return ""
    m = re.search(r"MAC:\s*([0-9A-Fa-f:]{17})", r.stdout or "")
    return m.group(1) if (r.returncode == 0 and m) else ""


def find(kind):
    """그 상태인 기기의 포트 이름. 없으면 None.

    내려받기 모드의 USB 번호(303A:1001)는 ESP32-S3·C3 보드라면 모두 같다. 그런 보드가 둘 이상
    꽂혀 있으면 어느 것이 우리 기기인지 알 수 없으므로 고르지 않고 멈춘다(엉뚱한 보드에 굽지 않게).
    """
    found = candidates(kind)
    if len(found) > 1:
        sys.exit(f"{kind} 상태의 기기가 {len(found)}대 보인다: {found}. 다른 보드를 뽑거나 "
                 "VIBE_MAC=<기기의 MAC 12자리> 를 주어 하나를 고른다.")
    return found[0] if found else None


def state():
    """기기의 상태. 우리 기기로 볼 만한 것이 여러 상태에 걸쳐 보이면 'ambiguous'."""
    seen = [kind for kind in ("app", "download", "uiflow") if candidates(kind)]
    if len(seen) > 1 or any(len(candidates(k)) > 1 for k in seen):
        return "ambiguous"
    return seen[0] if seen else "none"


def wait(kind, timeout):
    end = time.time() + timeout
    while time.time() < end:
        port = find(kind)
        if port:
            return port
        time.sleep(0.2)
    return None


def open_app():
    port = find("app")
    if not port:
        sys.exit(f"기기를 찾지 못했다(지금 상태: {state()})")
    # 포트를 열면 DTR 이 켜지고, 기기는 그것을 보고 로그를 보내기 시작한다.
    return serial.Serial(port, 115200, timeout=0.1)


READ_MISSES_MAX = 20  # 읽기가 잇달아 이만큼 실패하면(약 2초) 기기가 빠진 것으로 본다


def hist_problem(said):
    """`vibe:hist` 의 답이 온전한지 본다. 온전하면 빈 글, 아니면 무엇이 모자란지."""
    import re
    begin = re.search(r"vibe:hist begin (\d+)", said)
    end = re.search(r"vibe:hist end (\d+)", said)
    if not begin:
        return "머리말(vibe:hist begin)이 없다 — 기기가 이 명령을 모르거나(옛 펌웨어) 답이 오지 않았다"
    if not end:
        return "꼬리말(vibe:hist end)이 없다 — 받다가 끊겼다"
    if begin.group(1) != end.group(1):
        return f"보내려던 {begin.group(1)} 바이트 가운데 {end.group(1)} 바이트만 왔다"
    return ""


def printable(text):
    """기기가 보낸 글을 터미널에 찍어도 되는 글로: 줄바꿈·탭 말고는 제어 문자를 \ufffd 로 바꾼다(CR 은 뺀다).

    기기의 글은 믿을 수 없는 입력이다 — 같은 USB 번호를 흉내 낸 장치가 터미널을 조종하는 글(ESC …)을 보낼 수
    있고, 이 출력은 Claude 의 도구 결과로도 들어간다(보안 점검). 글 방향을 뒤집는 유니코드 문자도 거른다.
    """
    out = []
    for ch in text:
        o = ord(ch)
        if ch in "\n\t":
            out.append(ch)
        elif ch == "\r":
            continue
        elif (o < 0x20 or 0x7F <= o < 0xA0 or 0x200B <= o <= 0x200F or 0x2028 <= o <= 0x202E
              or 0x2066 <= o <= 0x2069 or o == 0xFEFF):  # 제어 문자 · 보이지 않는 문자 · 줄을 가르거나 글 방향을 뒤집는 문자
            out.append("\ufffd")
        else:
            out.append(ch)
    return "".join(out)


def pump(ser, seconds, out=sys.stdout):
    """seconds 동안 들어오는 줄을 찍고, 받은 글을 통째로 돌려준다.

    다른 도구가 같은 포트를 잠깐 함께 읽으면(로그를 보는 중에 명령을 보낼 때) 읽기가 한두 번 실패한다
    ("읽을 것이 있다더니 없다" — 그쪽이 먼저 가져갔다). 그것으로 멈추지 않는다: 잇달아 실패할 때만 멈춘다.
    (계속 말 걸기와 로그 보기가 겹쳐 75초짜리 기록이 12초에서 끊겼다.)
    """
    end = time.time() + seconds
    got = b""
    misses = 0
    while time.time() < end:
        try:
            chunk = ser.read(256)
        except serial.SerialException:
            misses += 1
            if misses >= READ_MISSES_MAX:
                raise
            time.sleep(0.1)
            continue
        misses = 0
        if chunk:
            got += chunk
            out.write(printable(chunk.decode("utf-8", "replace")))  # 찍는 것만 거른다(돌려주는 글은 받은 그대로)
            out.flush()
    return got.decode("utf-8", "replace")


IMG_BYTES = 320 * 240 * 2


def show(path, rotation="1"):
    """(개발판) 화면 전체 그림을 기기에 보낸다. 기기가 "받을 준비"를 알린 뒤에 바이트를 보낸다.

    rotation: 화면 방향 0~3(평소 화면은 1 = 가로). 세로 그림(240x320)은 0 이나 2.
    """
    with open_app() as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        if path == "off":
            ser.write(b"vibe:img off\n")
            pump(ser, 0.5)
            return
        data = open(path, "rb").read()
        if len(data) != IMG_BYTES:
            sys.exit(f"{path}: {len(data)} 바이트 — 320x240 RGB565({IMG_BYTES} 바이트)가 아니다")
        ser.write(f"vibe:img {rotation}\n".encode())
        said = pump(ser, 0.5, out=sys.stderr)
        if "vibe:img ready" not in said:
            sys.exit("기기가 그림을 받을 준비를 알리지 않았다(개발판이 아니거나 옛 펌웨어).")
        t0 = time.time()
        for off in range(0, len(data), 4096):
            ser.write(data[off:off + 4096])
        ser.flush()
        said = pump(ser, 3.0, out=sys.stderr)
        print(f"보냄 {len(data)} 바이트, {time.time() - t0:.1f}초", "— 기기: 다 받음" if "vibe:img done" in said else "— 기기의 끝 알림 없음")


def main(argv):
    """명령 하나를 돌린다. 보는 중에 기기가 USB 에서 빠지면(선이 뽑힘·재시작) 한 줄로 알리고 끝낸다."""
    import errno
    try:
        return run(argv)
    except serial.SerialException as e:
        # 로그를 보던 중에 선이 뽑히자 파이썬의 오류 더미가 그대로 나왔다("Device not configured").
        sys.exit(f"기기가 USB 에서 빠졌다(선이 뽑혔거나 기기가 재시작했다): {e}")
    except OSError as e:
        # 포트가 사라졌을 때의 오류만 그렇게 알린다. 그 밖의 것(없는 파일 등)은 원래대로 드러낸다.
        if e.errno in (errno.ENXIO, errno.EIO, errno.ENODEV, errno.EBADF):
            sys.exit(f"기기가 USB 에서 빠졌다(선이 뽑혔거나 기기가 재시작했다): {e}")
        raise


def run(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    what = argv[1]
    if what == "state":
        print(state())
    elif what == "port":
        port = find(argv[2] if len(argv) > 2 else "app")
        if not port:
            sys.exit(1)
        print(port)
    elif what == "wait":
        port = wait(argv[2], float(argv[3]) if len(argv) > 3 else 20)
        if not port:
            sys.exit(f"{argv[2]} 상태가 되지 않았다(지금: {state()})")
        print(port)
    elif what == "cmd":
        with open_app() as ser:
            time.sleep(0.3)  # 기기가 포트 열림을 알아챌 시간
            ser.write(f"vibe:{argv[2]}\n".encode())
            pump(ser, float(argv[3]) if len(argv) > 3 else 1.5)
    elif what == "hist":
        # 기기 속 기록(D67): 선이 뽑혀 있던 동안의 일까지, 기기가 적어 둔 최근 기록을 읽는다.
        with open_app() as ser:
            time.sleep(0.3)
            ser.write(b"vibe:hist\n")
            said = pump(ser, float(argv[2]) if len(argv) > 2 else 3)
        # 기기는 "vibe:hist begin <바이트>" … "vibe:hist end <바이트>" 로 싸서 준다. 둘이 다 있고 같아야 온전하다.
        problem = hist_problem(said)
        if problem:
            sys.exit(f"기록이 온전하지 않다: {problem} — 다시 읽을 것")
    elif what == "wanted":
        mac = wanted_mac()
        if not mac:
            sys.exit("어느 기기인지 적어 둔 것이 없다. cores3/.vibe-mac 에 기기의 MAC 12자리를 한 줄로 적거나 "
                     "VIBE_MAC=<MAC> 을 준다(예: A1B2C3D4E5F6).")
        print(mac)
    elif what == "log":
        with open_app() as ser:
            pump(ser, float(argv[2]) if len(argv) > 2 else 10)
    elif what == "boot":
        if find("download"):
            print(find("download"))
            return
        said = ""
        app_mac = mac_of(find("app") or "")  # 명령을 받는 바로 그 기기
        with open_app() as ser:
            time.sleep(0.3)
            ser.write(b"vibe:boot\n")
            try:
                said = pump(ser, 1.0, out=sys.stderr)
            except (serial.SerialException, OSError):
                pass  # 기기가 재시작하며 포트가 사라진다
        if "ignored" in said:
            # 평소 판이 방금 거절했거나(시간 초과 포함) 이미 묻는 중이다: 10초 동안은 다시 묻지 않는다(D47).
            sys.exit("기기가 지금은 묻지 않는다(방금 물음이 끝났거나 아직 떠 있다). 10초쯤 뒤에 다시 실행한다 — "
                     "RST 버튼은 누를 필요 없다.")
        if "waiting" in said:
            # 평소 판: 화면에서 사람이 확인해야 한다(D47)
            # "hold OK" 라고 답하는 판(D75, ~)은 ✓ 를 꾹 눌러야 받는다. 그 앞의 판은 톡으로 받는다.
            how = ("오른쪽 아래 ✓ 를 2초 동안 꾹 눌렀다 떼 주세요 — 톡 치면 받지 않습니다" if "hold OK" in said
                   else "오른쪽 아래 ✓ 를 눌러 주세요")
            print(f">>> 기기 화면에 `Update firmware?` 가 떴습니다. 화면에서 손을 뗀 채 2초쯤 기다렸다가 {how}(20초 안). <<<",
                  file=sys.stderr)
        port = wait("download", 25 if "waiting" in said else 15)
        if not port:
            if "waiting" in said:
                sys.exit("✓ 가 눌리지 않아 기기는 그대로 돌아갔다" + ("(꾹 2초 — 톡으로는 받지 않는다)" if "hold OK" in said else "")
                         + ". 10초쯤 뒤에 다시 실행한다 — RST 버튼은 누를 필요 없다.")
            sys.exit("내려받기 모드로 뜨지 않았다. 기기의 RST 버튼을 3초 누른다(초록 불).")
        if app_mac and mac_of(port) != app_mac:
            sys.exit(f"내려받기 모드로 뜬 보드({mac_of(port)})가 명령을 보낸 기기({app_mac})와 다르다.")
        print(port)
    elif what == "confirm":
        print(confirm(argv[2]))
    elif what == "time":
        with open_app() as ser:
            time.sleep(0.3)
            ser.write(time.strftime("vibe:time %Y%m%d%H%M%S\n").encode())
            pump(ser, 0.6)
    elif what == "note":
        with open_app() as ser:
            time.sleep(0.3)
            ser.write(f"vibe:note {argv[2]}\n".encode())
            pump(ser, 0.6)
    elif what == "show":
        show(argv[2], argv[3] if len(argv) > 3 else "1")
    elif what == "keepalive":
        seconds = float(argv[2]) if len(argv) > 2 else 3600
        end = time.time() + seconds
        while time.time() < end:
            try:
                with open_app() as ser:
                    time.sleep(0.3)
                    ser.write(b"vibe:ping\n")
                    ser.flush()
                    time.sleep(0.3)  # 읽지 않는다: 여기서 읽으면 함께 로그를 보는 도구의 글을 가로챈다
            except (serial.SerialException, OSError, SystemExit):
                pass
            time.sleep(20)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

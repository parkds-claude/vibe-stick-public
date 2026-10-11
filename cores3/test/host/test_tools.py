#!/usr/bin/env python3
"""cores3/tools 의 파이썬 도구 가운데 기기 없이 볼 수 있는 것을 시험한다. 실행: cores3/test/host/run.sh

기기가 꽂힌 채 돌려도 기기를 건드리지 않는다: 포트를 찾는 자리와 굽기 도구를 부르는 자리는 가짜로
바꿔 놓고 보고, 도구를 들여오는(import) 것만으로 무엇을 건드리지 않는지는 딴 프로세스에서 먼저 확인한다.
"""
import ast
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")

passed = failed = 0


def check(cond, what):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print(f"  실패 {what}")


# 도구 하나를 들여와 보는 작은 프로그램. 포트 열기·포트 목록 훑기·다른 프로그램 띄우기·스레드 시작의
# 자리에 "불리면 적고 멈추는" 가짜를 끼운 뒤에 들여온다 — 그래서 진짜는 한 번도 불리지 않는다.
IMPORT_PROBE = r'''
import subprocess
import sys
import threading

import serial
from serial.tools import list_ports

touched = []


def spy(name):
    def f(*a, **k):
        touched.append(name)
        raise RuntimeError(name)
    return f


serial.Serial = spy("serial.Serial")
list_ports.comports = spy("list_ports.comports")
subprocess.run = spy("subprocess.run")
subprocess.Popen = spy("subprocess.Popen")
threading.Thread.start = spy("Thread.start")
sys.path.insert(0, sys.argv[1])
try:
    __import__(sys.argv[2])
except BaseException as e:  # noqa: BLE001
    touched.append(f"들여오다 멈춤: {e!r}")
print("TOUCHED", touched)
'''


def import_side_effects(module):
    """도구를 들여오는 것만으로 건드리는 것들의 목록(없으면 빈 목록). 딴 프로세스에서 가짜를 끼우고 본다."""
    r = subprocess.run([sys.executable, "-c", IMPORT_PROBE, TOOLS, module], capture_output=True, text=True,
                       timeout=60)
    for ln in r.stdout.splitlines():
        if ln.startswith("TOUCHED "):
            return ast.literal_eval(ln[len("TOUCHED "):])
    return [f"확인 프로그램이 답하지 않았다: {(r.stdout + r.stderr).strip()[-200:]}"]


# 도구를 이 프로세스로 들여오기 전에 본다. 깨끗하지 않으면 들여오지 않고 멈춘다
# (모듈 맨 바깥에 포트를 여는 코드가 생기면, 시험을 돌리는 것만으로 꽂혀 있는 기기를 건드리게 된다).
print("- import_touches_nothing")
for _module in ("backup", "check_mac"):
    _hit = import_side_effects(_module)
    check(_hit == [], f"{_module} 을 들여오는 것만으로는 포트·다른 프로그램을 건드리지 않는다(지금: {_hit})")
if failed:
    print(f"통과 {passed}, 실패 {failed} — 도구를 들여오지 않고 멈춘다")
    sys.exit(1)

sys.path.insert(0, TOOLS)
import backup  # noqa: E402
import vibectl  # noqa: E402  (들여와도 포트를 열지 않는다 — backup 이 이미 들여온다)
import check_mac  # noqa: E402

# 시험은 진짜 "우리 기기인가" 확인(vibectl.confirm — 포트와 칩에게 묻는다)을 부르지 않는다: 가짜로 바꿔 끼운다.
REAL_CONFIRM = vibectl.confirm


def test_esptool_timeout_returns_text():
    """굽기 도구가 멈춰 시간 초과가 나도, 그때까지의 출력을 글자로 돌려준다.

    파이썬은 시간 초과 때 text=True 여도 출력을 바이트로 준다. 그것을 그대로 돌려주면 부르는 쪽의
    r.stdout + r.stderr 가 TypeError 로 죽어, "끊기면 그 조각만 다시 읽는다"가 한 번도 돌지 못한다.
    """
    print("- test_esptool_timeout_returns_text")
    with tempfile.TemporaryDirectory() as tmp:
        # 가짜 굽기 도구: 한 줄 찍고 멈춘다(python -m esptool 이 이것을 집는다)
        with open(os.path.join(tmp, "esptool.py"), "w") as f:
            f.write("import sys, time\nprint('partial', flush=True)\ntime.sleep(30)\n")
        old = os.environ.get("PYTHONPATH")
        os.environ["PYTHONPATH"] = tmp + (os.pathsep + old if old else "")
        try:
            r = backup.esptool("/dev/null", "read_mac", timeout=1.5)
        finally:
            if old is None:
                del os.environ["PYTHONPATH"]
            else:
                os.environ["PYTHONPATH"] = old
    check(r.returncode != 0, "시간 초과는 실패로 돌려준다")
    check(isinstance(r.stdout, str), f"stdout 이 글자다(지금: {type(r.stdout).__name__})")
    check(isinstance(r.stderr, str), "stderr 이 글자다")
    try:
        joined = (r.stdout + r.stderr).strip()  # read_all·resume 이 하는 그대로
    except TypeError as e:
        joined = None
        check(False, f"r.stdout + r.stderr 가 죽는다: {e}")
    if joined is not None:
        check("partial" in joined, "그때까지 나온 출력이 남아 있다")
        check("시간 초과" in joined, "시간 초과라고 알린다")


def run_without_device(action, path, stop_at_port=False):
    """backup 의 action(path) 을 기기 없이 돌린다.

    포트 찾기와 굽기 도구를 가짜로 바꾼다(가짜 기기는 무엇을 물어도 "같다"고 답한다).
    stop_at_port 면 포트를 찾으려는 순간 멈춘다 — 가짜로 바꿀 수 없는 다음 단계까지 가지 않게.
    돌려주는 값: (불린 것들의 차례, 끝난 값 — 0 이 성공, 글자면 그 말을 남기고 실패).
    """
    calls = []

    def fake_port():
        calls.append("port")
        if stop_at_port:
            sys.exit("가짜 포트에서 멈춤")
        return "/dev/null"

    def fake_esptool(port, *args, **kw):
        calls.append("esptool")
        return subprocess.CompletedProcess(args, 0, "verify OK (digest matched)", "")

    saved = backup.need_download_port, backup.esptool, vibectl.confirm
    backup.need_download_port, backup.esptool = fake_port, fake_esptool
    if vibectl.confirm is REAL_CONFIRM:  # 시험이 따로 끼운 것이 없으면 "우리 기기가 맞다"로 답한다
        vibectl.confirm = lambda port: "ok"
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            try:
                action(path)
                code = "끝내지 않고 돌아왔다"
            except SystemExit as e:
                code = e.code
    finally:
        backup.need_download_port, backup.esptool, vibectl.confirm = saved
    return calls, code


def make_file(path, size):
    """크기만 size 인 파일(내용은 0 — 디스크를 실제로 채우지 않는다)."""
    with open(path, "wb") as f:
        f.truncate(size)
    return path


def test_verify_rejects_partial_backup_before_device():
    """덜 받은 백업은 기기에 묻기 전에 실패로 끝낸다.

    기기의 대조(verify_flash)는 "파일의 길이만큼"만 견준다. 그래서 받다 만 파일도 앞부분만 같으면
    "같다"로 나온다 — 통째 백업이 있다고 믿고 새 펌웨어를 올리게 된다(코덱스 검토).
    """
    print("- test_verify_rejects_partial_backup_before_device")
    full = backup.FLASH_SIZE
    with tempfile.TemporaryDirectory() as tmp:
        for name, size in (("받다 만 것(1MB)", 0x100000), ("빈 파일", 0), ("1바이트 모자람", full - 1),
                           ("1바이트 남음", full + 1)):
            calls, code = run_without_device(backup.verify, make_file(os.path.join(tmp, "part.bin"), size))
            check(code not in (0, None), f"{name}: 실패로 끝난다(지금 끝난 값: {code!r})")
            check("port" not in calls, f"{name}: 포트를 찾기 전에 끝난다(need_download_port 가 불리지 않는다)")
            check("esptool" not in calls, f"{name}: 기기에 묻지 않는다")
            check(isinstance(code, str) and str(size) in code and str(full) in code,
                  f"{name}: 지금 크기와 있어야 할 크기를 알린다(지금: {code!r})")
        # 없는 파일도 기기에 묻기 전에 끝난다.
        calls, code = run_without_device(backup.verify, os.path.join(tmp, "no-such.bin"))
        check(code not in (0, None) and calls == [], f"없는 파일: 기기에 묻기 전에 실패로 끝난다({calls}, {code!r})")
        # 크기가 꼭 맞으면 예전처럼 기기에게 묻고, 그 답이 결과다.
        calls, code = run_without_device(backup.verify, make_file(os.path.join(tmp, "full.bin"), full))
        check(calls == ["port", "esptool"], f"16MB 파일: 포트를 찾고 기기에 묻는다(지금: {calls})")
        check(code == 0, f"16MB 파일: 기기가 같다고 하면 성공(지금: {code!r})")


def test_restore_rejects_partial_backup_before_device():
    """되돌려 쓰기도 통째 백업(16MB)일 때만 한다 — verify 와 같은 검사를 쓴다."""
    print("- test_restore_rejects_partial_backup_before_device")
    with tempfile.TemporaryDirectory() as tmp:
        for size in (0x100000, backup.FLASH_SIZE - 1):
            calls, code = run_without_device(backup.restore, make_file(os.path.join(tmp, "part.bin"), size),
                                             stop_at_port=True)
            check(code not in (0, None) and calls == [], f"{size} 바이트: 기기를 찾기 전에 실패로 끝난다({calls}, {code!r})")


def test_check_mac_run_timeout_is_a_failed_result():
    """check_mac 이 부른 프로그램이 멈춰도 예외로 죽지 않고 '실패한 결과'를 받는다(backup.esptool 과 같은 방식).

    예외로 흘리면 확인 도중에 도구가 통째로 죽어, 그때까지의 통과·실패 집계가 찍히지 않는다.
    """
    print("- test_check_mac_run_timeout_is_a_failed_result")
    cmd = [sys.executable, "-c", "import time\nprint('partial', flush=True)\ntime.sleep(30)\n"]
    try:
        r = check_mac.run(cmd, timeout=1.5)
    except subprocess.TimeoutExpired as e:
        r = None
        check(False, f"시간 초과가 예외로 새어 나온다: {e}")
    if r is not None:
        check(r.returncode != 0, "시간 초과는 실패로 돌려준다")
        check(isinstance(r.stdout, str) and isinstance(r.stderr, str),
              f"출력이 글자다(지금: {type(r.stdout).__name__}, {type(r.stderr).__name__})")
        check("partial" in (r.stdout or ""), "그때까지 나온 출력이 남아 있다")
        check("시간 초과" in (r.stderr or ""), "시간 초과라고 알린다")
    # 제때 끝난 프로그램의 결과는 그대로다.
    ok = check_mac.run([sys.executable, "-c", "import sys\nprint('out')\nprint('err', file=sys.stderr)\nsys.exit(3)\n"])
    check((ok.returncode, ok.stdout, ok.stderr) == (3, "out\n", "err\n"),
          f"제때 끝나면 끝난 값과 출력을 그대로 준다(지금: {(ok.returncode, ok.stdout, ok.stderr)!r})")


def test_check_list_survives_a_stuck_command():
    """맥에게 장치 목록을 묻는 명령이 멈춰도(시간 초과 → 끊긴 출력) C1 은 죽지 않고 '실패'로 적힌다."""
    print("- test_check_list_survives_a_stuck_command")

    def stuck(cmd, timeout=60):
        # 시간 초과로 끊긴 결과: 출력은 쓰다 만 것(JSON 이 닫히지 않았다)
        return subprocess.CompletedProcess(cmd, 1, '{"SPAudioDataType": [{"_items": [', "시간 초과")

    saved_run, saved_results = check_mac.run, list(check_mac.results)
    check_mac.run = stuck
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            found = check_mac.check_list()
        recorded = dict(check_mac.results[len(saved_results):])
    except Exception as e:  # noqa: BLE001
        found, recorded = None, {}
        check(False, f"확인이 죽는다: {e!r}")
    finally:
        check_mac.run = saved_run
        check_mac.results[:] = saved_results
    if found is not None:
        check(found is False, "마이크를 찾지 못한 것으로 돌려준다")
    check(recorded == {"C1-usb": False, "C1-mic": False, "C1-key": False},
          f"세 항목이 모두 실패로 적힌다(지금: {recorded})")


# ---------- Claude 의 알림(claude_note.py) ----------

def test_note_only_for_attended_sessions():
    print("- test_note_only_for_attended_sessions")
    import claude_note
    yes = claude_note.should_notify
    vscode = {"CLAUDE_CODE_ENTRYPOINT": "claude-vscode", "CLAUDE_CODE_SESSION_ATTENDED": "1"}
    bot = {"CLAUDE_CODE_ENTRYPOINT": "sdk-cli", "CLAUDE_CODE_SESSION_ATTENDED": "0"}
    check(yes("done", vscode, {}) and yes("ask", vscode, {}), "사람이 쓰는 창은 알린다")
    check(yes("done", {}, {}), "손으로 부르면(표시가 없으면) 알린다")
    check(not yes("done", bot, {}) and not yes("ask", bot, {}), "봇이 돌리는 claude -p 는 알리지 않는다")
    check(not yes("done", {"CLAUDE_CODE_SESSION_ATTENDED": "0"}, {}), "'보고 있지 않음' 표시 하나로도 알리지 않는다")
    check(not yes("done", {"CLAUDE_CODE_ENTRYPOINT": "sdk-py"}, {}), "SDK 로 돌리는 것도 알리지 않는다")
    check(yes("ask", vscode, {"notification_type": "permission_prompt", "message": "Claude needs your permission to use Bash"}),
          "허락을 기다리면 알린다")
    check(not yes("ask", vscode, {"notification_type": "idle_prompt"}), "'입력을 기다리는 중'은 알리지 않는다(끝났다는 알림이 이미 갔다)")
    check(not yes("ask", vscode, {"message": "Claude is waiting for your input"}), "종류가 없어도 글로 가려낸다")
    check(not yes("ask", vscode, {"notification_type": "auth_success"}), "로그인 성공은 알리지 않는다")
    check(not yes("boot", vscode, {}) and not yes("", vscode, {}), "done·ask 가 아니면 알리지 않는다")
    # 기기가 없거나 포트 목록이 죽어도 조용히 False(예외를 밖으로 내지 않는다)
    saved = vibectl.list_ports.comports
    try:
        vibectl.list_ports.comports = lambda: []
        check(claude_note.send("done") is False, "기기가 없으면 조용히 끝난다")
        vibectl.list_ports.comports = lambda: (_ for _ in ()).throw(RuntimeError("x"))
        check(claude_note.send("ask") is False, "포트 목록이 죽어도 조용히 끝난다")
    finally:
        vibectl.list_ports.comports = saved


# ---------- 포트 읽기 ----------

class FlakySerial:
    """읽을 때마다 정해 둔 것을 차례로 돌려준다: 바이트면 그대로, 예외면 던진다. 다 쓰면 빈 것."""
    def __init__(self, script):
        self.script = list(script)

    def read(self, n):
        if not self.script:
            time.sleep(0.01)
            return b""
        item = self.script.pop(0)
        if isinstance(item, Exception):
            raise item
        return item


def test_pump_survives_another_reader():
    print("- test_pump_survives_another_reader")
    oops = vibectl.serial.SerialException("device reports readiness to read but returned no data")
    sink = io.StringIO()
    got = vibectl.pump(FlakySerial([b"[sens] a\n", oops, oops, b"[sens] b\n"]), 0.6, out=sink)
    check(got == "[sens] a\n[sens] b\n", f"한두 번의 읽기 실패로 멈추지 않고 앞뒤 글을 다 받는다(지금: {got!r})")
    # 기기가 정말 빠졌으면(읽기가 잇달아 실패) 끝없이 기다리지 않고 알린다.
    saved = vibectl.READ_MISSES_MAX
    vibectl.READ_MISSES_MAX = 3
    try:
        raised = False
        try:
            vibectl.pump(FlakySerial([oops] * 10), 5, out=sink)
        except vibectl.serial.SerialException:
            raised = True
        check(raised, "잇달아 실패하면 멈춘다(기기가 빠졌다)")
        got = vibectl.pump(FlakySerial([oops, oops, b"x", oops, oops, b"y"]), 0.8, out=sink)
        check(got == "xy", f"사이에 한 번이라도 읽히면 다시 센다(지금: {got!r})")
    finally:
        vibectl.READ_MISSES_MAX = saved


# ---------- 어느 기기인가(MAC) ----------

class FakePort:
    def __init__(self, device, pid, serial_number):
        self.device, self.vid, self.pid, self.serial_number = device, 0x303A, pid, serial_number


@contextlib.contextmanager
def fake_world(ports, env_mac=None, file_mac=None):
    """vibectl 이 보는 세상을 바꾼다: 꽂힌 보드들, VIBE_MAC, .vibe-mac 파일. 진짜 포트는 훑지 않는다."""
    with tempfile.TemporaryDirectory() as tmp:
        saved = vibectl.list_ports.comports, vibectl.MAC_FILE, os.environ.get("VIBE_MAC")
        vibectl.list_ports.comports = lambda: list(ports)
        vibectl.MAC_FILE = os.path.join(tmp, ".vibe-mac")
        if file_mac is not None:
            with open(vibectl.MAC_FILE, "w") as f:
                f.write(file_mac)
        os.environ.pop("VIBE_MAC", None)
        if env_mac is not None:
            os.environ["VIBE_MAC"] = env_mac
        try:
            yield
        finally:
            vibectl.list_ports.comports, vibectl.MAC_FILE = saved[0], saved[1]
            os.environ.pop("VIBE_MAC", None)
            if saved[2] is not None:
                os.environ["VIBE_MAC"] = saved[2]


def exits(fn, *args, **kw):
    """fn 이 sys.exit 로 끝나면 그 말(또는 값), 끝나지 않으면 None."""
    try:
        fn(*args, **kw)
    except SystemExit as e:
        return str(e.code)
    return None


OURS = FakePort("/dev/cu.ours", 0x1001, "A1:B2:C3:D4:E5:F6")     # 내려받기 모드인 우리 기기
OTHER = FakePort("/dev/cu.other", 0x1001, "A1:B2:C3:00:11:22")   # 옆에 꽂힌 다른 ESP32-S3 보드


def test_mac_is_parsed_strictly():
    print("- test_mac_is_parsed_strictly")
    check(vibectl.parse_mac("A1B2C3D4E5F6", "t") == "A1B2C3D4E5F6", "12자리 그대로")
    check(vibectl.parse_mac(" a1:b2:c3:d4:e5:f6\n", "t") == "A1B2C3D4E5F6", "구분 기호와 소문자를 받아 준다")
    # 오타를 조용히 "아무 기기나"로 바꾸지 않는다(예전: 16진수가 아닌 글자를 빼 버려 빈 글이 되면 가려내기가 꺼졌다)
    for bad in ("AlB2C3D4E5F6", "A1B2C3D4E5F", "A1B2C3D4E5F6A", "xyz", "::"):
        check(exits(vibectl.parse_mac, bad, "t") is not None, f"잘못 적은 값은 멈춘다: {bad!r}")
    with fake_world([OURS, OTHER], env_mac="AlB2C3D4E5F6"):
        check(exits(vibectl.state) is not None, "VIBE_MAC 을 잘못 적으면 상태 보기부터 멈춘다")


def test_wanted_mac_env_then_file():
    print("- test_wanted_mac_env_then_file")
    with fake_world([], env_mac=None, file_mac=None):
        check(vibectl.wanted_mac() == "", "아무것도 없으면 빈 글")
    with fake_world([], file_mac="# 우리 기기\nA1B2C3D4E5F6\n"):
        check(vibectl.wanted_mac() == "A1B2C3D4E5F6", "파일에서 읽는다(주석 줄은 건너뛴다)")
    with fake_world([], env_mac="A1B2C3001122", file_mac="A1B2C3D4E5F6\n"):
        check(vibectl.wanted_mac() == "A1B2C3001122", "환경변수가 파일보다 먼저다")
    with fake_world([OURS, OTHER], file_mac="A1B2C3D4E5F6\n"):
        check(vibectl.candidates("download") == ["/dev/cu.ours"], "적어 둔 기기만 본다")
        check(vibectl.state() == "download", "다른 보드가 함께 있어도 헷갈리지 않는다")
    with fake_world([OURS, OTHER]):
        check(vibectl.state() == "ambiguous", "적어 두지 않았고 둘이면 고르지 않는다")


def test_confirm_refuses_the_wrong_board():
    """굽기 전의 마지막 확인. 우리 기기가 빠져 있고 옆 보드만 내려받기 모드일 때가 위험한 경우다."""
    print("- test_confirm_refuses_the_wrong_board")
    chip_ours = lambda port: "a1:b2:c3:d4:e5:f6"
    chip_other = lambda port: "a1:b2:c3:00:11:22"
    asked = []
    never = lambda port: asked.append(port) or ""
    with fake_world([OTHER]):  # MAC 을 적어 두지 않음: "하나뿐이니 이것"으로 굽지 않는다
        why = exits(vibectl.confirm, "/dev/cu.other", read_mac=never)
        check(why is not None and "A1B2C3001122" in why, f"적어 두지 않았으면 멈추고 보이는 MAC 을 알려 준다({why!r})")
        check(asked == [], "칩에 묻기 전에 멈춘다")
    with fake_world([OTHER], file_mac="A1B2C3D4E5F6\n"):
        check(exits(vibectl.confirm, "/dev/cu.other", read_mac=never) is not None, "다른 보드의 포트면 멈춘다")
        check(asked == [], "다른 보드에는 묻지도 않는다")
    with fake_world([OURS], file_mac="A1B2C3D4E5F6\n"):
        check(exits(vibectl.confirm, "/dev/cu.ours", read_mac=chip_other) is not None, "칩이 다른 MAC 을 답하면 멈춘다")
        check(exits(vibectl.confirm, "/dev/cu.ours", read_mac=lambda port: "") is not None, "칩이 답하지 않으면 멈춘다")
        check(vibectl.confirm("/dev/cu.ours", read_mac=chip_ours) == "A1B2C3D4E5F6", "USB 일련번호와 칩의 답이 모두 맞으면 통과")


def test_restore_confirms_the_board_before_writing():
    print("- test_restore_confirms_the_board_before_writing")
    seen = []

    def fake_confirm(port):
        seen.append(port)
        sys.exit("가짜 확인에서 멈춤")  # 진짜 쓰기(esptool)까지 가지 않는다

    saved = vibectl.confirm
    vibectl.confirm = fake_confirm
    try:
        with tempfile.TemporaryDirectory() as tmp:
            calls, code = run_without_device(backup.restore, make_file(os.path.join(tmp, "full.bin"), backup.FLASH_SIZE))
    finally:
        vibectl.confirm = saved
    check(seen == ["/dev/null"], f"쓰기 전에 그 포트의 보드를 확인한다(지금: {seen})")
    check(code == "가짜 확인에서 멈춤" and "esptool" not in calls, "확인이 멈추면 쓰지 않는다")


def test_backup_never_overwrites_an_existing_file():
    print("- test_backup_never_overwrites_an_existing_file")
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "good.bin")
        with open(path, "wb") as f:
            f.write(b"good backup")
        calls, code = run_without_device(backup.read_all, path, stop_at_port=True)
        check(calls == [] and code != 0, f"기기를 찾기 전에 거절한다(지금: {calls}, {code!r})")
        check(open(path, "rb").read() == b"good backup", "있던 파일은 그대로다")
        check(backup.part_path(path) == path + ".part", "받는 동안은 .part 에 쓴다")


def test_resume_never_touches_a_complete_backup():
    """--resume 은 받다 만 것만 잇는다. 이미 온전한(16MB) 백업을 주면 손대지 않고 끝낸다.

    예전에는 온전한 파일도 "받다 만 것"으로 받아, 기기의 지금 내용과 다른 조각을 다시 읽어 그 파일 위에
    덮었다 — 우리 펌웨어를 올린 뒤에 원래 펌웨어의 백업 이름으로 한 번 잘못 돌리면 하나뿐인 백업이
    지금 펌웨어로 바뀐다(독립 검토).
    """
    print("- test_resume_never_touches_a_complete_backup")
    calls = []

    def fake_port():
        calls.append("port")
        return "/dev/null"

    def fake_esptool(port, *args, **kw):
        calls.append(args[0])
        if args[0] == "verify_flash":  # 기기의 지금 내용은 이 파일과 다르다
            return subprocess.CompletedProcess(args, 1, "verify FAILED", "")
        if args[0] == "read_flash":    # 기기에서 읽은 것(지금 펌웨어)
            with open(args[3], "wb") as f:
                f.write(b"\x55" * int(args[2], 16))
            return subprocess.CompletedProcess(args, 0, "", "")
        return subprocess.CompletedProcess(args, 0, "", "")

    saved = backup.need_download_port, backup.esptool, backup.FLASH_SIZE, backup.CHUNK
    saved_confirm = vibectl.confirm
    vibectl.confirm = lambda port: "ok"  # 이 시험은 "우리 기기"로 답하는 보드를 쓴다
    backup.need_download_port, backup.esptool = fake_port, fake_esptool
    backup.FLASH_SIZE, backup.CHUNK = 0x4000, 0x1000  # 시험에서는 작게(16KB, 4KB 조각) — 판정은 크기와 무관하다
    try:
        with tempfile.TemporaryDirectory() as tmp:
            whole = os.path.join(tmp, "original.bin")
            with open(whole, "wb") as f:
                f.write(b"\xAA" * backup.FLASH_SIZE)  # 원래 펌웨어의 온전한 백업
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    backup.resume(whole)
                code = 0
            except SystemExit as e:
                code = e.code
            data = open(whole, "rb").read()
            check(data == b"\xAA" * backup.FLASH_SIZE, "온전한 백업의 내용이 그대로다")
            check(code not in (0, None), f"실패로 끝난다(지금 끝난 값: {code!r})")
            check(calls == [], f"기기를 찾지도 묻지도 않는다(지금: {calls})")
            check(not os.path.exists(whole + ".part"), "임시 파일을 남기지 않는다")

            # 낡은 .part 가 함께 있어도(사람이 파일을 옮겨 놓은 경우) 온전한 백업은 건드리지 않는다.
            calls.clear()
            with open(whole + ".part", "wb") as f:
                f.write(b"\x55" * 0x1000)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    backup.resume(whole)
                code = 0
            except SystemExit as e:
                code = e.code
            check(open(whole, "rb").read() == b"\xAA" * backup.FLASH_SIZE and code not in (0, None) and calls == [],
                  f".part 가 함께 있어도 온전한 백업은 그대로다(끝난 값 {code!r}, {calls})")
            os.remove(whole + ".part")

            # 받다 만 것(.part)은 예전처럼 잇는다.
            calls.clear()
            out = os.path.join(tmp, "new.bin")
            with open(out + ".part", "wb") as f:
                f.write(b"\x55" * 0x1000)  # 한 조각만 받았다
            def ok_esptool(port, *args, **kw):
                calls.append(args[0])
                if args[0] == "read_flash":
                    with open(args[3], "wb") as f:
                        f.write(b"\x55" * int(args[2], 16))
                return subprocess.CompletedProcess(args, 0, "verify OK (digest matched)", "")
            backup.esptool = ok_esptool
            with contextlib.redirect_stdout(io.StringIO()):
                backup.resume(out)
            check(os.path.exists(out) and os.path.getsize(out) == backup.FLASH_SIZE, "받다 만 것은 끝까지 받아 진짜 이름이 된다")
            check(calls.count("read_flash") == 3 and calls.count("verify_flash") == 1,
                  f"받은 조각은 지문만 묻고 나머지 셋만 읽는다(지금: {calls})")

            # 처음부터 받기(read_all)는 받다 만 것(.part)이 있으면 지우지 않고 --resume 을 알린다.
            calls.clear()
            half = os.path.join(tmp, "half.bin")
            with open(half + ".part", "wb") as f:
                f.write(b"\x11" * 0x3000)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    backup.read_all(half)
                code = 0
            except SystemExit as e:
                code = e.code
            check(os.path.getsize(half + ".part") == 0x3000, "받다 만 것을 비우지 않는다")
            check(isinstance(code, str) and "--resume" in code and calls == [],
                  f"기기를 찾기 전에 --resume 을 알리고 끝낸다(지금: {code!r}, {calls})")
    finally:
        backup.need_download_port, backup.esptool, backup.FLASH_SIZE, backup.CHUNK = saved
        vibectl.confirm = saved_confirm


def test_hist_reply_must_be_whole():
    """기기 속 기록(vibe:hist)의 답은 머리말과 꼬리말의 바이트 수가 같아야 온전하다."""
    print("- test_hist_reply_must_be_whole")
    ok = "vibe:hist begin 12\n607 [link] x\n\nvibe:hist end 12\n"
    check(vibectl.hist_problem(ok) == "", "온전한 답")
    check("꼬리말" in vibectl.hist_problem("vibe:hist begin 12\n607 [li"), "받다가 끊긴 답")
    check("머리말" in vibectl.hist_problem("vibe:? hist\n"), "이 명령을 모르는 옛 펌웨어")
    check("만 왔다" in vibectl.hist_problem("vibe:hist begin 900\n...\nvibe:hist end 300\n"), "기기가 중간에 그만둔 답")


def test_note_hook_check():
    """알림 훅이 "걸려 있는데 아무 일도 하지 않는" 것을 찾아낸다(claude_note.py check).
    (훅이 아예 없는 경우, 그리고 훅은 있는데 그 파이썬에 pyserial 이 없어 조용히 끝나는 경우가 실제로 있었다.)"""
    print("- test_note_hook_check")
    import claude_note

    SCRIPT = os.path.expanduser("~/vibe-stick/cores3/tools/claude_note.py")

    def hooks(py="/usr/bin/python3", script='"$HOME/vibe-stick/cores3/tools/claude_note.py"', tail="", lead="",
              matcher=None, **over):
        def one(kind):
            h = {"type": "command", "timeout": 5, "async": True, "command": f"{lead}{py} {script} {kind}{tail}"}
            h.update(over)
            group = {"hooks": [h]}
            if matcher is not None:
                group["matcher"] = matcher
            return [group]
        other = [{"hooks": [{"type": "command", "command": "python3 ~/other-tool/hook.py", "async": True}]}]
        return {"hooks": {"Stop": other + one("done"), "Notification": one("ask")}}

    yes, no = (lambda _py: True), (lambda _py: False)
    there = lambda _path: True  # noqa: E731
    probs = claude_note.hook_problems

    def both(got, word):
        """두 사건(Stop·Notification) 모두에 그 말이 든 문제가 하나씩 있다."""
        return len(got) == 2 and "Stop" in got[0] and "Notification" in got[1] and all(word in x for x in got)

    check(probs(hooks(), yes, there) == [], f"제대로 걸린 설정에는 문제가 없다(지금: {probs(hooks(), yes, there)})")
    none = probs({"hooks": {"Stop": [{"hooks": [{"type": "command", "command": "python3 x.py"}]}]}}, yes, there)
    check(both(none, "훅이 없다"), f"훅이 없으면 둘 다 알린다(지금: {none})")
    half = hooks()
    del half["hooks"]["Notification"]
    got = probs(half, yes, there)
    check(len(got) == 1 and "Notification" in got[0] and "훅이 없다" in got[0], "하나만 없으면 그것만 알린다")
    check(both(probs(hooks(), no, there), "pyserial"), "그 파이썬에 pyserial 이 없으면 알린다")
    asked = []
    probs(hooks(py="/opt/homebrew/bin/python3"), lambda py: asked.append(py) or True, there)
    check(set(asked) == {"/opt/homebrew/bin/python3"}, f"훅에 적힌 바로 그 파이썬을 본다(지금: {asked})")
    check(both(probs(hooks(**{"async": False}), yes, there), "async"), "async 가 꺼져 있으면 알린다(Claude 가 기다리게 된다)")
    bare = hooks()
    del bare["hooks"]["Stop"][1]["hooks"][0]["async"]
    got = probs(bare, yes, there)
    check(len(got) == 1 and "Stop" in got[0] and "async" in got[0], "async 가 아예 없어도 알린다")
    check(both(probs(hooks(py="python3"), yes, there), "절대 경로"), "파이썬을 이름만 적으면 알린다(창마다 다른 파이썬이 불린다)")
    check(both(probs(hooks(py="/usr/bin/env python3"), yes, there), "절대 경로"), "`env python3` 도 이름만 적은 것이다")
    # 스크립트·파이썬이 그 자리에 없다
    got = probs(hooks(), yes, lambda path: not path.endswith("claude_note.py"))
    check(both(got, "claude_note.py 가 그 자리에 없다"), f"스크립트가 그 자리에 없으면 알린다(지금: {got})")
    got = probs(hooks(py="/no/such/python3"), yes, lambda path: path != "/no/such/python3")
    check(both(got, "/no/such/python3 이 없다"), f"파이썬이 없으면 'pyserial 이 없다'가 아니라 그렇게 알린다(지금: {got})")
    seen = []
    probs(hooks(), yes, lambda path: seen.append(path) or True)
    check(SCRIPT in seen and not any("$HOME" in x or x.startswith("~") for x in seen), f"$HOME 을 풀어서 본다(지금: {seen})")
    # 독립 검토가 짚은 모양들 — 틀리게 "괜찮다"고 하거나, 틀린 까닭을 대던 것
    got = probs(hooks(matcher="idle_prompt"), yes, there)
    check(len(got) == 1 and "Notification" in got[0] and "matcher" in got[0],
          f"답을 기다리는 알림에 걸러내는 조건(matcher)이 붙어 있으면 알린다 — 일부 알림만 온다(지금: {got})")
    check(probs(hooks(matcher=""), yes, there) == [], "빈 matcher 는 '모두'다")
    check(both(probs(hooks(type="http"), yes, there), "command"), "명령을 돌리는 훅(type command)이 아니면 알린다")
    off = hooks()
    off["disableAllHooks"] = True
    got = probs(off, yes, there)
    check(len(got) == 1 and "disableAllHooks" in got[0], f"훅을 모두 끈 설정이면 알린다(지금: {got})")
    check(probs(hooks(tail=" >/dev/null 2>&1"), yes, there) == [], "명령 끝에 출력 버리기가 붙어도 훅을 알아본다")
    asked = []
    got = probs(hooks(lead='export PATH="$HOME/bin:$PATH" && '), lambda py: asked.append(py) or True, there)
    check(got == [] and set(asked) == {"/usr/bin/python3"}, f"앞에 다른 명령이 붙어도 스크립트 바로 앞의 파이썬을 본다(지금: {got}, {asked})")
    asked = []
    got = probs(hooks(py="~/bin/python3"), lambda py: asked.append(py) or True, there)
    check(got == [] and set(asked) == {os.path.expanduser("~/bin/python3")}, f"`~` 로 적은 파이썬은 풀어서 본다(지금: {got}, {asked})")
    twice = hooks()
    twice["hooks"]["Stop"] += twice["hooks"]["Stop"][1:]
    got = probs(twice, yes, there)
    check(len(got) == 1 and "Stop" in got[0] and "두 번" in got[0], f"같은 훅이 둘이면 알린다(알림이 두 번 간다)(지금: {got})")
    for odd in ({}, {"hooks": None}, {"hooks": []}, {"hooks": {"Stop": None}}, {"hooks": {"Stop": [None, 3, {"hooks": "x"}]}},
                {"hooks": {"Stop": [{"hooks": [{"command": 7}, None]}]}}, None, []):
        try:
            check(both(probs(odd, yes, there), "훅이 없다"), f"모양이 이상한 설정({odd!r})도 '훅이 없다'로 읽는다")
        except Exception as e:  # noqa: BLE001
            check(False, f"모양이 이상한 설정({odd!r})에 죽지 않는다(지금: {e!r})")
    # 따옴표가 깨진 명령: 그 훅은 "읽을 수 없다"로, 없는 쪽은 "없다"로
    broken = {"hooks": {"Stop": [{"hooks": [{"command": '/usr/bin/python3 "claude_note.py done', "async": True}]}]}}
    try:
        got = probs(broken, yes, there)
        check(len(got) == 2 and "Stop" in got[0] and "읽을 수 없다" in got[0] and "훅이 없다" in got[1],
              f"따옴표가 깨진 명령은 '읽을 수 없다'로 알린다(지금: {got})")
    except Exception as e:  # noqa: BLE001
        check(False, f"따옴표가 깨진 명령에 죽지 않는다(지금: {e!r})")
    # 파이썬을 실제로 띄워 보는 쪽(가짜를 끼우지 않고)
    has = claude_note.python_has_serial
    check(has(sys.executable), "이 시험을 돌리는 파이썬에는 알림에 필요한 것이 다 있다")
    check(not has("/no/such/python3"), "없는 파이썬은 '없다'")
    check(not has("/bin/echo"), "파이썬이 아닌 프로그램(무엇을 주든 성공으로 끝나는 echo)에 속지 않는다")
    # 여러 설정 파일의 훅을 합쳐 본다(사용자 설정 + 사용자 로컬 설정)
    merged = claude_note.merge_settings([half, {"hooks": {"Notification": hooks()["hooks"]["Notification"]}}, None, {"hooks": 3}])
    check(probs(merged, yes, there) == [], "훅이 두 파일에 나뉘어 있어도 합쳐서 본다")
    merged = claude_note.merge_settings([hooks(), {"disableAllHooks": True}])
    check(any("disableAllHooks" in x for x in probs(merged, yes, there)), "어느 한 파일이 훅을 모두 꺼도 알린다")


# ---------- 보안 점검에서 나온 것 ----------

def test_note_ignores_shadow_modules():
    """훅으로 자동 실행되는 claude_note.py 는 제 폴더에 놓인 같은 이름의 파일(json.py · shlex.py …)을 표준 라이브러리
    대신 실행하지 않는다. 파이썬은 스크립트의 폴더를 찾는 길 맨 앞에 두므로, 손보지 않으면 그 폴더에 생긴
    `shlex.py` 가 Claude 가 답을 끝낼 때마다 실행된다(독립 검토 이 재현)."""
    print("- test_note_ignores_shadow_modules")
    import shutil
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copy(os.path.join(TOOLS, "claude_note.py"), tmp)
        mark = os.path.join(tmp, "ran.txt")
        for name in ("json", "select", "shlex", "subprocess", "serial", "signal", "types"):
            with open(os.path.join(tmp, name + ".py"), "w", encoding="utf-8") as f:
                f.write(f"open({mark!r}, 'a').write({name!r} + '\\n')\nraise ImportError('가짜 {name}')\n")
        env = dict(os.environ, CLAUDE_CODE_SESSION_ATTENDED="0")  # 알리지 않는 실행: 기기에 아무것도 보내지 않는다
        r = subprocess.run([sys.executable, os.path.join(tmp, "claude_note.py"), "done"], input="{}", text=True,
                           capture_output=True, env=env, timeout=60, cwd=tmp)
        ran = open(mark, encoding="utf-8").read().split() if os.path.exists(mark) else []
        check(r.returncode == 0, f"그런 파일이 곁에 있어도 0 으로 끝난다(지금: {r.returncode}, {r.stderr[-200:]!r})")
        check(ran == [], f"곁의 같은 이름 파일을 실행하지 않는다(실행된 것: {ran})")
        # 알리는 실행(보내는 갈래까지 간다): 진짜 pyserial 을 들여온 뒤, 이 폴더에 vibectl.py 가 없어 포트를 열기 전에
        # 조용히 끝난다 — 기기에는 아무것도 가지 않는다. 곁의 가짜 serial.py 가 진짜 pyserial 을 가리지 않는지 본다.
        env2 = {k: v for k, v in os.environ.items() if not k.startswith("CLAUDE_CODE")}
        r = subprocess.run([sys.executable, os.path.join(tmp, "claude_note.py"), "done"], input="{}", text=True,
                           capture_output=True, env=env2, timeout=60, cwd=tmp)
        ran = open(mark, encoding="utf-8").read().split() if os.path.exists(mark) else []
        check(r.returncode == 0 and ran == [], f"보내는 갈래에서도 곁의 가짜 파일을 실행하지 않는다(실행된 것: {ran}, 끝난 값 {r.returncode})")
        # 점검 갈래(check)도 마찬가지다 — 설정과 포트 목록을 읽기만 한다(기기에 쓰지 않는다).
        r = subprocess.run([sys.executable, os.path.join(tmp, "claude_note.py"), "check"], text=True,
                           capture_output=True, timeout=120, cwd=tmp)
        ran = open(mark, encoding="utf-8").read().split() if os.path.exists(mark) else []
        check(ran == [], f"check 도 곁의 같은 이름 파일을 실행하지 않는다(실행된 것: {ran}, 끝난 값 {r.returncode})")
    # 다른 프로그램이 들여올 때(import)는 찾는 길을 건드리지 않는다
    import claude_note  # noqa: F401
    check(os.path.abspath(sys.path[0]) == os.path.abspath(TOOLS), "들여오는 것만으로는 찾는 길의 차례를 바꾸지 않는다")


def test_scratch_dirs_are_removed():
    """맥 쪽 확인 도구가 만든 녹음(방의 소리가 담긴다)은 끝나면 지운다. 점검: 이틀 전의 녹음 넷이
    임시 폴더에 그대로 남아 있었다."""
    print("- test_scratch_dirs_are_removed")
    saved = os.environ.pop("VIBE_KEEP", None)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            with check_mac.scratch("vibe-test-") as d:
                made = d
                open(os.path.join(d, "rec.wav"), "wb").write(b"x" * 100)
                check(os.path.isdir(d) and os.path.basename(d).startswith("vibe-test-"), "쓰는 동안에는 있다")
                check(oct(os.stat(d).st_mode & 0o777) == "0o700", "폴더는 본인만 볼 수 있다")
        check(not os.path.exists(made), "끝나면 폴더째 지운다")
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                with check_mac.scratch("vibe-test-") as d:
                    made = d
                    raise RuntimeError("도중에 멈춤")
        except RuntimeError:
            pass
        check(not os.path.exists(made), "도중에 멈춰도 지운다")
        os.environ["VIBE_KEEP"] = "1"
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            with check_mac.scratch("vibe-test-") as d:
                made = d
        check(os.path.isdir(made) and made in out.getvalue(), "VIBE_KEEP=1 이면 남기고 자리를 알린다")
        os.rmdir(made)
    finally:
        os.environ.pop("VIBE_KEEP", None)
        if saved is not None:
            os.environ["VIBE_KEEP"] = saved


def test_device_text_is_made_printable():
    """기기가 보낸 글을 터미널에 찍기 전에 제어 문자를 걸러 낸다(같은 USB 번호를 흉내 낸 장치가 터미널을 조종하는
    글을 보낼 수 있고, 이 출력은 Claude 의 도구 결과로도 들어간다)."""
    print("- test_device_text_is_made_printable")
    clean = vibectl.printable
    check(clean("[link] READY t=1\n[ev] 3\n") == "[link] READY t=1\n[ev] 3\n", "평범한 줄은 그대로다")
    check(clean("한글 · 탭\t끝\n") == "한글 · 탭\t끝\n", "한글 · 탭 · 줄바꿈은 그대로다")
    got = clean("a\x1b[31mb\x07c\x00d\x7fe\r\n")
    check("\x1b" not in got and "\x07" not in got and "\x00" not in got and "\x7f" not in got and "\r" not in got,
          f"ESC · 벨 · 0 · DEL · CR 을 걸러 낸다(지금: {got!r})")
    check(got.endswith("\n") and got.startswith("a") and "b" in got and "e" in got, "글자는 남긴다")
    check(clean("\x9b31m\u202e거꾸로") == "\ufffd31m\ufffd거꾸로", "8비트 제어 문자와 글 방향을 뒤집는 문자도 걸러 낸다")
    for ch in ("\u200b", "\u200e", "\u200f", "\u2028", "\u2029", "\ufeff", "\u2066", "\u2069"):
        check(clean(f"a{ch}b") == "a\ufffdb", f"보이지 않는 문자 U+{ord(ch):04X} 도 걸러 낸다")
    out = io.StringIO()

    class Fake:  # 포트 흉내: 한 번에 다 주고 그다음은 빈 것
        def __init__(self, data):
            self.data = data

        def read(self, n):
            d, self.data = self.data, b""
            return d

    text = vibectl.pump(Fake(b"ok\x1b]0;title\x07\n"), 0.2, out)
    check("\x1b" not in out.getvalue() and "\x07" not in out.getvalue(), "pump 가 찍는 글에도 제어 문자가 없다")
    check("\x1b" in text, "돌려주는 글(판정에 쓰는 것)은 받은 그대로다")


def test_backup_confirms_board_and_keeps_files_private():
    """백업을 받기 전에도 '우리 기기'인지 확인하고(엉뚱한 보드의 내용을 이 기기의 원본으로 믿지 않게), 받은 파일은
    본인만 읽을 수 있게 만든다(다른 펌웨어의 와이파이 비밀번호가 들어 있다)."""
    print("- test_backup_confirms_board_and_keeps_files_private")
    seen = []

    def stop_confirm(port):
        seen.append(port)
        sys.exit("가짜 확인에서 멈춤")

    saved_confirm = vibectl.confirm
    vibectl.confirm = stop_confirm
    try:
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "new.bin")
            calls, code = run_without_device(backup.read_all, out)
            check(seen == ["/dev/null"] and code == "가짜 확인에서 멈춤", f"읽기 전에 그 포트의 보드를 확인한다(지금: {seen}, {code!r})")
            check("esptool" not in calls and not os.path.exists(out + ".part"), f"확인이 멈추면 읽지도, 파일을 만들지도 않는다(지금: {calls})")
            seen.clear()
            with open(out + ".part", "wb") as f:
                f.write(b"\x55" * 0x1000)
            calls, code = run_without_device(backup.resume, out)
            check(seen == ["/dev/null"] and "esptool" not in calls, f"이어 받을 때도 먼저 확인한다(지금: {seen}, {calls})")
    finally:
        vibectl.confirm = saved_confirm

    # 받는 동안의 임시 파일(.part)과 다 받은 파일의 권한
    def ok_esptool(port, *args, **kw):
        if args[0] == "read_flash":
            with open(args[3], "wb") as f:
                f.write(b"\x33" * int(args[2], 16))
        return subprocess.CompletedProcess(args, 0, "verify OK (digest matched)", "")

    saved = backup.need_download_port, backup.esptool, backup.FLASH_SIZE, backup.CHUNK, vibectl.confirm
    backup.need_download_port, backup.esptool = (lambda: "/dev/null"), ok_esptool
    backup.FLASH_SIZE, backup.CHUNK = 0x4000, 0x1000
    vibectl.confirm = lambda port: "ok"
    old_umask = os.umask(0o022)  # 평소의 설정(남도 읽을 수 있게 만들어지는)에서도
    try:
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "a.bin")
            with contextlib.redirect_stdout(io.StringIO()):
                backup.read_all(out)
            check(os.path.getsize(out) == backup.FLASH_SIZE, "다 받는다")
            check(oct(os.stat(out).st_mode & 0o777) == "0o600", f"받은 파일은 본인만 읽는다(지금: {oct(os.stat(out).st_mode & 0o777)})")
            out2 = os.path.join(tmp, "b.bin")
            with open(out2 + ".part", "wb") as f:
                f.write(b"\x33" * 0x1000)
            os.chmod(out2 + ".part", 0o644)
            with contextlib.redirect_stdout(io.StringIO()):
                backup.resume(out2)
            check(oct(os.stat(out2).st_mode & 0o777) == "0o600", f"이어 받은 파일도 본인만 읽는다(지금: {oct(os.stat(out2).st_mode & 0o777)})")
    finally:
        os.umask(old_umask)
        backup.need_download_port, backup.esptool, backup.FLASH_SIZE, backup.CHUNK, vibectl.confirm = saved
    # 받다 만 파일이 저장소 안 어디에 생겨도 실수로 커밋되지 않는다
    r = subprocess.run(["git", "-C", os.path.join(TOOLS, "..", ".."), "check-ignore", "-q", "cores3/anywhere.bin.part"])
    check(r.returncode == 0, "받다 만 파일(*.part)은 저장소 어디에 있어도 git 이 넣지 않는다")


def test_restore_asks_before_overwriting():
    """되돌려 쓰기(--restore)는 기기의 16MB 를 통째로 덮는다. 쓰기 전에 그 파일의 지문을 보여 주고 사람에게 묻는다 —
    "이걸로 되돌리세요"라며 남이 건넨 파일을 그대로 굽는 일이 없게(공개 전 검토)."""
    print("- test_restore_asks_before_overwriting")
    wrote = []

    def fake_run(cmd, **kw):
        wrote.append(cmd)
        return subprocess.CompletedProcess(cmd, 0)

    saved = backup.need_download_port, vibectl.confirm, backup.subprocess.run, backup.FLASH_SIZE
    backup.need_download_port, vibectl.confirm, backup.subprocess.run = (lambda: "/dev/null"), (lambda port: "ok"), fake_run
    backup.FLASH_SIZE = 0x4000
    try:
        with tempfile.TemporaryDirectory() as tmp:
            path = make_file(os.path.join(tmp, "full.bin"), backup.FLASH_SIZE)
            for answer, should_write in (("", False), ("n", False), ("yes", False), ("y", False), ("restore", True)):
                wrote.clear()
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    try:
                        backup.restore(path, ask=lambda prompt, a=answer: a)
                        code = 0
                    except SystemExit as e:
                        code = e.code
                check(bool(wrote) == should_write, f"답 {answer!r} → 쓰기 {'한다' if should_write else '안 한다'}(지금: {len(wrote)}번, 끝난 값 {code!r})")
                check("sha256" in out.getvalue(), "묻기 전에 파일의 지문을 보여 준다")
            # 물어볼 사람이 없으면(입력이 닫혀 있으면) 쓰지 않는다
            wrote.clear()

            def no_one(prompt):
                raise EOFError

            with contextlib.redirect_stdout(io.StringIO()):
                try:
                    backup.restore(path, ask=no_one)
                except SystemExit:
                    pass
            check(not wrote, "물어볼 사람이 없으면 쓰지 않는다")
    finally:
        backup.need_download_port, vibectl.confirm, backup.subprocess.run, backup.FLASH_SIZE = saved


def test_host_test_constants_match_config():
    """컴퓨터에서 도는 시험(test_touch.cpp)이 손으로 적어 둔 값이 기기의 설정(config.h)과 같다.
    config.h 는 기기의 헤더를 들여와 그 시험에서 바로 읽지 못한다 — 어긋나면 시험은 통과하는데 기기는 다른 값으로 돈다."""
    print("- test_host_test_constants_match_config")
    import re
    config = open(os.path.join(TOOLS, "..", "main", "config.h"), encoding="utf-8").read()
    tests = open(os.path.join(HERE, "test_touch.cpp"), encoding="utf-8").read()

    def const(src, name):
        m = re.search(r"^static const \w+\s+%s\s*=\s*(\d+)\s*;" % re.escape(name), src, re.M)
        return int(m.group(1)) if m else None

    for theirs, ours in (("GUESS_VOICE_MIN", "GUESS_MIN"), ("GUESS_VOICE_RATIO", "GUESS_RATIO"), ("GUESS_VOICE_RUN", "GUESS_RUN")):
        a, b = const(config, theirs), const(tests, ours)
        check(a is not None and a == b, f"config.h 의 {theirs}({a}) = test_touch.cpp 의 {ours}({b})")
    wake = const(config, "QUIET_WAKE_CALM_MS")
    used = re.search(r"quiet\.arm\(now, 0, (\d+)\);\s*// config\.h 의 QUIET_WAKE_CALM_MS", tests)
    check(wake is not None and used is not None and int(used.group(1)) == wake,
          f"꺼진 화면을 깨운 뒤의 뜸: config.h({wake}) = 시험이 쓰는 값({used.group(1) if used else None})")


def test_screen_words_are_not_shared():
    """화면의 말 하나는 한 가지 뜻으로만 쓴다(D72): 그림을 만드는 표(gen_art.py)에 같은 글이 두 번 없고,
    표의 길이가 펌웨어가 쓰는 번호표(art_data.h)와 같다. (그림 도구 없이 글로만 본다.)"""
    print("- test_screen_words_are_not_shared")
    src = open(os.path.join(TOOLS, "gen_art.py"), encoding="utf-8").read()
    tables = {}
    for node in ast.parse(src).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            if node.targets[0].id in ("WORDS", "HINTS"):
                tables[node.targets[0].id] = list(ast.literal_eval(node.value))
    check(set(tables) == {"WORDS", "HINTS"}, f"WORDS 와 HINTS 를 찾았다(지금: {sorted(tables)})")
    every = [t.strip().lower() for t in tables.get("WORDS", []) + tables.get("HINTS", [])]
    twice = sorted({t for t in every if every.count(t) > 1})
    check(not twice, f"같은 글을 두 곳에 쓰지 않는다(지금 겹치는 것: {twice})")
    header = open(os.path.join(TOOLS, "..", "logic", "art_data.h"), encoding="utf-8").read()

    def enum_names(name):
        """art_data.h 의 그 enum 에서 _COUNT 앞까지의 이름들. 주석은 먼저 지운다(중괄호 안에 주석이 들어가도 세는 것이 틀리지 않게)."""
        bare = "\n".join(ln.split("//", 1)[0] for ln in header.splitlines())
        body = bare.split(f"enum {name} : uint8_t {{", 1)[1].split("}", 1)[0]
        names = [x.split("=")[0].strip() for x in body.split(",") if x.strip()]
        return names[:names.index(f"{name.upper()}_COUNT")]

    words, hints = enum_names("Word"), enum_names("Hint")
    check(len(words) == len(tables.get("WORDS", [])), "낱말의 수가 art_data.h 의 Word 와 같다")
    check(len(hints) == len(tables.get("HINTS", [])), "안내 말의 수가 art_data.h 의 Hint 와 같다")
    # 수만 같고 차례가 어긋나면 엉뚱한 말이 뜬다: 뜻을 나눈 셋은 이름과 글을 짝지어 본다.
    by_name = dict(zip(words, tables.get("WORDS", [])))
    by_name.update(zip(hints, tables.get("HINTS", [])))
    for name, text in (("WORD_OFF", "Not connected"), ("HINT_NOTSENT", "Key not sent"),
                       ("HINT_NOTCLEARED", "Pairing not cleared"), ("HINT_CLEARED", "Pairing cleared")):
        check(by_name.get(name) == text, f"{name} 의 글은 `{text}` 다(지금: {by_name.get(name)!r})")


if __name__ == "__main__":
    test_esptool_timeout_returns_text()
    test_verify_rejects_partial_backup_before_device()
    test_restore_rejects_partial_backup_before_device()
    test_check_mac_run_timeout_is_a_failed_result()
    test_check_list_survives_a_stuck_command()
    test_note_only_for_attended_sessions()
    test_pump_survives_another_reader()
    test_mac_is_parsed_strictly()
    test_wanted_mac_env_then_file()
    test_confirm_refuses_the_wrong_board()
    test_restore_confirms_the_board_before_writing()
    test_backup_never_overwrites_an_existing_file()
    test_resume_never_touches_a_complete_backup()
    test_hist_reply_must_be_whole()
    test_note_hook_check()
    test_note_ignores_shadow_modules()
    test_scratch_dirs_are_removed()
    test_device_text_is_made_printable()
    test_backup_confirms_board_and_keeps_files_private()
    test_restore_asks_before_overwriting()
    test_host_test_constants_match_config()
    test_screen_words_are_not_shared()
    print(f"통과 {passed}, 실패 {failed}")
    sys.exit(1 if failed else 0)

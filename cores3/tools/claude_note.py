#!/usr/bin/env python3
"""Claude Code 의 훅이 부른다: vibe-stick(화면 판)에 알린다 — 화면에 잠깐 뜨고 소리가 난다(D63).

    claude_note.py done   Claude 가 일을 끝냈다            (Stop 훅)
    claude_note.py ask    Claude 가 사람의 답을 기다린다    (Notification 훅)

알리지 않는 것:
  - 사람이 보고 있지 않은 Claude(자동으로 돌리는 `claude -p`, SDK). 그런 실행에는 Claude Code 가
    CLAUDE_CODE_SESSION_ATTENDED=0 · CLAUDE_CODE_ENTRYPOINT=sdk-* 를 붙인다(claude 2.1.283 에서 확인).
  - "입력을 기다리는 중"(끝난 뒤 한참 손대지 않으면 오는 알림) — 끝났다는 알림이 이미 갔다. 로그인 성공 알림도.

기기가 꽂혀 있지 않거나 답이 없으면 아무 일도 하지 않고 조용히 끝난다 — Claude 의 일을 막거나 늦추지 않는다.
선이 꽂혀 있을 때만 된다(알림은 USB 의 로그 포트로 간다).

Claude Code 설정(~/.claude/settings.json)의 "hooks" 에 넣는 모양(<저장소> 는 이 저장소를 내려받은 폴더):

    "Stop":         [{"hooks": [{"type": "command", "timeout": 5, "async": true,
                      "command": "<파이썬> \"<저장소>/cores3/tools/claude_note.py\" done"}]}],
    "Notification": [{"hooks": [{"type": "command", "timeout": 5, "async": true,
                      "command": "<파이썬> \"<저장소>/cores3/tools/claude_note.py\" ask"}]}]

<파이썬> 은 그 컴퓨터에서 pyserial 이 들어 있는 python3 를 절대 경로로 적는다(D73). 예:
    /opt/homebrew/bin/python3   홈브루 파이썬에 pyserial 을 깔아 둔 경우
    /usr/bin/python3            맥에 딸린 파이썬(3.9)의 사용자 꾸러미에 pyserial 이 있는 경우
  이유: 이름만 적으면(`python3`) Claude 를 띄운 곳(터미널 · VS Code)에 따라 다른 파이썬이 불린다. pyserial 이 없는
        파이썬이 불리면 이 스크립트는 아무 일도 하지 않고 조용히 끝난다 — 알림이 오지 않는데 까닭이 보이지 않는다.
        홈브루 파이썬에 깐 꾸러미는 홈브루가 파이썬을 올릴 때(3.14 → 3.15) 사라질 수 있다.
  비용: 컴퓨터마다 줄이 다르다. 맥에 딸린 파이썬은 3.9 라, 이 스크립트와 vibectl.py 는 3.9 에서 돌아야 한다.
  탈출구: 걸어 둔 것이 정말 일하는지는 `claude_note.py check` 가 알려 준다(훅이 있는가 · async 인가 ·
          그 파이썬에 pyserial 이 있는가 · 기기가 꽂혀 있는가). 알림이 오지 않으면 이것부터 돌린다.

**주의: 이 훅은 이 파일을 Claude 가 답을 끝낼 때마다 자동으로 실행한다.** 읽어 본, 믿을 수 있는 사본의 경로만 걸 것.
`git pull` 로 갱신하는 저장소의 경로를 그대로 걸면, 그 저장소에 들어온 커밋 하나가 곧 이 컴퓨터에서 도는 코드가 된다.
이 파일과 vibectl.py(이 파일이 불러 쓴다)를 따로 떠서 쓰기를 막아 두고 거기에 거는 편이 낫다 — docs/사용법-cores3.md.
그렇게 뜬 사본은 cores3/.vibe-mac 을 보지 못하므로, 훅의 명령 앞에 VIBE_MAC=<MAC> 을 붙인다.

"async": true 를 빼지 말 것 — 빼면 Claude 가 이 스크립트가 끝나기를 기다린다(포트가 막히면 5초까지).

빼려면 그 두 묶음을 지운다. 손으로 불러 볼 수도 있다(터미널에서 `claude_note.py done`).
"""
import os
import sys

if __name__ == "__main__":
    # 이 스크립트는 훅으로 자동 실행된다(Claude 가 답을 끝낼 때마다). 파이썬은 스크립트의 폴더를 "찾는 길"의 맨 앞에
    # 두므로, 그대로 두면 이 폴더에 생긴 json.py · shlex.py · select.py 같은 파일이 표준 라이브러리 대신 실행된다
    # (실험하다 만든 파일, 동기화로 들어온 파일 — 시험 test_note_ignores_shadow_modules 가 재현한다).
    # 그래서 다른 것을 들여오기 전에 이 폴더를 맨 뒤로 보낸다: 표준 라이브러리와 설치된 꾸러미(pyserial)가 먼저다.
    # 우리 파일(vibectl.py)은 그 뒤에서 찾는다. os · sys 는 파이썬이 켜질 때 이미 올라와 있어 가려지지 않는다.
    # (경로는 링크를 푼 진짜 자리로 견준다 — 파이썬은 찾는 길 맨 앞에 링크를 푼 폴더를 넣는다.)
    _here = os.path.dirname(os.path.realpath(__file__))
    sys.path[:] = [p for p in sys.path if os.path.realpath(p or os.curdir) != _here] + [_here]

import json  # noqa: E402
import select  # noqa: E402
import shlex  # noqa: E402
import subprocess  # noqa: E402

VID, PID_APP = 0x303A, 0x4015  # vibectl.py 와 같다
QUIET_TYPES = ("idle_prompt", "auth_success")


def should_notify(kind, env, info):
    """알릴 일인가. env: 환경변수, info: 훅이 넘겨준 것(없으면 빈 것)."""
    if kind not in ("done", "ask"):
        return False
    if env.get("CLAUDE_CODE_SESSION_ATTENDED") == "0":  # 사람이 보고 있지 않다(봇의 claude -p)
        return False
    if env.get("CLAUDE_CODE_ENTRYPOINT", "").startswith("sdk"):
        return False
    if kind == "ask":
        if str(info.get("notification_type", "")) in QUIET_TYPES:
            return False
        if "waiting for your input" in str(info.get("message", "")):
            return False
    return True


def hook_input():
    """훅이 표준 입력으로 넘겨준 것(JSON). 손으로 불렀거나 읽을 수 없으면 빈 것. 기다리며 멈추지 않는다."""
    try:
        if sys.stdin is None or sys.stdin.isatty():
            return {}
        if not select.select([sys.stdin], [], [], 0.3)[0]:
            return {}
        data = b""
        while len(data) < 1 << 20:
            chunk = os.read(sys.stdin.fileno(), 65536)
            if not chunk:
                break
            data += chunk
            if not select.select([sys.stdin], [], [], 0.05)[0]:
                break
        got = json.loads(data.decode("utf-8", "replace") or "{}")
        return got if isinstance(got, dict) else {}
    except BaseException:
        return {}


def send(kind):
    if kind not in ("done", "ask"):
        return False
    try:
        import serial
        from serial.tools import list_ports
        here = os.path.dirname(os.path.realpath(__file__))
        if here not in sys.path:
            sys.path.append(here)  # 맨 뒤에: 이 폴더의 파일이 표준 라이브러리·pyserial 을 가리지 않게(위 설명)
        import vibectl
        only = vibectl.wanted_mac()  # 이 컴퓨터의 "우리 기기"(적어 두었으면 그 기기에만)
        for p in list_ports.comports():
            if p.vid == VID and p.pid == PID_APP and p.device.startswith("/dev/cu."):
                if only and vibectl._mac(p.serial_number) != only:
                    continue
                with serial.Serial(p.device, 115200, timeout=0.2, write_timeout=0.5) as ser:
                    ser.write(f"vibe:note {kind}\n".encode())
                    ser.flush()
                return True
    except BaseException:  # 어떤 일이 있어도(도구 없음, 포트 바쁨, 설정 오타) Claude 의 일을 막지 않는다
        pass
    return False


# ---------- 걸어 둔 훅이 정말 일하는가(claude_note.py check) ----------

WANTED_HOOKS = (("Stop", "done"), ("Notification", "ask"))


def merge_settings(parts):
    """여러 설정 파일의 내용에서 훅만 합친다(Claude Code 는 사용자 설정과 로컬 설정의 훅을 모두 돌린다).
    어느 한 파일이라도 훅을 모두 껐으면(disableAllHooks) 그것도 옮긴다. 모양이 이상한 것은 건너뛴다."""
    out = {"hooks": {}}
    for part in parts:
        if not isinstance(part, dict):
            continue
        if part.get("disableAllHooks") is True:
            out["disableAllHooks"] = True
        hooks = part.get("hooks")
        for event, groups in (hooks.items() if isinstance(hooks, dict) else ()):
            if isinstance(groups, list):
                out["hooks"].setdefault(event, []).extend(groups)
    return out


def _note_hooks(settings, event):
    """설정에서 그 사건에 걸린 훅 가운데 이 스크립트를 부르는 것들 → [(훅, 그 묶음의 matcher)].
    설정의 모양이 이상해도 죽지 않는다."""
    found = []
    hooks = settings.get("hooks") if isinstance(settings, dict) else None
    groups = hooks.get(event) if isinstance(hooks, dict) else None
    for group in groups if isinstance(groups, list) else []:
        inner = group.get("hooks") if isinstance(group, dict) else None
        for h in inner if isinstance(inner, list) else []:
            if isinstance(h, dict) and isinstance(h.get("command"), str) and "claude_note.py" in h["command"]:
                found.append((h, group.get("matcher")))
    return found


def python_has_serial(python):
    """그 파이썬으로 알림을 보낼 수 있는가: 보낼 때 쓰는 것(pyserial 과 그 포트 목록, vibectl)을 모두 들여올 수 있는가.
    실제로 띄워 본다. 답을 셈으로 받는다 — 무엇을 주든 성공으로 끝나는 프로그램(echo)이나 이름만 같은 다른 `serial`
    꾸러미에 속지 않게(독립 검토)."""
    here = os.path.dirname(os.path.realpath(__file__))
    # 찾는 길: 지금 폴더('')를 빼고, 우리 폴더는 맨 뒤에(훅으로 돌 때와 같은 차례 — 이 폴더의 같은 이름 파일에 속지 않는다).
    code = ("import sys; sys.path[:] = [p for p in sys.path if p] + [sys.argv[1]]; import serial; "
            "from serial.tools import list_ports; import vibectl; print('vibe-note-probe:' + str(6 * 7))")
    try:
        r = subprocess.run([python, "-c", code, here], capture_output=True, text=True, timeout=20)
        return r.returncode == 0 and r.stdout.strip().endswith("vibe-note-probe:42")
    except (OSError, ValueError, subprocess.SubprocessError):
        return False


def hook_problems(settings, has_serial=python_has_serial, exists=os.path.exists):
    """설정(~/.claude/settings.json 의 내용)에 알림 훅이 제대로 걸려 있는가. 문제마다 한 줄, 없으면 빈 목록.
    명령은 "… <파이썬> <…/claude_note.py> <done|ask> …" 꼴로 읽는다: 앞에 다른 명령(`export … &&`)이나 뒤에
    출력 버리기(`>/dev/null`)가 붙어도 스크립트의 바로 앞 낱말이 파이썬, 바로 뒤 낱말이 알림의 종류다."""
    if isinstance(settings, dict) and settings.get("disableAllHooks") is True:
        return ['설정에 "disableAllHooks": true 가 있다 — 훅이 모두 꺼져 있어 알림이 가지 않는다']
    out = []

    def add(line):
        if line not in out:
            out.append(line)

    for event, kind in WANTED_HOOKS:
        mine, unreadable = [], False
        for h, matcher in _note_hooks(settings, event):
            try:
                words = shlex.split(h["command"])
            except ValueError:
                add(f"{event}: 훅의 명령을 읽을 수 없다(따옴표가 맞지 않는다): {h['command']}")
                unreadable = True
                continue
            at = next((i for i, w in enumerate(words) if w.endswith("claude_note.py")), -1)
            if at >= 0 and at + 1 < len(words) and words[at + 1] == kind:
                mine.append((h, matcher, words, at))
        if not mine:
            if not unreadable:
                add(f"{event}: `claude_note.py {kind}` 를 부르는 훅이 없다")
            continue
        if len(mine) > 1:
            add(f"{event}: 같은 훅이 {len(mine)}개 걸려 있다 — 알림이 두 번 간다")
        for h, matcher, words, at in mine:
            if h.get("type") != "command":
                add(f'{event}: 훅의 "type" 이 "command" 가 아니다({h.get("type")!r}) — 명령을 돌리지 않는다')
            if h.get("async") is not True:
                add(f'{event}: "async": true 가 없다 — Claude 가 이 스크립트가 끝나기를 기다린다')
            # Notification 의 matcher 는 알림의 종류를 거른다(Stop 에는 matcher 가 없다 — 적어도 무시된다).
            if event == "Notification" and isinstance(matcher, str) and matcher not in ("", "*"):
                add(f"{event}: 묶음에 matcher({matcher!r})가 붙어 있다 — 그 종류의 알림에만 온다(허락을 기다리는 알림이 빠질 수 있다)")
            python = os.path.expanduser(os.path.expandvars(words[at - 1])) if at > 0 else ""
            if not os.path.isabs(python):
                add(f"{event}: 파이썬을 절대 경로로 적지 않았다({python or '없음'}) — Claude 를 띄운 곳에 따라 다른 파이썬이 불린다")
            elif not exists(python):
                add(f"{event}: 파이썬 {python} 이 없다")
            elif not has_serial(python):
                add(f"{event}: {python} 으로는 알림을 보낼 수 없다(pyserial 이 없거나 들여오지 못한다) — 알림이 조용히 사라진다")
            script = os.path.expanduser(os.path.expandvars(words[at]))
            if not exists(script):
                add(f"{event}: 훅이 부르는 claude_note.py 가 그 자리에 없다({script})")
    return out


def check_main(paths=None):
    """`claude_note.py check`: 사람이 읽을 결과를 찍고, 문제가 없으면 0. 읽기만 한다(설정도 기기도 고치지 않는다)."""
    paths = paths or [os.path.expanduser("~/.claude/settings.json"), os.path.expanduser("~/.claude/settings.local.json")]
    parts, read = [], []
    for path in paths:
        if not os.path.exists(path):
            continue
        try:
            with open(path, encoding="utf-8") as f:
                parts.append(json.load(f))
            read.append(path)
        except (OSError, ValueError) as e:
            print(f"설정을 읽지 못했다({path}): {e}")
            return 1
    if not parts:
        print(f"설정 파일이 없다({', '.join(paths)})")
        return 1
    try:
        problems = hook_problems(merge_settings(parts))
    except Exception as e:  # noqa: BLE001
        print(f"점검하다 멈췄다: {e!r}")
        return 1
    for line in problems:
        print("문제 —", line)
    if not problems:
        print("훅: 괜찮다 — 끝났을 때와 답을 기다릴 때 둘 다 걸려 있고, 그 파이썬으로 알림을 보낼 수 있다")
    print(f"  (본 곳: {', '.join(read)}. 프로젝트 폴더의 .claude/settings*.json 과 회사 정책 설정은 보지 않는다)")
    try:
        from serial.tools import list_ports
        here = [p.device for p in list_ports.comports()
                if p.vid == VID and p.pid == PID_APP and p.device.startswith("/dev/cu.")]
        print("기기: " + (f"꽂혀 있다({', '.join(here)})" if here else "꽂혀 있지 않다 — 알림은 선이 꽂혀 있을 때만 간다"))
    except Exception as e:  # noqa: BLE001
        print(f"기기: 볼 수 없다(이 파이썬에 pyserial 이 없다: {e})")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "check":
        sys.exit(check_main())
    try:
        what = sys.argv[1] if len(sys.argv) > 1 else ""
        if should_notify(what, os.environ, hook_input()):
            send(what)
    except BaseException:
        pass
    sys.exit(0)

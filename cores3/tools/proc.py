"""다른 프로그램을 돌리고 그 결과를 받는다. cores3/tools 의 도구들이 함께 쓴다(backup.py, check_mac.py)."""
import subprocess


def run_text(cmd, timeout=None):
    """cmd 를 돌려 끝난 값과 출력(글자)을 돌려준다. 시간 초과도 예외가 아니라 "실패한 결과"로 준다.

    이유: 시간 초과를 예외로 흘리면 부르는 쪽이 하던 일(끊긴 조각 다시 읽기, 통과·실패 집계)을 마치지
          못하고 통째로 죽는다. 그리고 파이썬은 시간 초과 때 text=True 여도 그때까지의 출력을 바이트로
          준다(없으면 None) — 그대로 돌려주면 r.stdout + r.stderr 가 TypeError 로 죽는다.
    돌려주는 것: 시간 초과면 끝난 값 1, stdout 은 그때까지 나온 것(글자), stderr 는 "시간 초과".
    """
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        out = e.stdout or ""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        return subprocess.CompletedProcess(cmd, 1, out, "시간 초과")

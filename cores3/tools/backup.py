#!/usr/bin/env python3
"""기기의 저장 내용(플래시 16MB)을 통째로 파일에 받아 둔다. 새 펌웨어를 올리기 전에 한 번.

    backup.py <결과.bin>           내려받기 모드인 기기에서 받는다(RST 3초 → 초록 불). 받는 동안은
                                   <결과.bin>.part 에 쓰고, 다 받아야 그 이름이 된다. 이미 있는 파일은 덮어쓰지 않는다
    backup.py --resume <결과.bin>  받다 만 파일을 이어서 받는다: 이미 받은 조각은 기기에게 지문(MD5)을
                                   물어 지금 내용과 같으면 그대로 두고, 다르거나 없는 조각만 다시 읽는다.
                                   이미 온전한(16MB) 파일에는 손대지 않는다 — 온전한 백업을 기기의 지금 내용으로
                                   덮지 않는다
    backup.py --verify <파일.bin>  기기의 내용이 파일과 같은지 확인한다(파일이 꼭 16MB 일 때만 — 덜 받은
                                   파일은 기기에 묻기 전에 실패로 끝낸다)
    backup.py --restore <파일.bin> 파일을 기기에 되돌려 쓴다(지금 들어 있는 것은 지워진다)

한 번에 16MB 를 읽으면 중간에 끊기는 일이 잦아(기록: 개발 기록(공개판에는 없음)) 1MB 씩 16번
나눠 읽고, 끊기면 그 조각만 다시 읽는다. 조각마다 esptool 이 MD5 로 읽은 내용을 확인한다.
"""
import hashlib
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proc  # noqa: E402
import vibectl  # noqa: E402

FLASH_SIZE = 0x1000000  # 16MB
CHUNK = 0x100000        # 1MB


# 읽는 속도. 기본 속도(115200)로는 1MB 에 90초가 걸린다(실측 — 16MB 면 24분).
# 빠른 속도부터 해 보고, 끊기면 그 조각만 한 단계 낮춰 다시 읽는다.
# 주의(실측): 921600 으로 15MB 를 잘 읽다가 16번째 조각에서 굽기 도구의 보조 프로그램이
# 죽으며 기기가 재시작해 내려받기 모드에서 빠져나갔다. 읽기가 실패하면 기기가 사라질 수 있다 →
# 받은 만큼은 파일에 남기고, 사람이 RST 3초를 다시 누른 뒤 --resume 으로 잇는다.
BAUDS = [921600, 460800, 230400, 115200]
RESUME_BAUDS = [230400, 115200]  # 이어 받을 때는 처음부터 느리고 안전하게


def esptool(port, *args, baud=115200, timeout=None):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "-b", str(baud),
           "--before", "no_reset", "--after", "no_reset", *args]
    # 시간 초과도 "실패한 결과"(출력은 글자)로 받는다 — 그래야 끊긴 조각만 다시 읽는 길이 돈다(proc.py).
    return proc.run_text(cmd, timeout=timeout)


def need_download_port():
    port = vibectl.find("download")
    if not port:
        sys.exit(f"내려받기 모드인 기기가 없다(지금 상태: {vibectl.state()}). "
                 "기기의 RST 버튼을 3초 눌러 초록 불이 켜지게 한다.")
    return port


def need_full_image(path):
    """path 가 통째 백업(꼭 16MB)인지 본다. 아니면 기기를 찾기 전에 여기서 끝낸다.

    이유: 기기의 대조(verify_flash)는 "파일의 길이만큼"만 견준다. 받다 만 파일도 앞부분만 같으면
          "같다"로 나와, 통째 백업이 있다고 믿고 새 펌웨어를 올리게 된다(코덱스 검토).
          되돌려 쓸 때 짧은 파일을 주면 뒷부분은 옛 내용이 남는다.
    """
    try:
        size = os.path.getsize(path)
    except OSError as e:
        sys.exit(f"파일을 읽을 수 없다: {e}")
    if size != FLASH_SIZE:
        sys.exit(f"통째 백업이 아니다: {path} 는 {size} 바이트다(있어야 할 크기는 16MB = {FLASH_SIZE} 바이트). "
                 "받다 만 파일이면 --resume 으로 이어서 받는다.")


def open_private(path, mode="wb"):
    """본인만 읽고 쓸 수 있는 파일로 연다(0600). 이미 있던 파일이면 권한을 그렇게 고친다.

    백업에는 기기에 들어 있던 것이 통째로 담긴다 — 다른 펌웨어가 적어 둔 와이파이 비밀번호·토큰까지.
    그냥 열면 컴퓨터의 평소 설정대로 남도 읽을 수 있는 파일(0644)이 된다(보안 점검).
    """
    flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if "w" in mode else 0)
    fd = os.open(path, flags, 0o600)
    os.fchmod(fd, 0o600)
    return os.fdopen(fd, mode)


def part_path(out_path):
    """받는 동안 쓰는 임시 이름. 다 받아야 진짜 이름이 된다 — 받다 만 파일이 온전한 백업처럼 보이지 않게."""
    return out_path + ".part"


def read_all(out_path):
    # 이미 있는 백업을 덮어쓰지 않는다: 예전에는 여는 순간 비워서, 중간에 실패하면 멀쩡하던 백업이
    # 받다 만 파일로 바뀌었다(독립 검토). 기기를 찾기 전에 여기서 끝낸다.
    if os.path.exists(out_path):
        sys.exit(f"{out_path} 가 이미 있다 — 덮어쓰지 않는다. 다른 이름을 주거나, 받다 만 것이면 --resume 을 쓴다.")
    # 받다 만 것(.part)이 있으면 비우지 않는다: 처음부터 다시 받으면 받아 둔 조각을 잃는다.
    if os.path.exists(part_path(out_path)):
        sys.exit(f"{part_path(out_path)} 가 있다(받다 만 것) — 지우지 않는다. 이어 받으려면 --resume 을 쓰고, "
                 "처음부터 다시 받으려면 그 파일을 직접 지운 뒤 다시 실행한다.")
    port = need_download_port()
    # 읽기 전에도 "우리 기기"인지 확인한다(MAC). 옆에 꽂힌 다른 보드만 내려받기 모드일 때 그 보드의 내용을
    # "이 기기의 원본"이라는 이름으로 받아 두면, 나중에 그것을 믿고 되돌려 쓰게 된다(보안 점검).
    vibectl.confirm(port)
    r = esptool(port, "read_mac")
    print(r.stdout.strip().splitlines()[-2:] if r.returncode == 0 else r.stdout + r.stderr)
    import time
    level = 0  # BAUDS 의 몇 번째 속도로 읽고 있는가
    t0 = time.time()
    with open_private(part_path(out_path)) as out, tempfile.TemporaryDirectory() as tmp:
        for off in range(0, FLASH_SIZE, CHUNK):
            part = os.path.join(tmp, "part.bin")
            for attempt in range(1, 9):
                if os.path.exists(part):
                    os.remove(part)
                baud = BAUDS[level]
                # 1MB 를 그 속도로 읽는 데 걸릴 시간의 세 배 + 여유를 넘기면 멈춘 것으로 본다
                r = esptool(port, "read_flash", hex(off), hex(CHUNK), part, baud=baud,
                            timeout=CHUNK * 10 / baud * 3 + 30)
                ok = r.returncode == 0 and os.path.exists(part) and os.path.getsize(part) == CHUNK
                if ok:
                    break
                print(f"  {off:#09x} 를 {baud} 로 읽지 못함({attempt}): {(r.stdout + r.stderr).strip()[-100:]!r}",
                      flush=True)
                if level < len(BAUDS) - 1:
                    level += 1
                time.sleep(1)
                port = need_download_port()
            else:
                sys.exit(f"{off:#x} 조각을 읽지 못했다")
            with open(part, "rb") as f:
                out.write(f.read())
            out.flush()
            print(f"  {off + CHUNK:#09x} / {FLASH_SIZE:#x}  {time.time() - t0:5.0f}초  ({BAUDS[level]})", flush=True)
    os.replace(part_path(out_path), out_path)  # 다 받았다: 이제 진짜 이름으로
    data = open(out_path, "rb").read()
    print(f"저장: {out_path} ({len(data)} 바이트)")
    print("sha256:", hashlib.sha256(data).hexdigest())


def resume(out_path):
    """받다 만 파일을 이어서 받는다."""
    import time
    work = part_path(out_path)
    # 이미 온전한(통째 크기) 파일은 "받다 만 것"이 아니다 → 손대지 않는다.
    # 예전에는 온전한 파일도 이어 받을 것으로 보고, 기기의 지금 내용과 다른 조각을 다시 읽어 그 파일 위에 덮었다.
    # 우리 펌웨어를 올린 뒤 원래 펌웨어의 백업 이름으로 한 번 잘못 돌리면 하나뿐인 백업이 지금 펌웨어로 바뀐다
    # (독립 검토, 시험 test_resume_never_touches_a_complete_backup). 기기를 찾기 전에 끝낸다.
    # (받다 만 임시 파일 .part 가 곁에 있어도 마찬가지다 — 끝에서 그 파일이 이 이름을 덮는다.)
    if os.path.exists(out_path) and os.path.getsize(out_path) == FLASH_SIZE:
        sys.exit(f"{out_path} 는 이미 온전한 백업이다({FLASH_SIZE} 바이트) — 이어 받을 것이 없다. 손대지 않는다. "
                 "기기의 지금 내용과 같은지 보려면 --verify, 지금 내용을 새로 받으려면 다른 이름을 준다"
                 + (f"(곁의 {work} 는 다른 때 받다 만 것이다 — 필요 없으면 직접 지운다)." if os.path.exists(work) else "."))
    port = need_download_port()
    vibectl.confirm(port)  # 이어 받을 때도 "우리 기기"인지부터(read_all 과 같은 까닭)
    # 받다 만 것은 임시 이름(.part)에 있다. 예전 방식으로 받다 만 파일(진짜 이름)도 받아 준다.
    src = work if os.path.exists(work) else out_path
    have = open(src, "rb").read() if os.path.exists(src) else b""
    parts, level, t0 = [], 0, time.time()
    with tempfile.TemporaryDirectory() as tmp:
        part = os.path.join(tmp, "part.bin")
        for off in range(0, FLASH_SIZE, CHUNK):
            old = have[off:off + CHUNK]
            if len(old) == CHUNK:
                with open(part, "wb") as f:
                    f.write(old)
                # 기기가 그 구간의 지문(MD5)을 스스로 계산해 알려 준다 — 내용을 다시 받지 않아 빠르다.
                r = esptool(port, "verify_flash", hex(off), part, timeout=120)
                if r.returncode == 0 and "verify OK" in r.stdout:
                    parts.append(old)
                    print(f"  {off:#09x} 그대로(지문 일치)", flush=True)
                    continue
                print(f"  {off:#09x} 지금 내용과 다르다 → 다시 읽는다", flush=True)
                port = need_download_port()
            for attempt in range(1, 5):
                if os.path.exists(part):
                    os.remove(part)
                baud = RESUME_BAUDS[level]
                r = esptool(port, "read_flash", hex(off), hex(CHUNK), part, baud=baud,
                            timeout=CHUNK * 10 / baud * 3 + 30)
                if r.returncode == 0 and os.path.exists(part) and os.path.getsize(part) == CHUNK:
                    break
                print(f"  {off:#09x} 를 {baud} 로 읽지 못함({attempt}): {(r.stdout + r.stderr).strip()[-100:]!r}",
                      flush=True)
                level = min(level + 1, len(RESUME_BAUDS) - 1)
                time.sleep(1)
                port = need_download_port()
            else:
                sys.exit(f"{off:#x} 조각을 읽지 못했다")
            parts.append(open(part, "rb").read())
            print(f"  {off:#09x} 읽음  {time.time() - t0:5.0f}초  ({RESUME_BAUDS[level]})", flush=True)
            # 받은 데까지를 곧바로 파일에 남긴다(뒤에서 실패해도 잃지 않게)
            with open_private(work) as f:
                f.write(b"".join(parts) + have[len(b"".join(parts)):])
    data = b"".join(parts)
    with open_private(work) as f:
        f.write(data)
    os.replace(work, out_path)  # 다 받았다: 이제 진짜 이름으로
    print(f"저장: {out_path} ({len(data)} 바이트)")
    print("sha256:", hashlib.sha256(data).hexdigest())


def verify(path):
    need_full_image(path)  # 크기부터: 덜 받은 파일이 "같다"로 나오지 않게
    port = need_download_port()
    r = esptool(port, "verify_flash", "0x0", path)  # 내용을 주고받지 않으므로 기본 속도면 된다
    print((r.stdout + r.stderr).strip()[-400:])
    sys.exit(r.returncode)


def restore(path, ask=input):
    need_full_image(path)
    port = need_download_port()
    # 16MB 를 통째로 덮어쓰는 일이다 → 이 보드가 정말 우리 기기인지(MAC) 확인한 뒤에만 쓴다.
    # 옆에 꽂힌 다른 ESP32-S3 보드만 내려받기 모드일 때 그 보드를 덮어쓰지 않게(독립 검토).
    vibectl.confirm(port)
    # 그리고 사람에게 묻는다. 이 명령은 파일이 "내가 이 기기에서 받은 백업"인지 알 길이 없다 — 남이 건넨 파일을 그대로
    # 구우면 그 펌웨어가 기기를 차지한다(키보드라면 무엇이든 친다). 지문을 보여 주니 백업할 때 찍힌 값과 견준다.
    with open(path, "rb") as f:
        data = f.read()
    digest = hashlib.sha256(data).hexdigest()
    print(f"되돌려 쓸 파일: {path}\nsha256: {digest}")
    print("이 파일이 내가 이 기기에서 backup.py 로 받은 것이 맞는가? 기기에 지금 들어 있는 것은 모두 지워진다.")
    try:
        answer = ask("맞으면 restore 라고 치고 Enter(다른 답은 그만둔다): ")
    except EOFError:
        answer = ""
    if answer.strip() != "restore":
        sys.exit("그만둔다 — 기기에는 아무것도 쓰지 않았다.")
    # 지문을 낸 바로 그 내용을 굽는다: 본인만 읽는 임시 사본에 써서 그것을 넘긴다(묻는 사이에 원래 파일이 바뀌어도 상관없게).
    with tempfile.TemporaryDirectory() as tmp:
        copy = os.path.join(tmp, "restore.bin")
        with open_private(copy) as f:
            f.write(data)
        cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "--before", "no_reset",
               "--after", "watchdog_reset", "write_flash", "0x0", copy]
        code = subprocess.run(cmd).returncode
    sys.exit(code)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--verify":
        verify(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--resume":
        resume(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--restore":
        restore(sys.argv[2])
    elif len(sys.argv) == 2 and not sys.argv[1].startswith("-"):
        read_all(sys.argv[1])
    else:
        sys.exit(__doc__)

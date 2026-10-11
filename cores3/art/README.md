# 화면 조각의 재료

`cores3/tools/gen_art.py` 가 화면의 조각 그림(`cores3/logic/art_data.cpp`)을 만들 때 쓰는 재료를 두는 곳이다.
만든 결과는 저장소에 들어 있으므로, **펌웨어를 빌드하는 데에는 이 폴더가 필요 없다.** 모습을 바꿔 다시 만들 때만 채운다.

| 파일 | 무엇 | 받는 곳 |
|---|---|---|
| `fonts/Pretendard-SemiBold.otf` | 화면의 글자·숫자 글꼴. SIL Open Font License 1.1 | https://github.com/orioncactus/pretendard (`packages/pretendard/dist/public/static/`) |
| `fonts/Pretendard-Medium.otf` | 타일의 이름(Talk · OK …)에 쓰는 굵기. 없으면 위 글꼴로 대신한다 | 위와 같다 |
| `thiings/<이름>.png` | (선택) 타일에 넣을 그림 일곱 장. **없으면 스크립트가 선 그림을 그린다** | 자기 그림, 또는 쓸 권리가 있는 그림 |
| `logo/logo.svg` | (선택) 그림이 없을 때 Talk 타일에 쓸 자기 로고. 없으면 마이크 선 그림 | 자기 그림 |

`fonts/` · `thiings/` · `logo/` 는 git 에 넣지 않는다(`.gitignore`).

## 이 저장소에 들어 있는 그림

**이 저장소의 `art_data.cpp` 는 선 그림으로 만든 것이다**(`gen_art.py --lines`, 남의 아이콘·로고 없음). 글자는 Pretendard(SIL OFL 1.1)로 그렸다.

타일에 자기 그림을 넣으려면 `thiings/`(스크립트가 찾는 폴더 이름이다)에 아래 이름의 PNG(투명 바탕, 정사각형)를 두고 다시 만든다 — **쓸 권리가 있는 그림만** 넣을 것. 그렇게 만든 `art_data.cpp` 를 다시 배포할 때는 그 그림의 조건을 따른다.

| 타일 | 파일 이름(`thiings/`) |
|---|---|
| Talk | `retro-microphone.png` |
| OK · ✕ | `check-mark.png` · `x-mark.png` |
| Back · Next | `left-arrow.png` · `right-arrow.png` |
| ▲ · ▼ | `up-arrow.png` · `arrow-down.png` |

## 다시 만들기

    python3 cores3/tools/gen_art.py --lines   # 선 그림으로 다시 만든다(아이콘이 다 있으면 --lines 없이)
    python3 cores3/tools/gen_art.py --check   # 저장소의 결과가 스크립트와 같은지 본다

그림 도구(Pillow)의 판이 다르면 글자·그림자의 끝자리가 달라져 모든 조각이 미세하게 달라진다. 저장소의 결과는 Pillow 12.2 로 만들었다 — 다른 판으로 만들려면 `--any-pil` 을 붙인다. 미리보기는 `cores3/test/host/preview.sh`.

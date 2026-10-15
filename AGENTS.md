# 저장소 작업 규칙

## 작업 시작 순서

1. 이 파일과 [개발 규칙](rules/development.md), [제품 요구사항](rules/product.md)을 읽는다.
2. 수정할 기능에 해당하는 아래 규칙을 읽고 실제 코드·테스트와 대조한다.
3. `git status --short --branch`와 `git diff`로 현재 브랜치와 기존 작업을 확인한다. 다른 PC에서 이어서 작업할 때는 원격 변경도 확인하고 작업 트리를 보존하며 동기화한다.

Git에 저장된 `AGENTS.md`와 `rules/`를 모든 PC·작업 도구의 공통 기준으로 삼는다. 개인 메모나 이전 대화로 저장소 규칙을 대체하지 않는다. 사용자의 명시적인 최신 지시가 기존 규칙을 바꾸면 관련 규칙도 함께 갱신한다. 상세 절차를 여러 파일에 복사하지 않고 원본 문서를 링크한다.

## 기능별 규칙

| 작업 영역 | 확인할 문서 |
| --- | --- |
| 빌드·검증·배포·PC 간 작업 | [development.md](rules/development.md) |
| 제품 범위·저장소·디자인 | [product.md](rules/product.md) |
| 작업 공간·탐색·탭·폰트 | [workspace-ui.md](rules/workspace-ui.md) |
| 분할 공통 입력·동기화 | [broadcast.md](rules/broadcast.md) |
| SFTP | [sftp.md](rules/sftp.md) |
| SSH 개인키 | [keys.md](rules/keys.md) |
| 명령줄 접속·기존 창 전달 | [launch-arguments.md](rules/launch-arguments.md) |
| 외부 코드·자산·라이선스 | [licensing.md](rules/licensing.md) |
| iPad | [ipad.md](rules/ipad.md) |

## 저장소 구조

- `src/`: Windows C/C++ UI, 연결·설정·인증·파일 전송.
- `patches/`: PuTTY 엔진 통합. `scripts/`: 빌드·패키징·배포 검증.
- `mac/`: macOS Swift 앱과 테스트. `ios/`: iPad 앱.
- `tests/`: 핵심·도구 테스트, `tests/ssh/`: 루프백 SSH/UI 통합 테스트.
- `assets/`, `licenses/`: 내장 자산과 라이선스.
- `docs/`: 상세 사용법·개발 안내, `docs/releases/`: 버전별 변경 및 검증 기록.
- `rules/`: 유지해야 할 작업·기능 규칙. `release/`: 버전별 배포 파일·해시·manifest.

## 구현과 검증

- C++20·Win32, C/C++ 공백 4칸·PascalCase 타입·camelCase 함수/변수를 사용한다. Swift 기준은 개발 규칙을 따른다.
- Markdown은 UTF-8, 설명적인 제목, 언어를 표시한 코드 블록을 사용한다. 새 문서 이름은 소문자와 하이픈을 사용한다. 줄바꿈은 `.gitattributes`를 따른다.
- 빌드 전제 조건·빠른 실행 명령은 [README](README.md#개발-환경), 상세 명령은 [개발 안내](docs/development.md)를 따른다.
- 기본 작업 범위는 Windows 로컬 빌드·검증이다. macOS·iPad 빌드와 GitHub Actions 재개는 명시적인 요청을 따른다.
- 실행 동작 변경에는 회귀 테스트를 추가한다. 실제 사용자 데이터 대신 `build/` 아래 격리 데이터와 루프백 서버를 사용한다.
- Linux 정적 검사·교차 컴파일과 Windows 실행 검증을 구분한다. 실행하지 못한 검사는 이유와 함께 기록하고 통과로 표시하지 않는다.
- 문서 변경은 링크·경로·명령을 확인하고 `git diff --check`를 실행한다. README에 버전별 작업 기록을 누적하지 않는다.

## 커밋과 배포

- 의미 있는 작업 단위로 나누고 간결한 명령형 커밋 제목을 쓴다. 커밋 전 변경 내용과 검증 결과를 확인한다.
- 배포는 [개발 규칙](rules/development.md)의 버전·출처·검증 절차를 따른다. 이미 공개한 버전별 파일은 덮어쓰지 않는다.
- PR에는 문제, 변경 후 동작, 검증 결과와 미실행 항목을 적고 UI 변경에는 스크린샷을 포함한다.

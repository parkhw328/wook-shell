# wShell 프로젝트 지도

wShell은 PuTTY 엔진을 사용하는 Windows 단일 EXE 터미널 앱이다. 저장 호스트, SSH·로컬 터미널, 탭·분할, 키 관리와 SFTP를 제공한다. 현재 작업 범위는 **Windows 로컬 빌드·검증**이며 macOS·iPad 빌드와 GitHub Actions는 명시적인 재개 요청을 따른다.

## 문서 구분

| 구분 | 담는 내용 | 시작 문서 |
| --- | --- | --- |
| [rules/](rules/) — 규칙 | 코딩·검증·배포·PC 간 작업·라이선스 준수 방법 | [개발 규칙](rules/development.md), [라이선스 규칙](rules/licensing.md) |
| [requirements/](requirements/) — 요구사항 | 제품이 제공할 동작, 상태별 제약, 데이터 보호와 인수 기준 | [제품 요구사항](requirements/product.md) |
| [docs/](../docs/) — 안내 | 실제 사용법, 빌드 명령, 기능 설명과 검증 기록 | [사용법](../docs/usage.md), [개발 안내](../docs/development.md) |

작업은 [AGENTS.md](../AGENTS.md) → [개발 규칙](rules/development.md) → [제품 요구사항](requirements/product.md) → 해당 기능 요구사항 순으로 확인한다. 요구사항은 유지할 계약이며, 현재 배포의 검증 완료 여부는 [다운로드 목록](../release/README.md)과 [릴리스 노트](../docs/releases/)에서 확인한다.

## 기능과 구현·검증 연결

| 기능 | 요구사항 | 주요 구현 | 주요 검증 |
| --- | --- | --- | --- |
| 제품 범위·디자인·저장소 | [product.md](requirements/product.md) | [src/](../src/), [assets/](../assets/) | [핵심 검사](../tests/core_test.cpp), [패키지 검사](../scripts/verify-package.py) |
| 메뉴바·탐색·폴더·탭·글꼴·IME | [workspace-ui.md](requirements/workspace-ui.md) | [main.cpp](../src/main.cpp), [navigation.hpp](../src/navigation.hpp), [menu_bar.cpp](../src/menu_bar.cpp), [theme.cpp](../src/theme.cpp) | [작업 공간](../tests/workspace_smoke.inc), [탐색 너비·툴팁](../tests/ssh/navigation-integration.cjs), [키 입력·포커스](../tests/focus_smoke.inc), [연결 목록](../tests/ssh/connections-integration.cjs) |
| 분할 공통 입력·동기화 | [broadcast.md](requirements/broadcast.md) | [broadcast.h](../src/broadcast.h), [엔진 통합](../patches/wook-window.h) | [UI·SSH 통합](../tests/ssh/integration.cjs), [포커스 통합](../tests/ssh/focus-integration.cjs) |
| SFTP·전송·취소 | [sftp.md](requirements/sftp.md) | [sftp.cpp](../src/sftp.cpp), [sftp_view.cpp](../src/sftp_view.cpp), [공통 코덱](../mac/Sources/CSFTP/) | [SFTP 통합](../tests/ssh/sftp-integration.cjs), [취소 통합](../tests/ssh/sftp-cancel-integration.cjs) |
| SSH 공개키 선택·개인키 등록 | [keys.md](requirements/keys.md) | [keys.c](../src/keys.c), [key_import.cpp](../src/key_import.cpp) | [공개키 검사](../tests/test_public_key.py), [키 등록 통합](../tests/ssh/key-import-integration.cjs) |
| 실행 인자·기존 창 전달 | [launch-arguments.md](requirements/launch-arguments.md) | [launch.cpp](../src/launch.cpp), [launch_relay.cpp](../src/launch_relay.cpp) | [실행 인자](../tests/launch_test.inc), [기존 창 전달](../tests/launch_relay_test.cpp) |
| iPad | [ipad.md](requirements/ipad.md) | [ios/](../ios/) | [검증·설치 안내](../docs/ipad.md), 현재 빌드 중단 |

## 변경을 기록할 위치

- 사용자에게 제공할 동작과 인수 기준이 바뀌면 `requirements/`의 해당 원본을 갱신한다.
- 작업 방식이나 배포 정책이 바뀌면 `rules/`의 원본을 갱신한다.
- 사용 절차는 `docs/`, 버전별 변경·실행한 검사·미실행 검사는 `docs/releases/`에 둔다. 개발 중인 기능은 배포 여부를 구분해 안내한다.
- 파일을 이동하면 `AGENTS.md`, 이 지도와 모든 상대 링크를 함께 갱신한다. 같은 규칙이나 요구사항을 여러 파일에 복사하지 않는다.

# 개발 규칙

- 기존 루트 `AGENTS.md`는 유지하며, 갱신되는 개발 규칙은 이 폴더에 기록한다.
- C++20과 Win32 API를 사용한다. C/C++ 들여쓰기는 공백 4칸, 타입은 PascalCase, 함수와 변수는 camelCase다.
- 소스는 `src/`, 테스트는 `tests/`, 빌드 자동화는 `scripts/`, 패치는 `patches/`, 재배포 자산은 `assets/`와 `licenses/`에 둔다.
- PuTTY 코어의 암호화와 터미널 해석은 재구현하지 않는다. 변경은 포터블 저장 및 창 통합 경계에 한정한다.
- 키 관리는 직접 라이브러리를 호출한다. RSA 생성은 Windows CNG를 사용하고 PuTTY의 암호 연산 링크 보호를 제거하지 않는다.
- 텍스트 크기는 `src/ui.hpp`의 `TextSize`를 사용하고 굵기는 실제 내장 Regular/Bold만 사용한다.
- 외부 소스·도구는 버전과 SHA-256을 고정하고 검증한다. 빌드 산출물과 개발 도구는 커밋하지 않는다.
- 사용자 입력은 검증하고, 프로세스 인수는 Windows 규칙에 따라 인용한다. 셸을 경유해 실행하지 않는다.
- 영속 설정은 파일 잠금과 원자적 교체로 보호한다. 저장 실패를 사용자에게 알린다.
- 연결 수명주기, 탭 모델, 주소 파싱, 설정 저장과 포터블 저장을 테스트한다.
- 의미 있는 구현 단위로 명령형 커밋 메시지를 작성하고 검증 후 `origin`에 푸시한다.

## 빌드와 회귀 검증

- 최초 빌드: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Bootstrap`.
- 이후 빌드: `scripts/build.ps1`; CTest의 `core-tests`와 배포 ZIP 생성까지 수행한다.
- SSH/UI: `npm --prefix tests/ssh ci --ignore-scripts --omit=optional` 후 `node tests/ssh/integration.cjs`.
- 테스트는 임시 루프백 SSH 서버와 `build/` 아래 격리 데이터만 사용한다. 테스트 창은 자동 종료한다.
- 실행파일 이름은 `wShell.exe`, ZIP 이름은 `wshell-<version>-win-x64.zip`이다.
- UI 변경은 생성된 `build/ssh-test-*/standalone/ui-*.bmp` 화면을 확인한다. 암호·실제 서버 정보를 스크린샷에 포함하지 않는다.
- 통합 테스트는 EXE만 복사한 빈 폴더에서 시작하고, 생성한 키의 실제 SSH 서명을 별도 서버 구현으로 검증한다.
- 아이콘 원본은 `assets/branding/wshell-icon.png`, ICO 재생성은 `scripts/make-icon.ps1`이다.
- 공개 PR에는 문제/변경 동작, 관련 이슈, 실행한 검증과 UI 변경 스크린샷을 적는다.

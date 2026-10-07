# 개발 규칙

- 기존 루트 `AGENTS.md`는 유지하며, 갱신되는 개발 규칙은 이 폴더에 기록한다.
- C++20과 Win32 API를 사용한다. C/C++ 들여쓰기는 공백 4칸, 타입은 PascalCase, 함수와 변수는 camelCase다.
- 소스는 `src/`, 테스트는 `tests/`, 빌드 자동화는 `scripts/`, 패치는 `patches/`, 재배포 자산은 `assets/`와 `licenses/`에 둔다.
- PuTTY 코어의 암호화와 터미널 해석은 재구현하지 않는다. 변경은 저장소, 창 통합, 설정 화면과 자격 증명 공급 경계에 한정한다.
- 저장 비밀번호는 `src/credentials.cpp`에서 DPAPI 사용자 범위로 암호화한다. 평문 인수·환경변수·임시 파일을 사용하지 않으며 복호화 버퍼는 사용 후 지운다.
- 비밀번호 공급은 SSH 엔진이 지정한 인증 대상 메타데이터만 사용한다. 서버가 보낸 프롬프트 문자열로 비밀번호나 OTP를 추측하지 않는다. 백업에서는 암호문과 연결 정보를 제외한다.
- 연결 설정의 추상 모델과 핸들러는 유지한다. `patches/wshell-config.h`가 모델을 `src/settings_ui.cpp`의 테마 화면에 연결한다.
- 키 관리는 직접 라이브러리를 호출한다. RSA 생성은 Windows CNG를 사용하고 PuTTY의 암호 연산 링크 보호를 제거하지 않는다.
- 텍스트 크기는 `src/ui.hpp`의 `TextSize`를 사용하고 굵기는 실제 내장 Regular/Bold만 사용한다.
- 외부 소스·도구는 버전과 SHA-256을 고정하고 검증한다. 빌드 산출물과 개발 도구는 커밋하지 않는다.
- 사용자 입력은 검증하고, 프로세스 인수는 Windows 규칙에 따라 인용한다. 셸을 경유해 실행하지 않는다.
- 영속 설정은 파일 잠금과 원자적 교체로 보호한다. 저장 실패를 사용자에게 알린다.
- 연결 수명주기, 탭 모델, 주소 파싱, 설정 저장·이전·백업과 설정 UI의 적용·취소를 테스트한다.
- 기본 저장소는 `%LOCALAPPDATA%\wShell`이다. 테스트는 `WOOK_DATA_DIR`로 격리하며, 이전 `data/` 가져오기 중 환경변수 전환은 작업 스레드와 자식 프로세스 생성 전에만 수행한다.
- 의미 있는 구현 단위로 명령형 커밋 메시지를 작성하고 검증 후 `origin`에 푸시한다.

## 빌드와 회귀 검증

- 최초 빌드: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Bootstrap`.
- 이후 빌드: `scripts/build.ps1`; CTest의 `core-tests`와 배포 ZIP 생성까지 수행한다.
- SSH/UI: `npm --prefix tests/ssh ci --ignore-scripts --omit=optional` 후 `node tests/ssh/integration.cjs`.
- 테스트는 임시 루프백 SSH 서버와 `build/` 아래 격리 데이터만 사용한다. 테스트 창은 자동 종료한다.
- 버전은 루트 `VERSION`에서 관리한다. `dist/<version>/windows-x64/`와 `dist/<version>/macos-universal/`로 배포하며 `scripts/index-dist.py`로 해시 manifest를 생성한다.
- UI 변경은 생성된 `build/ssh-test-*/standalone/ui-*.bmp` 화면을 확인한다. 암호·실제 서버 정보를 스크린샷에 포함하지 않는다.
- 통합 테스트는 EXE만 복사한 빈 폴더에서 시작하고, 생성한 키의 실제 SSH 서명을 별도 서버 구현으로 검증한다.
- 아이콘 원본은 `assets/branding/wshell-icon.png`, ICO 재생성은 `scripts/make-icon.ps1`이다.
- 공개 PR에는 문제/변경 동작, 관련 이슈, 실행한 검증과 UI 변경 스크린샷을 적는다.

## macOS와 로컬 터미널

- macOS 코드는 `mac/`의 Swift 5 언어 모드·Swift 6 도구 체인을 사용하며 공백 4칸, PascalCase 타입과 camelCase 함수를 따른다.
- AppKit·SwiftTerm을 사용한다. 외부 Swift 패키지는 정확한 커밋과 `Package.resolved`로 고정한다.
- `python3 scripts/build-mac.py`는 핵심 테스트, arm64/x86_64 빌드, Universal 앱 패키징을 수행한다. macOS 실행 검증은 macOS CI에서 수행한다.
- `node tests/ssh/mac-integration.cjs`는 앱의 로컬 PTY·SSH 입출력·공개키 서명·Keychain 비밀번호 공급을 실제 루프백 서버로 검증한다.
- macOS 비밀번호는 동기화하지 않는 현재 사용자 Keychain에 저장한다. SSH password 인증만 자동 공급하며 OTP·키 암호·호스트 신뢰 확인은 수동이다.
- SwiftTerm의 터미널 명령을 통한 클립보드 읽기/쓰기는 허용하지 않는다. 사용자가 실행한 키보드 복사/붙여넣기는 유지한다.
- Windows 로컬 셸은 PuTTY ConPTY 백엔드, macOS는 PTY와 로그인 셸을 사용한다. 셸/SSH 프로세스는 탭 종료 시 정리한다.
- macOS의 고급 연결 기능은 Windows와 범위가 다르다. README 기능표와 실제 검증 범위를 일치시킨다.

## 분할 보기

- 분할 영역은 탭의 안정적인 객체/UUID로 추적한다. 탭 재정렬·닫기·포커스 이동에 따라 다른 연결을 잘못 조작하지 않도록 검증한다.
- Windows 엔진의 포커스 알림은 실제 소유한 터미널 HWND만 받는다. macOS는 해당 작업 공간 창의 클릭만 추적한다.
- 2~4개 네이티브 터미널의 비중첩 배치·독립 입력·단일 보기 복귀를 UI 통합 테스트로 검증한다. 테스트 파일은 격리된 `build/` 아래에만 쓴다.

## 한글·IME 입력

- Windows 조합 미리보기는 `src/ime.c`에서 실제 터미널 커서와 셀 크기에 맞춰 그린다. 조합창은 포커스를 가져가지 않으며 OS 후보창을 유지한다.
- 조합 문자열은 화면 표시 전용이다. IME가 확정한 문자열만 기존 입력 경로로 보내고, 취소·종료 이벤트에서 중복 전송하지 않는다. UTF-16 서로게이트 쌍을 나누지 않는다.
- macOS는 AppKit의 attributed/plain 확정 문자열과 marked selection을 모두 처리한다. 입력 내용을 임의로 정규화하거나 서버의 로케일·키맵을 변경하지 않는다.
- `ime-tests`로 조합·취소·확정·한글 폭·리사이즈를 검증하고, 루프백 SSH에서 유니코드 문자열이 한 번만 전달되는지 확인한다. 합성 이벤트 검증과 실제 OS 입력기 수동 검증을 구분해 기록한다.

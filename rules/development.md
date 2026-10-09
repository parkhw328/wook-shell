# 개발 규칙

## 현재 작업 범위 — Windows 집중

- 2026-10-07 사용자 요청에 따라 당분간 Windows만 빌드·테스트·패키징한다. macOS와 iPad 빌드는 사용자가 해당 플랫폼의 빌드 또는 재개를 명시적으로 요청할 때까지 중단한다.
- 일반적인 "빌드", "전체 점검", "배포 파일 갱신" 요청은 Windows 범위로 해석한다. macOS·iPad의 로컬 빌드, CI 수동 실행, 서명·패키징을 함께 수행하지 않는다.
- 2026-10-08 GitHub Actions 포함 사용량 소진으로 Windows도 로컬 빌드·검증을 기본으로 한다. `windows.yml`과 `ipad.yml`은 수동 실행 전용이며 push·PR에서 자동 빌드를 실행하지 않는다. 한도가 초기화되더라도 자동으로 CI를 재개하지 않고 사용자의 재개 요청을 따른다.
- GitHub-hosted CI는 사용자가 실행을 명시적으로 요청했을 때만 사용한다. macOS는 추가로 `windows.yml`의 `build_macos: true`를, iPad는 `ipad.yml`을 선택해야 하며 해당 플랫폼 빌드 중단 규칙도 유지한다.
- 기존 macOS·iPad 소스와 빌드 절차는 재개를 위해 보존한다. Windows 검증 결과를 다른 플랫폼의 검증 완료로 표시하지 않으며, 이전 산출물을 최신 Windows 변경까지 반영한 것으로 안내하지 않는다.
- 아래 macOS 및 [iPad 규칙](ipad.md)의 빌드·검증 절차는 해당 플랫폼을 명시적으로 요청받았을 때 적용한다.

## 공통 개발 원칙

- 기존 루트 `AGENTS.md`는 유지하며, 갱신되는 개발 규칙은 이 폴더에 기록한다.
- C++20과 Win32 API를 사용한다. C/C++ 들여쓰기는 공백 4칸, 타입은 PascalCase, 함수와 변수는 camelCase다.
- 소스는 `src/`, 테스트는 `tests/`, 빌드 자동화는 `scripts/`, 패치는 `patches/`, 재배포 자산은 `assets/`와 `licenses/`에 둔다.
- PuTTY 코어의 암호화와 터미널 해석은 재구현하지 않는다. 변경은 저장소, 창 통합, 설정 화면과 자격 증명 공급 경계에 한정한다.
- 저장 비밀번호는 `src/credentials.cpp`에서 DPAPI 사용자 범위로 암호화한다. 자식 프로세스에 평문 인수·환경변수·임시 파일로 전달하지 않으며 복호화 버퍼는 사용 후 지운다. 사용자가 직접 지정한 실행 인자 `-pw`/`--password`는 입력 경계에서만 허용하며 노출 범위와 `--password-stdin` 대안을 문서화한다. 상세 동작은 [실행 인자 규칙](launch-arguments.md)을 따른다.
- 비밀번호 공급은 SSH 엔진이 지정한 인증 대상 메타데이터만 사용한다. 서버가 보낸 프롬프트 문자열로 비밀번호나 OTP를 추측하지 않는다. 백업에서는 암호문과 연결 정보를 제외한다.
- 연결 설정의 추상 모델과 핸들러는 유지한다. `patches/wshell-config.h`가 모델을 `src/settings_ui.cpp`의 테마 화면에 연결한다.
- 키 관리는 직접 라이브러리를 호출한다. RSA 생성은 Windows CNG를 사용하고 PuTTY의 암호 연산 링크 보호를 제거하지 않는다.
- Windows 키 가져오기·등록·재사용은 [개인키 등록 규칙](keys.md)을 따른다.
- 텍스트 크기는 `src/ui.hpp`의 `TextSize`를 사용하고 굵기는 실제 내장 Regular/Bold만 사용한다.
- 탐색창·상단 메뉴·검색·폰트를 변경할 때는 [작업 공간 UI 규칙](workspace-ui.md)을 적용한다. 폰트 캐시 생성 전부터 `WOOK_DATA_DIR`를 격리하고, 자식 터미널도 같은 캐시를 사용한다.
- 외부 소스·도구는 버전과 SHA-256을 고정하고 검증한다. 개발 도구·중간 빌드·캐시는 커밋하지 않는다. 2026-10-08 사용자 요청에 따라 검증한 버전별 `release` 배포 산출물은 Git에 함께 보관한다.
- 사용자 입력은 검증하고, 프로세스 인수는 Windows 규칙에 따라 인용한다. 셸을 경유해 실행하지 않는다.
- 영속 설정은 파일 잠금과 원자적 교체로 보호한다. 저장 실패를 사용자에게 알린다.
- 연결 수명주기, 탭 모델, 주소 파싱, 설정 저장·이전·백업과 설정 UI의 적용·취소를 테스트한다.
- 기본 저장소는 `%LOCALAPPDATA%\wShell`이다. 테스트는 `WOOK_DATA_DIR`로 격리하며, 이전 `data/` 가져오기 중 환경변수 전환은 작업 스레드와 자식 프로세스 생성 전에만 수행한다.
- 의미 있는 구현 단위로 명령형 커밋 메시지를 작성하고 검증 후 `origin`에 푸시한다.

## 빌드와 회귀 검증

- Linux x86_64 교차 빌드: `python3 scripts/build-windows-linux.py --bootstrap`, 이후 `python3 scripts/build-windows-linux.py`. 도구는 `.tools/`, 중간 빌드는 `build/windows-linux/`, 작업 중 EXE·ZIP·해시는 `build/packages/<VERSION>/windows-x64/`에 둔다. 배포 요청 시 소스를 커밋하고 `python3 scripts/build-windows-linux.py --dist`로 새 버전을 `release/<VERSION>/windows-x64/`에 모은다.
- Linux에서는 PE·ZIP·내장 리소스를 정적으로 검증하고 Windows 테스트 EXE를 컴파일한다. Windows 실행·SSH·UI·IME·DPAPI 회귀 검증은 실행하지 않으며 통과로 표시하지 않는다. Windows와 Linux 빌드는 같은 checkout에서 동시에 실행하지 않는다. 2026-10-08 사용자 요청에 따라 Windows 실기 검증 전의 교차 빌드도 `release/`에서 제공하되, manifest와 다운로드 안내에 Linux 정적 검사 통과·Windows/NGS 실행 미검증을 명시한다. 이는 이전의 실기 검증 전 배포 금지 규칙을 대체한다.
- Python 빌드 도구 회귀 검증: `python3 -m unittest discover -s tests -p 'test_*.py'`. Linux 호스트 런타임 호환 설정은 README의 교차 빌드 절차를 따른다.
- 최초 빌드: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Bootstrap`.
- 이후 빌드: `scripts/build.ps1`; CTest의 `core-tests`와 배포 ZIP 생성까지 수행한다.
- SSH/UI: `npm --prefix tests/ssh ci --ignore-scripts --omit=optional` 후 `node tests/ssh/integration.cjs`.
- 폰트 캐시: `node tests/ssh/font-integration.cjs`. 폰트 선택·탐색 UI와 실제 분할 입력은 SSH/UI 테스트에 포함한다.
- 터미널 마우스·스크롤: `node tests/ssh/mouse-integration.cjs`, `node tests/ssh/mouse-integration.cjs --application-mouse`, `node tests/ssh/scroll-integration.cjs`, `node tests/ssh/scroll-integration.cjs --follow-output`. 실제 루프백 SSH·클립보드·스크롤바와 Win32 메시지를 사용하며 OS의 실제 포커스 라우팅 검사와 구분한다.
- 실행 인자: `node tests/ssh/launch-integration.cjs`. 배포 EXE로 실제 루프백 SSH 인증, 저장 호스트 재사용, UTF-8 파이프, 일회용 암호 정리와 자식 명령줄을 검증한다.
- 개인키 등록: `node tests/ssh/key-import-integration.cjs`. 외부 형식 가져오기, 등록 파일 보호와 배포 EXE의 실제 키 인증·재사용을 검증한다.
- 테스트는 임시 루프백 SSH 서버와 `build/` 아래 격리 데이터만 사용한다. 테스트 창은 자동 종료한다.
- 배포 산출물은 플랫폼별 기존 `release` 경로로 일원화하고 해시·manifest·다운로드 목록·설명서를 함께 갱신해 커밋·푸시한다. 실행파일과 ZIP 내부 이름은 항상 `wShell.exe`이며 NGS용 별도 이름의 파일은 제공하지 않는다. NGS 환경의 수동 이름 변경은 README에만 안내한다. Linux 배포 manifest는 `--cross-built`로 생성하고 재검증은 `--catalog-only`로 기존 출처와 검증 상태를 보존한다.
- 데스크톱 버전은 루트 `VERSION`에서 관리한다. 현재 배포 대상은 `release/<version>/windows-x64/`이며 `scripts/index-dist.py`로 해시 manifest를 생성한다. `release/<version>/macos-universal/`은 macOS 빌드를 명시적으로 요청받았을 때만 갱신한다.
- 새 Windows 배포는 `VERSION`을 올리고 소스 변경을 커밋한 뒤 `scripts/build.ps1`과 위의 로컬 회귀 검증을 실행한다. 검증한 EXE·ZIP·SHA-256·manifest를 별도 배포 커밋으로 푸시한다. 소스 커밋·푸시와 `release` 파일 공유에 GitHub Actions 실행은 필요하지 않다.
- 로컬 배포 manifest는 실제 `sourceCommit`과 `sourceDirty: false`를 확인한다. 로컬 빌드에는 `workflow`가 없으며 CI 통과로 표시하지 않는다. 명시적으로 요청받은 CI 배포는 통과한 산출물을 내려받고 `python scripts/index-dist.py --catalog-only`로 검증·목록만 갱신해 원래 소스 커밋과 CI 링크를 보존한다.
- 실제 키보드·마우스를 사용하는 UI 검사는 잠기지 않은 대화형 Windows 데스크톱에서 실행한다. 포커스 확보 실패 등 환경 문제를 통과로 바꾸거나 검사에서 제외하지 않는다. 미완료 검증은 명확히 기록하며 CI 한도 소진을 빌드 자체의 불가능으로 취급하지 않는다.
- 이미 공개한 버전의 파일은 덮어쓰지 않는다. 수정 배포에는 `VERSION`을 올린다. `release/README.md`에서 버전별 다운로드를 제공하며, 배포 파일만 변경한 푸시는 빌드를 다시 실행하지 않는다. CI는 현재 `VERSION` 폴더만 산출물로 업로드한다.
- `release`에는 허용한 배포 파일만 추가한다. `data`, 실제 암호·개인키, 로그·캐시를 포함하지 않는다. Windows ZIP의 유일한 파일은 함께 배포하는 EXE와 바이트가 같아야 한다. 기존 macOS·iPad 산출물 보관은 해당 플랫폼의 새 빌드나 최신 기능 검증을 뜻하지 않는다.
- 배포 검증 스크립트의 손상 해시·다른 EXE·키 파일 차단과 manifest 보존은 `python -m unittest discover -s tests -p test_release.py`로 검사한다.
- UI 변경은 생성된 `build/ssh-test-*/standalone/ui-*.bmp` 화면을 확인한다. 암호·실제 서버 정보를 스크린샷에 포함하지 않는다.
- 통합 테스트는 EXE만 복사한 빈 폴더에서 시작하고, 생성한 키의 실제 SSH 서명을 별도 서버 구현으로 검증한다.
- 아이콘 원본은 `assets/branding/wshell-icon.png`, ICO 재생성은 `scripts/make-icon.ps1`이다.
- 공개 PR에는 문제/변경 동작, 관련 이슈, 실행한 검증과 UI 변경 스크린샷을 적는다.

## macOS와 로컬 터미널

- macOS 빌드·검증은 현재 중단 상태이며 위의 Windows 집중 규칙을 우선 적용한다.
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
- Windows 활성 영역은 호스트 색상과 독립된 주황색 테두리와 ACTIVE 텍스트로 표시한다. 확대/복귀는 기존 영역 객체를 보존하며 입력 동기화를 해제한다. Files는 선택적 도구 모음의 맨 왼쪽에 두어 로컬 셸에서 중간 공백을 만들지 않는다.
- Windows 키보드 포커스는 실제 전경 입력 큐로 검증한다. 지연된 엔진 알림만으로 활성 영역을 바꾸지 않는다. 분할 선택·확대/복귀와 터미널 본문 클릭은 터미널로 포커스를 돌려주며, 명령창 편집 중에는 포커스를 빼앗지 않는다. 실제 입력 영역만 커서·테두리가 깜빡이고 명령 전송 대상은 SELECTED로 구별한다.
- 포커스 회귀 테스트는 `tests/focus_smoke.inc`에서 `SendInput`으로 실제 셸 입력을 확인하고 커서 셀·테두리 픽셀 변화를 검사한다. 특정 HWND에 직접 보낸 WM_CHAR만으로 포커스 전환을 검증하지 않는다.
- 2~4개 네이티브 터미널의 비중첩 배치·독립 입력·단일 보기 복귀를 UI 통합 테스트로 검증한다. 테스트 파일은 격리된 `build/` 아래에만 쓴다.

## 한글·IME 입력

- Windows 조합 미리보기는 `src/ime.c`에서 실제 터미널 커서와 셀 크기에 맞춰 그린다. 조합창은 포커스를 가져가지 않으며 OS 후보창을 유지한다.
- 조합 문자열은 화면 표시 전용이다. IME가 확정한 문자열만 기존 입력 경로로 보내고, 취소·종료 이벤트에서 중복 전송하지 않는다. UTF-16 서로게이트 쌍을 나누지 않는다.
- macOS는 AppKit의 attributed/plain 확정 문자열과 marked selection을 모두 처리한다. 입력 내용을 임의로 정규화하거나 서버의 로케일·키맵을 변경하지 않는다.
- `ime-tests`로 조합·취소·확정·한글 폭·리사이즈를 검증하고, 루프백 SSH에서 유니코드 문자열이 한 번만 전달되는지 확인한다. 합성 이벤트 검증과 실제 OS 입력기 수동 검증을 구분해 기록한다.

# 빌드 및 검증 상세 안내

모든 명령은 저장소 루트에서 실행합니다. 작업 범위와 배포 정책은 [개발 규칙](../rules/development.md)을 따릅니다.

## 빠른 개발 빌드

**작업 전 [AGENTS.md](../AGENTS.md) → [개발 규칙](../rules/development.md) → 해당 기능 규칙을 읽습니다.** PC가 바뀌어도 Git에 저장된 규칙을 공통 기준으로 사용합니다. 현재는 Windows 로컬 빌드·검증이 기본이며, macOS·iPad 빌드와 GitHub Actions 실행은 명시적인 요청이 있을 때만 수행합니다.

모든 명령은 저장소 루트에서 실행합니다. Windows는 Python 3.10 이상·PowerShell이 필요하며, 최초 명령이 고정된 빌드 도구와 소스를 다운로드합니다.

```powershell
# 최초 개발 빌드 (기존 배포 파일을 갱신하지 않음)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Bootstrap -EngineOnly
# 이후 개발 빌드
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -EngineOnly
# 실행
.\build\native\wShell.exe
# 핵심 테스트 (-EngineOnly는 테스트를 실행하지 않음)
.tools\cmake-4.4.4-windows-x86_64\bin\ctest.exe --test-dir build/native/workspace --output-on-failure
```

[빌드 문제 해결](build-troubleshooting.md)

## Linux에서 Windows 교차 빌드

Linux x86_64에서도 Windows EXE와 ZIP을 만들 수 있습니다. Python 3.12 이상과 인터넷 연결이 필요하며, 컴파일러 실행에는 `GLIBCXX_3.4.30`을 제공하는 호환 libstdc++가 필요합니다. 현재 Rocky Linux 9 / glibc 2.34 환경에서 아래 호스트 런타임 설정으로 검증했습니다. 아래 명령은 고정 버전·SHA-256으로 검증한 LLVM-MinGW, CMake, Ninja와 PuTTY 소스를 `.tools/`, `.deps/`에 준비합니다. 시스템 전역에 컴파일러를 설치하지 않습니다.

```sh
python3 scripts/build-windows-linux.py --bootstrap
# 이후 변경 빌드 (다운로드 생략)
python3 scripts/build-windows-linux.py
# 빌드 병렬 작업 수 제한
python3 scripts/build-windows-linux.py --jobs 4
```

작업 중 EXE·ZIP·SHA-256은 `build/packages/<VERSION>/windows-x64/`에 생성됩니다. 배포 요청 시에는 소스와 `VERSION` 변경을 먼저 커밋하고 `python3 scripts/build-windows-linux.py --dist`를 실행합니다. 새 버전의 `release/<VERSION>/windows-x64/`에 같은 파일을 모으고, manifest와 다운로드 목록에 Linux 정적 검사 통과·Windows/NGS 실행 미검증 상태를 기록합니다. 이후 `release/` 변경을 커밋·푸시합니다. 이미 존재하는 버전 폴더는 `--dist`가 거부하므로 수정 배포에는 버전을 올리세요. 기존 `release/` 배포 파일과 manifest를 덮어쓰지 않습니다. 중간 빌드는 `build/windows-linux/`이며 Windows 테스트 실행파일도 함께 컴파일합니다. EXE의 x64 형식, DLL 의존성, ZIP 무결성, 내장 폰트·라이선스·아이콘은 Linux에서 정적으로 검사합니다.

**이 명령은 Windows 실행 테스트를 수행하지 않습니다.** SSH·UI·IME·DPAPI 등의 실제 동작 검증은 Windows에서 별도로 수행해야 합니다. Linux 빌드 성공을 Windows 회귀 테스트 통과나 새 버전 배포 완료로 취급하지 않습니다. Windows와 Linux 빌드는 생성 리소스와 PuTTY 소스를 공유하므로 같은 checkout에서 동시에 실행하지 마세요.

현재 Rocky Linux 9 환경에서는 기본 libstdc++에 `GLIBCXX_3.4.30`이 없어, 기존 `/root/miniconda3/lib/libstdc++.so.6`를 `.tools/linux-host-libs/libstdc++.so.6`에 심볼릭 링크로 연결했습니다. 빌드 스크립트는 이 선택적 디렉터리를 자식 빌드 프로세스의 `LD_LIBRARY_PATH`에 추가합니다. Windows EXE에 이 Linux 라이브러리를 포함하지 않습니다. 다른 PC에서 같은 오류가 발생하면 그 PC의 호환 libstdc++를 해당 디렉터리에 연결하세요. 현재 서버의 경로를 다른 PC에 그대로 적용하지 마세요.

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

위 명령은 배포 무결성 검사와 PE 리소스 판독기의 정상·손상 입력 회귀 테스트를 실행합니다.

## Windows에서 빌드와 실행 검증

빌드 환경은 Windows x64, Python 3.10 이상, PowerShell입니다. C++ 컴파일러·CMake·Ninja·PuTTY 소스는 다음 명령이 저장소 내부로 다운로드하고 SHA-256을 확인합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Bootstrap
# 이후 변경 빌드, 핵심 테스트, 배포 ZIP 생성
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

이 명령은 현재 PC에서 실행하며 GitHub Actions 사용 시간을 소비하지 않습니다. 결과는 `release/<VERSION>/windows-x64/`에 생성합니다. 새 버전 배포 시 `VERSION`을 올리고 소스를 먼저 커밋한 뒤 빌드·검증하여, 기존 배포 파일을 덮어쓰지 않고 새 버전 폴더를 커밋·푸시합니다. GitHub-hosted CI는 현재 수동 실행 전용입니다.

실제 SSH 및 네이티브 UI 통합 테스트에는 개발용 Node.js 22 이상이 추가로 필요합니다.
테스트가 자체 창을 잠시 열고 루프백 서버에 연결합니다. 외부 서버·사용자 키·실제 접속 데이터는 사용하지 않습니다.

```powershell
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/font-integration.cjs
node tests/ssh/launch-integration.cjs
node tests/ssh/key-import-integration.cjs
node tests/ssh/mouse-integration.cjs
node tests/ssh/mouse-integration.cjs --application-mouse
node tests/ssh/mouse-integration.cjs --alternate-screen
node tests/ssh/scroll-integration.cjs
node tests/ssh/scroll-integration.cjs --follow-output
node tests/ssh/scroll-integration.cjs --block-mouse
node tests/ssh/scroll-integration.cjs --no-mouse
node tests/ssh/integration.cjs
node tests/ssh/sftp-integration.cjs
node tests/ssh/sftp-error-integration.cjs # 권한 오류 후 조작·재시도·F5 재연결
node tests/ssh/sftp-auth-integration.cjs # 실제 OTP 입력·Enter/Esc·창 포커스 복귀
node tests/ssh/scrollbar-theme-integration.cjs # 네비게이션·목록·텍스트·드롭다운 스크롤바 색상/동작
node tests/ssh/sftp-benchmark.cjs # 업로드·다운로드 지연 조건 비교 및 바이트 일치
python -m unittest discover -s tests -p test_release.py
python scripts/index-dist.py --catalog-only # 배포 해시·manifest 확인과 다운로드 목록 갱신
git diff --check
```

분할 포커스만 빠르게 재검증하려면 빌드 후 `node tests/ssh/focus-integration.cjs`를 실행합니다. 전체 UI 통합 테스트에도 같은 검증이 포함됩니다. 이 테스트는 자체 창을 전경으로 가져와 실제 마우스·키보드 입력, 명령창에서의 포커스 복귀, 커서·테두리 깜빡임을 검사합니다. 실행 중에는 테스트 창의 입력이 끝날 때까지 기다려 주세요.

SFTP 인증 포커스 검사는 빌드 후 `node tests/ssh/sftp-auth-integration.cjs`로 실행합니다. 잠기지 않은 Windows 데스크톱에서 격리된 루프백 서버의 다중 OTP 입력·Enter 제출·Esc 취소와 창 전경 복귀를 실제 키보드 입력으로 확인합니다. 다른 테스트 창으로 전환한 뒤 인증창을 닫아도 포커스를 빼앗지 않는지 함께 검사합니다. Linux 교차 빌드는 이 테스트 EXE의 컴파일까지만 수행합니다.

스크롤바 테마 검사는 `node tests/ssh/scrollbar-theme-integration.cjs`로 실행합니다. 실제 왼쪽 호스트 목록에 100개 항목을 넣고, 목록·다중 행 텍스트·파일 목록·드롭다운에 스크롤을 발생시켜 막대와 모서리 픽셀 밝기, 가로·세로 이동, 크기 변경·테마 갱신 후 상태를 확인합니다. 전체 UI 검사에도 포함하며, 설정 드롭다운은 설정 페이지 검사, 별도 엔진의 터미널·이벤트 로그는 `scroll-integration.cjs`에서 확인합니다. Windows 화면이 필요한 검사이며 Linux에서는 실행하지 않습니다.

마우스 회귀 테스트는 격리된 루프백 SSH 서버에서 마우스 보고와 bracketed paste를 켜고 끄며, 터미널 HWND에 보낸 Win32 마우스 메시지와 실제 시스템 클립보드로 복사·한글 붙여넣기·설정 변경을 검증합니다. 기존 클립보드 텍스트는 복원합니다. OS의 실제 입력 대상 선택은 위의 별도 포커스 테스트에서 검증하며, 원격 Codex CLI 자체를 실행하는 테스트는 아닙니다.

스크롤 회귀 테스트는 같은 방식의 루프백 SSH와 휠 메시지를 사용합니다. 테스트 연결의 스크롤바를 켜 실제 위치를 관찰하고, 연속 출력 중 위치 유지, 반 휠 단위 누적, 맨 아래에서 출력 따라가기, 대체 화면의 커서 이동·다시 그리기와 명시적인 자동 이동 설정을 검증합니다.

아래 macOS 절차는 해당 플랫폼을 명시적으로 요청받았을 때 사용합니다. CI에서는 `Desktop builds`를 수동 실행하면서 `build_macos`를 선택해야 macOS 작업이 실행됩니다. `iPad personal preview`도 수동 실행 전용입니다.

macOS 빌드는 macOS 13 이상, Xcode Command Line Tools와 Swift 6 이상, Python 3에서 실행합니다. Swift 의존성은 정확한 커밋과 `mac/Package.resolved`로 고정합니다.

```sh
python3 scripts/build-mac.py       # Swift 테스트, 두 아키텍처 빌드, 앱 번들/아이콘/서명/ZIP 검증
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/mac-integration.cjs # AppKit, 로컬 PTY, 실제 공개키/Keychain 암호 SSH
python3 scripts/index-dist.py      # 현재 버전의 배포 manifest 생성
```

`src/`는 Windows UI·설정 저장소, `patches/`는 PuTTY 통합, `mac/Sources/WShell/`은 AppKit UI, `mac/Sources/WShellCore/`는 macOS 설정·인증·백업, `tests/`와 `mac/Tests/`는 테스트, `scripts/`는 빌드/패키징, `rules/`는 개발 규칙입니다.
작업 전 [AGENTS.md](../AGENTS.md)와 [공통 개발 규칙](../rules/development.md)을 확인합니다.
자동 포맷터나 수치형 커버리지 기준은 아직 없습니다. 기능 변경에는 해당 동작의 회귀 검증을 추가하세요.

검증 범위: 주소/인수 처리, 한글 설정, 파일 잠금·손상, 설정 백업·복원과 기존 데이터 이전, 기존 호스트 키 보존, 세 종류의 키 생성·암호화 저장·잘못된 암호 거부, 암호화된 OpenSSH 키 변환, 실제 SSH 암호/공개키 인증, 저장된 비밀번호의 암호화·대상 확인·변조 거부·백업 제외·삭제, 비밀번호 세션 복제·재연결·수동 입력 전환, 호스트 키 거부·저장, GUI SSH 입출력·리사이즈, 탭 전환·복제, 컬러/한글 화면, 연결 설정 전체 페이지의 글꼴·폭·검색·스크롤과 변경 저장, 세션 설정의 적용·취소, 호스트 인증기관 저장.
UI 검증은 **EXE만 복사한 빈 폴더**에서 시작하며 내장 폰트·브랜딩과 같은 EXE로 실행되는 탭을 확인합니다. 패키징 시 단일 파일 ZIP, x64 시스템 DLL 의존성, 7개 해상도 아이콘, 내장 폰트·라이선스도 검사합니다.
하드웨어 Serial, 실제 프록시/GSSAPI/X11 구성, 원격 Codex 화면 전체는 환경별 실기 검증이 남아 있습니다.
분할 비율 드래그 조정, 클라우드 동기화는 제공하지 않습니다. `plink`·`pscp`·`psftp` 등 별도 CLI 실행파일도 배포하지 않습니다.


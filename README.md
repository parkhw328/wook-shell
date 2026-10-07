<p align="center"><img src="assets/branding/wshell-wordmark.png" width="520" alt="wShell"></p>

# wShell

**가볍게 열고, 여러 서버를 한 창에서.** Windows x64용 네이티브 포터블 터미널입니다.
PuTTY의 연결·터미널 엔진에 탭 작업 공간, 공개 Flexoki Dark 팔레트와 JetBrains Mono를 더했습니다.

![wShell workspace](assets/screenshots/workspace.png)

## 실행하기

1. `wshell-0.1.0-win-x64.zip` 전체를 쓰기 가능한 폴더에 압축 해제합니다.
2. **`wShell.exe`**를 실행합니다. 설치, 관리자 권한, WebView2, .NET, Node.js가 필요하지 않습니다.
3. `New host`에서 접속 정보를 저장하거나, 빠른 연결 칸에 `user@hostname:22`를 입력합니다.
4. 처음 접속하는 SSH 서버의 키 지문을 확인한 뒤 신뢰 여부를 선택합니다.

대상: Windows 10 1903 이상 / Windows 11, x64. 폰트도 프로세스 내부에서만 로드합니다.
실행파일 하나만 옮기지 말고 엔진·폰트·라이선스가 포함된 **폴더 전체**를 함께 옮기세요.
현재 배포본은 코드 서명이 없는 초기 버전입니다.

빌드한 ZIP은 `dist/`에 생성됩니다. GitHub Actions의 성공한 `Windows portable build` 실행에서도 ZIP과 SHA-256 파일을 내려받을 수 있습니다.

## 연결과 탭

- **호스트 관리:** 저장·편집·복제·삭제, 이름/주소/그룹/사용자 검색, 더블 클릭으로 연결.
- **탭:** 새 연결, 독립 세션 복제, 드래그 재정렬, 가운데 클릭으로 닫기, 연결 재시작.
- **터미널:** UTF-8/한글, ANSI·256색·24비트 True Color, 선택/복사/붙여넣기, 10,000줄 스크롤백, 전체 화면.
- **고급 연결:** `Advanced connection…`에서 PuTTY의 프록시, SSH 터널, X11 전달, Pageant, 키 인증, 로깅, 키보드/터미널 설정을 사용합니다. 연결 중에는 `Settings`를 사용하세요.
- **프로토콜:** SSH, Telnet, Rlogin, Raw TCP, Serial. Serial은 `New host`에서 COM 포트와 전송 속도를 지정합니다.
- **도구:** `Tools`에서 PuTTYgen과 Pageant 실행, 데이터/라이선스 폴더 열기, 탭 목록 선택.
- **파일 전송:** 함께 제공하는 `pscp.exe`와 `psftp.exe`를 명령줄에서 사용합니다.
- **미리 보기:** `Color preview`는 서버 연결 없이 실제 터미널 엔진의 색상을 보여 줍니다.

```powershell
.\plink.exe -ssh user@example.com
.\pscp.exe .\example.txt user@example.com:/tmp/
.\psftp.exe user@example.com
```

키 파일은 PuTTY의 `.ppk` 형식을 선택합니다. OpenSSH 개인키는 PuTTYgen에서 가져와 변환할 수 있습니다.
세부 설정 창은 PuTTY의 네이티브 UI를 유지합니다. Pageant는 여러 연결에서 키를 공유하므로 wShell을 닫아도 별도로 실행됩니다.

| 단축키 | 동작 |
| --- | --- |
| `Ctrl+Shift+T` / 탭 줄의 `+` | 새 연결 화면 |
| `Ctrl+Shift+D` | 현재 연결을 새 탭으로 복제 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 다음 / 이전 탭 |
| `Ctrl+Shift+W` | 현재 탭 닫기 |
| `Ctrl+Shift+R` | 현재 연결 다시 시작 |
| `Ctrl+Shift+P` | 호스트 검색 |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | 터미널 복사 / 붙여넣기 |
| `Alt+1` / `Alt+2…9` | 작업 공간 / 연결 탭 1…8 |
| `F11` | 전체 화면 |

터미널 종류는 `xterm-256color`이고 `COLORTERM=truecolor` 환경변수를 요청합니다.
서버가 환경변수 전달을 허용하지 않으면 원격 셸에서 `export COLORTERM=truecolor`를 설정하세요.
Codex 등 원격 TUI의 색상 자동 감지는 해당 프로그램과 서버 설정에도 영향을 받습니다.

## 포터블 데이터

```
wShell.exe
wook-putty.exe          # 수정한 PuTTY 터미널 엔진
puttygen.exe / pageant.exe / plink.exe / pscp.exe / psftp.exe
fonts/                 # 설치 없이 사용하는 JetBrains Mono
assets/                # wShell 이미지 브랜딩
licenses/              # 제3자 라이선스 원문
data/                  # 최초 실행 시 생성
  sessions/            # 세션 설정
  trust/               # 신뢰한 SSH 호스트 키
  cas/                 # SSH 호스트 인증기관
```

설정은 기존 PuTTY 레지스트리와 분리됩니다. 앱을 닫은 뒤 `data/`를 백업하세요.
암호와 프록시 암호는 저장하지 않으며, 개인키 파일은 복사하지 않고 경로만 참조합니다.
장치를 옮길 때는 개인키 경로를 다시 지정해야 할 수 있습니다. `data/`는 암호화된 비밀 저장소가 아닙니다.
테스트 또는 별도 데이터 위치에는 `WOOK_DATA_DIR` 환경변수를 사용할 수 있습니다.

## 개발 및 검증

빌드 환경은 Windows x64, Python 3.10 이상, PowerShell입니다. C++ 컴파일러·CMake·Ninja·PuTTY 소스는 다음 명령이 저장소 내부로 다운로드하고 SHA-256을 확인합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Bootstrap
# 이후 변경 빌드, 핵심 테스트, 배포 ZIP 생성
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

실제 SSH 및 네이티브 UI 통합 테스트에는 개발용 Node.js 22 이상이 추가로 필요합니다.
테스트가 자체 창을 잠시 열고 루프백 서버에 연결합니다. 외부 서버·사용자 키·실제 접속 데이터는 사용하지 않습니다.

```powershell
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/integration.cjs
git diff --check
```

`src/`는 UI·설정 저장소, `patches/`는 PuTTY 통합, `tests/`는 테스트, `scripts/`는 빌드/패키징, `rules/`는 개발 규칙입니다.
기존 `AGENTS.md`는 보존하며 최신 구현 규칙은 [rules/development.md](rules/development.md)에 기록합니다.
자동 포맷터나 수치형 커버리지 기준은 아직 없습니다. 기능 변경에는 해당 동작의 회귀 검증을 추가하세요.

검증 범위: 주소/인수 처리, 한글 설정, 파일 잠금·손상, 실제 SSH 암호 인증, 호스트 키 거부·저장, GUI SSH 입출력·리사이즈, 탭 전환·복제, 컬러/한글 화면, 아이콘·브랜딩 로딩. 패키징 시 ZIP 무결성, x64 시스템 DLL 의존성, 7개 해상도 아이콘, 라이선스 포함 여부도 검사합니다.
하드웨어 Serial, 실제 Pageant/프록시/GSSAPI/X11 구성, 원격 Codex 화면 전체는 환경별 실기 검증이 남아 있습니다.
분할 창, 그래픽 SFTP 탐색기, 클라우드 동기화는 제공하지 않습니다.

## 라이선스와 출처

자체 코드와 wShell 브랜딩 자산은 [MIT License](LICENSE)로 배포합니다.
PuTTY와 Flexoki는 MIT, JetBrains Mono는 SIL OFL 1.1이며 정적으로 연결한 런타임 고지도 포함합니다.
원문 및 버전/수정 범위는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), 이미지 생성 기록은 [assets/branding/README.md](assets/branding/README.md)를 참고하세요.

PuTTY 기반 엔진은 **공식 배포본이 아닌 수정 빌드**입니다. 이 프로젝트는 PuTTY, Termius, JetBrains와 제휴 관계가 없으며 Termius의 독점 폰트나 이미지를 포함하지 않습니다.

<p align="center"><img src="assets/branding/wshell-wordmark.png" width="520" alt="wShell"></p>

# wShell

**가볍게 열고, 여러 서버를 한 창에서.** Windows x64용 네이티브 포터블 터미널입니다.
PuTTY의 연결·터미널 엔진에 탭 작업 공간, 공개 Flexoki Dark 팔레트와 JetBrains Mono를 더했습니다.

![wShell workspace](assets/screenshots/workspace.png)

## 실행하기

1. **`wShell.exe` 하나**를 쓰기 가능한 폴더에 복사합니다. `wshell-0.2.0-win-x64.zip`에도 이 파일만 들어 있습니다.
2. 실행합니다. 설치, 관리자 권한, WebView2, .NET, Node.js가 필요하지 않습니다.
3. `New host`에서 접속 정보를 저장하거나, 빠른 연결 칸에 `user@hostname:22`를 입력합니다.
4. 처음 접속하는 SSH 서버의 키 지문을 확인한 뒤 신뢰 여부를 선택합니다.

대상: Windows 10 1903 이상 / Windows 11, x64. 터미널 엔진·폰트·브랜딩·라이선스를 EXE 안에 포함하며, 실행할 때 보조 EXE나 폰트를 임시 폴더에 풀지 않습니다.
폰트는 프로세스 안에서만 로드합니다. 현재 배포본에는 코드 서명이 없습니다.

빌드한 EXE, ZIP, SHA-256 파일은 `dist/`에 생성됩니다. GitHub Actions의 성공한 `Windows portable build` 실행에서도 내려받을 수 있습니다.

## 연결과 탭

- **호스트 관리:** 저장·편집·복제·삭제, 이름/주소/그룹/사용자 검색, 더블 클릭으로 연결.
- **탭:** 새 연결, 독립 세션 복제, 드래그 재정렬, 가운데 클릭으로 닫기, 연결 재시작.
- **터미널:** UTF-8/한글, ANSI·256색·24비트 True Color, 선택/복사/붙여넣기, 10,000줄 스크롤백, 전체 화면.
- **고급 연결:** `Advanced connection…`에서 프록시, SSH 터널, X11 전달, 키 인증, 로깅, 키보드/터미널 설정을 사용합니다. 연결 중에는 `Settings`를 사용하세요.
- **프로토콜:** SSH, Telnet, Rlogin, Raw TCP, Serial. Serial은 `New host`에서 COM 포트와 전송 속도를 지정합니다.
- **도구:** 자체 SSH 키 관리, 설정 내보내기/가져오기, 데이터 폴더 열기, 내장 라이선스 보기, 탭 목록 선택.
- **미리 보기:** `Color preview`는 서버 연결 없이 실제 터미널 엔진의 색상을 보여 줍니다.

각 탭은 같은 `wShell.exe`의 별도 프로세스로 연결을 관리합니다. PuTTYgen, Pageant나 설치된 SSH 서비스에 의존하지 않으며, 외부 에이전트 인증·에이전트 전달·연결 공유는 사용하지 않습니다.
고급 설정은 통합한 PuTTY의 네이티브 UI를 사용합니다.

앱의 글꼴은 **JetBrains Mono Regular/Bold**로 통일했습니다. 본문·입력·버튼 11pt, 보조 정보 9pt, 섹션 제목 13pt, 페이지 제목 20pt를 공통으로 적용합니다. 터미널 기본 크기도 11pt이며 호스트별로 조절할 수 있습니다. Windows의 파일 선택창 등 시스템 대화상자는 OS 설정을 따릅니다.

## SSH 키 관리

`Tools → SSH key manager…`에서 **Ed25519, RSA 3072, RSA 4096** 키를 생성합니다. 개인키와 SHA-256 지문을 앱 안에서 처리하며 별도 프로그램을 실행하지 않습니다.

1. 알고리즘을 선택하고 `Generate`를 누릅니다.
2. 개인키를 암호화하려면 `Passphrase`에 암호를 입력한 뒤 `Save private key`로 `.ppk`를 저장합니다. 빈 암호로 저장하면 암호화되지 않습니다.
3. 공개키를 복사하거나 `Save public key`로 `.pub`를 저장하여 서버의 `authorized_keys`에 등록합니다.
4. `New host`의 `Private key`에서 저장한 `.ppk`를 선택합니다.

`Import key…`는 기존 PPK와 OpenSSH 개인키를 읽습니다. 암호화된 키는 가져오기 전에 암호를 입력하세요. 저장은 PPK v3 형식이며, 가져오기와 저장에 사용한 암호 입력은 작업 후 지웁니다.
RSA 생성은 Windows CNG, Ed25519와 키 형식 처리는 PuTTY 라이브러리를 사용합니다.

![Native SSH key manager](assets/screenshots/key-manager.png)

## 단축키와 컬러

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
data/                  # 최초 실행 시 생성
  sessions/            # 세션 설정
  trust/               # 신뢰한 SSH 호스트 키
  cas/                 # SSH 호스트 인증기관
```

**실행에 필요한 파일은 EXE 하나이고, `data/`는 사용자가 저장한 설정입니다.** 새 환경에서는 EXE만 복사하면 됩니다. 저장한 서버까지 그대로 옮기려면 앱을 닫은 뒤 `data/`도 복사하거나 `Tools → Export settings…`로 만든 `.wshell` 파일을 `Import settings…`로 가져오세요.

백업에는 세션·신뢰한 호스트 키·호스트 인증기관이 포함됩니다. 가져오기는 기존 이름의 세션이나 기존 호스트 키를 덮어쓰지 않습니다. 신뢰할 수 있는 백업만 가져오고 연결 전에 설정을 확인하세요. 0.1의 `data/`도 그대로 사용할 수 있습니다.
설정은 기존 PuTTY 레지스트리와 분리됩니다.
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

검증 범위: 주소/인수 처리, 한글 설정, 파일 잠금·손상, 설정 백업·복원, 기존 호스트 키 보존, 세 종류의 키 생성·암호화 저장·잘못된 암호 거부, 암호화된 OpenSSH 키 변환, 실제 SSH 암호/공개키 인증, 호스트 키 거부·저장, GUI SSH 입출력·리사이즈, 탭 전환·복제, 컬러/한글 화면.
UI 검증은 **EXE만 복사한 빈 폴더**에서 시작하며 내장 폰트·브랜딩과 같은 EXE로 실행되는 탭을 확인합니다. 패키징 시 단일 파일 ZIP, x64 시스템 DLL 의존성, 7개 해상도 아이콘, 내장 폰트·라이선스도 검사합니다.
하드웨어 Serial, 실제 프록시/GSSAPI/X11 구성, 원격 Codex 화면 전체는 환경별 실기 검증이 남아 있습니다.
분할 창, 그래픽 SFTP 탐색기, 클라우드 동기화는 제공하지 않습니다. `plink`·`pscp`·`psftp` 등 별도 CLI 실행파일도 배포하지 않습니다.

## 라이선스와 출처

자체 코드와 wShell 브랜딩 자산은 [MIT License](LICENSE)로 배포합니다.
PuTTY와 Flexoki는 MIT, JetBrains Mono는 SIL OFL 1.1이며 정적으로 연결한 런타임 고지도 포함합니다.
원문 및 버전/수정 범위는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), 이미지 생성 기록은 [assets/branding/README.md](assets/branding/README.md)를 참고하세요.
재배포에 필요한 라이선스 원문은 EXE에 내장되어 있으며 `Tools → Open-source licenses`에서 읽을 수 있습니다.

PuTTY 기반 엔진은 **공식 배포본이 아닌 수정 빌드**입니다. 이 프로젝트는 PuTTY, Termius, JetBrains와 제휴 관계가 없으며 Termius의 독점 폰트나 이미지를 포함하지 않습니다.

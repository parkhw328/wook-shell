<p align="center"><img src="assets/branding/wshell-icon.png" width="116" alt="wShell icon"></p>

# wShell

**가볍게 열고, 여러 서버를 한 창에서.** Windows x64와 macOS용 네이티브 터미널입니다.
탭 작업 공간, 공개 Flexoki Dark 팔레트와 JetBrains Mono를 사용합니다. Created by **Hyunwook Park**.

![wShell workspace](assets/screenshots/workspace.png)

## 분할 보기

여러 탭을 연 뒤 **Split**에서 2·3·4분할을 선택합니다. 영역을 클릭하면 입력 대상이 바뀌며, **Single pane**으로 돌아가도 연결은 유지됩니다. Windows `Ctrl+Shift+S`, Mac `⌘⇧S`. [자세한 사용법](docs/split-view.md)

![wShell split terminals](assets/screenshots/split.png)

## iPad 개인용 미리보기

iPad용 네이티브 SSH·SFTP 앱은 별도로 준비 중입니다. 무료 Apple 계정으로 설치할 수 있으며, unsigned IPA의 로컬 서명과 7일 주기 갱신이 필요합니다. [기능·빌드·설치 안내](docs/ipad.md)

## 실행하기

### Windows

1. **`wShell.exe` 하나**를 원하는 폴더에 복사합니다. `wshell-0.8.0-windows-x64.zip`에도 이 파일만 들어 있습니다.
2. 실행합니다. 설치, 관리자 권한, WebView2, .NET, Node.js가 필요하지 않습니다.
3. `New host`에서 접속 정보를 저장하거나, 빠른 연결 칸에 `user@hostname:22`를 입력합니다.
4. 처음 접속하는 SSH 서버의 키 지문을 확인한 뒤 신뢰 여부를 선택합니다.

대상: Windows 10 1903 이상 / Windows 11, x64. 터미널 엔진·폰트·브랜딩·라이선스를 EXE 안에 포함하며, 실행할 때 보조 EXE나 폰트를 임시 폴더에 풀지 않습니다.
폰트는 프로세스 안에서만 로드합니다. 현재 배포본에는 코드 서명이 없습니다.

### macOS

1. `wshell-0.8.0-macos-universal.zip`을 풀고 **`wShell.app`**을 실행합니다. Applications 폴더로 옮겨도 됩니다.
2. macOS 13 이상에서 Apple Silicon·Intel을 모두 지원하는 Universal 앱입니다. 별도 런타임이나 WebView 설치가 필요하지 않습니다.
3. `New host`로 SSH 서버를 추가하거나 `Open Terminal`로 현재 Mac의 로그인 셸을 엽니다.

macOS 배포본은 ad-hoc 서명 상태이며 **Apple Developer ID 서명·공증은 아직 없습니다**. 다운로드한 앱의 첫 실행은 Gatekeeper가 차단할 수 있습니다. 출처를 확인한 경우 시스템 설정의 개인정보 보호 및 보안에서 해당 앱 실행을 허용해야 합니다.
macOS의 `.app`은 Finder에서 하나의 앱으로 이동하는 번들이며, 내부 실행파일만 꺼내 사용하는 형태는 아닙니다.

![wShell macOS workspace](assets/screenshots/mac-workspace.png)

### 버전별 배포

루트 `VERSION`에서 앱·패키지 버전을 관리합니다. 성공한 GitHub Actions의 `Desktop builds`에서도 두 플랫폼의 배포물을 받을 수 있습니다.

```text
dist/
  README.md
  0.8.0/
    manifest.json
    windows-x64/
      wShell.exe
      wShell.exe.sha256
      wshell-0.8.0-windows-x64.zip
      wshell-0.8.0-windows-x64.zip.sha256
    macos-universal/
      wshell-0.8.0-macos-universal.zip
      wshell-0.8.0-macos-universal.zip.sha256
```

| 기능 | Windows | macOS |
| --- | --- | --- |
| SSH 비밀번호·공개키 인증, 컬러 터미널, 탭 | 지원 | 지원 |
| SFTP 전용 탭 | 로컬·원격 목록, 파일 업로드/다운로드 | 동일 |
| 로컬 셸 | CMD 기본 / PowerShell 선택 | 시스템 로그인 셸(zsh 등) |
| 저장 비밀번호 | 현재 Windows 계정 DPAPI | 이 Mac의 로그인 Keychain, 동기화 제외 |
| 키 생성 | Ed25519 / RSA, 자체 키 관리자 | Ed25519 / RSA, OS ssh-keygen을 앱 내 터미널에서 실행 |
| 개인키 파일 | PPK; OpenSSH를 키 관리자에서 변환 | OpenSSH 형식 |
| 고급 PuTTY 설정 / Serial·Telnet·Rlogin·Raw | 지원 | 이번 macOS 버전에서는 미지원 |
| 설정 백업 | `.wshell`, 암호·개인키 제외 | 같은 형식; 호스트 목록 교환 가능 |

macOS는 AppKit·SwiftTerm과 OS의 OpenSSH를 사용합니다. SSH 설정 파일·에이전트·공유 연결은 별도로 사용하지 않으며 호스트 키 저장소도 wShell 전용입니다. Windows와 macOS 엔진의 호스트 신뢰 형식은 서로 다르므로 플랫폼을 바꾸면 지문을 다시 확인하세요. PPK 파일을 OpenSSH 개인키로 자동 변환하지 않습니다.
macOS의 첫 접속은 비밀번호 공급 전에 서버 키 지문을 별도 화면에서 확인합니다. 확인한 키와 다른 키가 제시되면 연결을 거부합니다.

## Windows 연결과 탭

**SFTP:** SSH 호스트를 선택하고 `SFTP`를 누르면 로컬·원격 파일 목록을 나란히 볼 수 있습니다. 여러 파일 전송, 진행률·취소, 폴더 생성·이름 변경·삭제를 지원합니다. Windows SSH 탭의 `Files`, macOS의 `Tools → Open SFTP`에서도 열 수 있습니다. 폴더 전체 재귀 전송·동기화는 아직 지원하지 않습니다. [사용법과 지원 범위](docs/sftp.md)를 확인하세요.

![wShell SFTP tab](assets/screenshots/sftp.png)

- **호스트 관리:** 저장·편집·복제·삭제, 이름/주소/그룹/사용자 검색, 더블 클릭으로 연결.
- **탭:** 새 연결, 독립 세션 복제, 드래그 재정렬, 가운데 클릭으로 닫기, 연결 재시작.
- **터미널:** UTF-8/한글, ANSI·256색·24비트 True Color, 선택/복사/붙여넣기, 10,000줄 스크롤백, 전체 화면.
- **연결 설정:** `Connection settings`에서 프록시, SSH 터널, X11 전달, 키 인증, 로깅, 키보드/터미널 설정을 사용합니다. 설정 이름이나 항목으로 검색할 수 있으며, 긴 페이지는 스크롤합니다. 연결 중에는 `Settings`에서 변경하고 `Apply changes`로 적용하거나 `Cancel`로 취소하세요.
- **프로토콜:** SSH, Telnet, Rlogin, Raw TCP, Serial. Serial은 `New host`에서 COM 포트와 전송 속도를 지정합니다.
- **도구:** 자체 SSH 키 관리, 설정 내보내기/가져오기, 데이터 폴더 열기, 내장 라이선스 보기, 탭 목록 선택.
- **미리 보기:** `Color preview`는 서버 연결 없이 실제 터미널 엔진의 색상을 보여 줍니다.
- **로컬 셸:** 홈 또는 `Tools`에서 `Command Prompt` / `PowerShell`을 엽니다. `Ctrl+Shift+L`은 CMD를 열며, 탭 복제·전환·종료도 사용할 수 있습니다. ConPTY를 통해 이 PC의 명령을 실행하며 기본 작업 폴더는 현재 사용자의 홈입니다.

![Local Command Prompt](assets/screenshots/local-cmd.png)

각 탭은 같은 `wShell.exe`의 별도 프로세스로 연결을 관리합니다. PuTTYgen, Pageant나 설치된 SSH 서비스에 의존하지 않으며, 외부 에이전트 인증·에이전트 전달·연결 공유는 사용하지 않습니다.
연결 설정·세션 설정·호스트 인증기관 관리 창은 wShell의 Flexoki Dark 화면을 사용합니다. 기존 엔진의 설정과 검증 로직을 유지하면서 탐색, 검색, 입력, 버튼과 스크롤을 새로 구성했습니다.

앱의 글꼴은 **JetBrains Mono Regular/Bold**로 통일했습니다. 본문·입력·버튼 11pt, 보조 정보 9pt, 섹션 제목 13pt, 페이지 제목 20pt를 공통으로 적용합니다. 터미널 기본 크기도 11pt이며 호스트별로 조절할 수 있습니다. Windows의 파일 선택창 등 시스템 대화상자는 OS 설정을 따릅니다.
버튼·선택·포커스 강조색은 따뜻한 주황색 `#DA702C`입니다. 앱과 설정 화면의 왼쪽 상단에는 `wShell` 텍스트만 표시합니다. 실행파일 아이콘은 단색 주황색 `w`이며, 터미널의 ANSI·True Color 출력은 기존 색상을 유지합니다.

![wShell connection settings](assets/screenshots/connection-settings.png)

## 한글 입력 보정

Windows에서는 한글 조합을 별도 팝업 대신 **터미널 커서 자리**에 표시합니다. 터미널과 같은 글꼴·크기·배경을 사용하며, 조합 중인 글자는 주황색 밑줄로 구분합니다. 한글은 두 칸 폭에 맞추고 창 크기·커서 위치를 따라 이동합니다. SSH와 로컬 CMD/PowerShell에 별도 설정 없이 적용됩니다.

조합 중인 글자는 화면에만 표시하며 확정된 글자만 전송합니다. 탭을 옮길 때는 기존 탭에서 조합을 마무리합니다. 한자 등 후보 선택창은 Windows 기본 기능을 유지하며 조합 위치에 맞춰 배치합니다. macOS도 조합 밑줄·선택 범위와 attributed 확정 문자열을 처리합니다. JetBrains Mono에 없는 한글 글리프는 OS 글꼴로 보완합니다.

자동 검증은 합성 조합 이벤트·취소·한글 폭·리사이즈와 실제 루프백 SSH의 한글/이모지 전송을 포함합니다. OS 입력기별 실제 타이핑·후보창 동작은 [수동 확인 절차](docs/ime-input.md)를 따릅니다. 서버의 문자 인코딩이나 셸 편집 동작은 변경하지 않습니다.

![커서 위치에 표시한 한글 조합 — 합성 입력 테스트](assets/screenshots/ime-composition.png)

## 공개키 인증과 SSH 키 관리

SSH의 **공개키 인증은 키 쌍을 사용**합니다. 서버의 `~/.ssh/authorized_keys`에 공개키(`.pub`)를 등록하고, wShell은 대응하는 개인키로 서명합니다. 공개키 파일만으로는 로그인할 수 없습니다. 외부 에이전트의 키는 사용하지 않습니다.

`Authentication → Public key authentication`을 선택하고 개인키 경로를 지정하세요. Windows는 `.ppk`, macOS는 OpenSSH 개인키를 사용합니다. 개인키는 서버에 업로드하지 마세요.

![Public key authentication](assets/screenshots/public-key-host.png)

`No supported authentication methods available (server sent: publickey,gssapi-keyex,gssapi-with-mic)`가 나오면 서버가 비밀번호 인증을 제공하지 않는 상태입니다. 올바른 사용자 이름과 해당 사용자에 등록된 공개키의 **짝이 되는 개인키**를 확인하세요. `.pem` 또는 `id_ed25519` / `id_rsa` 파일을 가지고 있다면 Windows 키 관리자의 `Import key`로 가져온 뒤 `.ppk`로 저장하여 선택합니다. 개인키가 없다면 새 키 쌍을 생성하고 관리자에게 공개키 등록을 요청해야 합니다. 이름만 `.pub`에서 `.ppk`로 바꿔서는 사용할 수 없습니다. 인증서·조직의 Kerberos 설정은 별도 구성이 필요합니다. [OpenSSH 공개키 인증 설명](https://man.openbsd.org/ssh#AUTHENTICATION)

다음 자체 키 관리자 절차는 Windows에 해당합니다.

`Tools → SSH key manager…`에서 **Ed25519, RSA 3072, RSA 4096** 키를 생성합니다. 개인키와 SHA-256 지문을 앱 안에서 처리하며 별도 프로그램을 실행하지 않습니다.

1. 알고리즘을 선택하고 `Generate`를 누릅니다.
2. 개인키를 암호화하려면 `Passphrase`에 암호를 입력한 뒤 `Save private key`로 `.ppk`를 저장합니다. 빈 암호로 저장하면 암호화되지 않습니다.
3. 공개키를 복사하거나 `Save public key`로 `.pub`를 저장하여 서버의 `authorized_keys`에 등록합니다.
4. `New host`의 `Authentication → Public key authentication`에서 저장한 `.ppk`를 선택합니다.

`Import key…`는 기존 PPK와 OpenSSH 개인키를 읽습니다. 암호화된 키는 가져오기 전에 암호를 입력하세요. 저장은 PPK v3 형식이며, 가져오기와 저장에 사용한 암호 입력은 작업 후 지웁니다.
인증 화면의 `Generate or import a key pair`로 키 관리자를 열면, 저장한 개인키 경로가 호스트 설정에 자동으로 채워집니다. `.pem`·`.key`·`.txt` 확장자도 가져오기 목록에서 선택할 수 있으며 실제 파일 내용을 기준으로 형식을 판별합니다.
RSA 생성은 Windows CNG, Ed25519와 키 형식 처리는 PuTTY 라이브러리를 사용합니다.

![Native SSH key manager](assets/screenshots/key-manager.png)

## SSH 비밀번호 저장

`New host` 또는 `Edit host`에서 `Authentication → Password`를 선택합니다. 기본값은 연결할 때 입력하는 방식입니다.

1. 서버 주소·포트·사용자 이름을 입력합니다.
2. `Save password encrypted on this PC`를 체크하고 비밀번호를 입력한 뒤 저장합니다.
3. 다음 연결부터 저장한 비밀번호를 사용합니다. 편집 시 비밀번호 칸을 비워 두면 기존 값을 유지하고, 새 값을 입력하면 교체합니다.
4. 체크를 해제하고 저장하거나 키 인증으로 전환하면 저장된 비밀번호를 삭제합니다. 호스트를 삭제해도 함께 삭제됩니다.

비밀번호는 [Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)로 현재 Windows 계정에 연결하여 암호화합니다. AppData의 세션 파일에는 암호문만 포함하며, 프로세스 인수·환경변수·임시 파일로 평문을 전달하지 않습니다. 서버·포트·사용자 이름에 연결해 보관하므로 연결 대상이 달라지면 다시 입력해야 합니다.

일반 SSH `password` 인증에 한 번 자동 입력하며, 거부되거나 복호화할 수 없으면 터미널에서 직접 입력합니다. 키 암호, 비밀번호 변경, keyboard-interactive/OTP 질문에는 자동 입력하지 않습니다.
**`.wshell` 내보내기·가져오기에는 암호문도 포함하지 않습니다.** 다른 PC나 Windows 계정에서는 비밀번호를 다시 저장하세요. 로그인된 동일 Windows 계정의 프로그램으로부터 비밀번호를 격리하는 기능은 아닙니다.

![Encrypted SSH password settings](assets/screenshots/password-host.png)

## 단축키와 컬러

| 단축키 | 동작 |
| --- | --- |
| `Ctrl+Shift+T` / 탭 줄의 `+` | 새 연결 화면 |
| `Ctrl+Shift+L` | 로컬 CMD 탭 |
| `Ctrl+Shift+D` | 현재 연결을 새 탭으로 복제 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 다음 / 이전 탭 |
| `Ctrl+Shift+W` | 현재 탭 닫기 |
| `Ctrl+Shift+R` | 현재 연결 다시 시작 |
| `Ctrl+Shift+P` | 호스트 검색 |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | 터미널 복사 / 붙여넣기 |
| `Alt+1` / `Alt+2…9` | 작업 공간 / 연결 탭 1…8 |
| `F11` | 전체 화면 |

macOS: `⌘⇧L` 로컬 터미널, `⌘⇧T` 새 연결 화면, `⌘⇧D` 탭 복제, `⌘W` 탭 닫기, `⌘⇧R` 재연결, `⌘⇧[` / `⌘⇧]` 이전/다음 탭, `⌘C` / `⌘V` 복사/붙여넣기. `Tabs` 메뉴에서 탭 순서를 바꿀 수 있습니다.

터미널 종류는 `xterm-256color`이고 `COLORTERM=truecolor` 환경변수를 요청합니다.
서버가 환경변수 전달을 허용하지 않으면 원격 셸에서 `export COLORTERM=truecolor`를 설정하세요.
Codex 등 원격 TUI의 색상 자동 감지는 해당 프로그램과 서버 설정에도 영향을 받습니다.

## AppData 저장과 내보내기·가져오기

```
%LOCALAPPDATA%/wShell/  # 최초 실행 시 생성
  sessions/            # 세션 설정
  trust/               # 신뢰한 SSH 호스트 키
  cas/                 # SSH 호스트 인증기관
```

**실행에 필요한 파일은 EXE 하나이며, 설정은 현재 Windows 사용자의 AppData에 저장됩니다.** 일반적인 위치는 `C:\Users\<사용자>\AppData\Local\wShell`입니다. EXE 옆에 `data/`를 새로 만들지 않습니다. 같은 PC에서는 EXE를 옮기거나 교체해도 저장한 서버 목록을 그대로 사용합니다.

1. `Tools → Export settings…`에서 `.wshell` 백업을 저장합니다.
2. 다른 PC에 EXE와 백업을 가져간 뒤 `Tools → Import settings…`로 복원합니다.
3. 현재 저장 위치는 `Tools → Open AppData folder`에서 확인합니다.

**비밀번호는 Export/Import로 옮겨지지 않습니다. 암호화된 비밀번호도 백업에서 제외되며, 가져온 호스트의 비밀번호는 다시 입력해야 합니다.** 파일 선택 화면과 완료 안내에도 이를 표시합니다.

백업에는 세션·신뢰한 호스트 키·호스트 인증기관이 포함됩니다. 가져오기는 기존 이름의 세션이나 기존 호스트 키를 덮어쓰지 않습니다. 신뢰할 수 있는 백업만 가져오고 연결 전에 설정을 확인하세요.
이전 버전의 EXE 옆 `data/`가 있으면 최초 전환 시 AppData로 한 번 가져옵니다. 원본 폴더와 AppData의 기존 항목은 보존하며, 이후에는 AppData만 사용합니다. 이미 EXE만 이동했다면 이전 버전에서 내보낸 백업으로 가져올 수 있습니다.
설정은 기존 PuTTY 레지스트리와 분리됩니다.
SSH 비밀번호는 선택한 호스트에 한해 암호화해 저장하고 백업에서는 제외합니다. 프록시 암호는 저장하지 않으며, 개인키 파일은 복사하지 않고 경로만 참조합니다.
장치를 옮길 때는 개인키를 별도로 이동하고 경로를 다시 지정해야 할 수 있습니다. 설정 폴더와 `.wshell` 백업은 암호화된 비밀 저장소가 아닙니다.
테스트 또는 별도 데이터 위치에는 `WOOK_DATA_DIR` 환경변수를 사용할 수 있습니다.

macOS는 `~/Library/Application Support/wShell/`의 `settings.json`과 `known_hosts`에 저장합니다. 저장 비밀번호는 Keychain에 별도로 보관하며 `.wshell` 백업에서는 제외됩니다. 호스트 편집 시 비밀번호를 비워 두면 같은 접속 대상의 기존 값을 유지합니다. 저장 체크를 해제하거나 공개키 인증으로 바꾸면 저장 비밀번호를 삭제합니다.

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
node tests/ssh/sftp-integration.cjs
git diff --check
```

macOS 빌드는 macOS 13 이상, Xcode Command Line Tools와 Swift 6 이상, Python 3에서 실행합니다. Swift 의존성은 정확한 커밋과 `mac/Package.resolved`로 고정합니다.

```sh
python3 scripts/build-mac.py       # Swift 테스트, 두 아키텍처 빌드, 앱 번들/아이콘/서명/ZIP 검증
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/mac-integration.cjs # AppKit, 로컬 PTY, 실제 공개키/Keychain 암호 SSH
python3 scripts/index-dist.py      # 현재 버전의 배포 manifest 생성
```

`src/`는 Windows UI·설정 저장소, `patches/`는 PuTTY 통합, `mac/Sources/WShell/`은 AppKit UI, `mac/Sources/WShellCore/`는 macOS 설정·인증·백업, `tests/`와 `mac/Tests/`는 테스트, `scripts/`는 빌드/패키징, `rules/`는 개발 규칙입니다.
기존 `AGENTS.md`는 보존하며 최신 구현 규칙은 [rules/development.md](rules/development.md)에 기록합니다.
자동 포맷터나 수치형 커버리지 기준은 아직 없습니다. 기능 변경에는 해당 동작의 회귀 검증을 추가하세요.

검증 범위: 주소/인수 처리, 한글 설정, 파일 잠금·손상, 설정 백업·복원과 기존 데이터 이전, 기존 호스트 키 보존, 세 종류의 키 생성·암호화 저장·잘못된 암호 거부, 암호화된 OpenSSH 키 변환, 실제 SSH 암호/공개키 인증, 저장된 비밀번호의 암호화·대상 확인·변조 거부·백업 제외·삭제, 비밀번호 세션 복제·재연결·수동 입력 전환, 호스트 키 거부·저장, GUI SSH 입출력·리사이즈, 탭 전환·복제, 컬러/한글 화면, 연결 설정 전체 페이지의 글꼴·폭·검색·스크롤과 변경 저장, 세션 설정의 적용·취소, 호스트 인증기관 저장.
UI 검증은 **EXE만 복사한 빈 폴더**에서 시작하며 내장 폰트·브랜딩과 같은 EXE로 실행되는 탭을 확인합니다. 패키징 시 단일 파일 ZIP, x64 시스템 DLL 의존성, 7개 해상도 아이콘, 내장 폰트·라이선스도 검사합니다.
하드웨어 Serial, 실제 프록시/GSSAPI/X11 구성, 원격 Codex 화면 전체는 환경별 실기 검증이 남아 있습니다.
분할 비율 드래그 조정, 클라우드 동기화는 제공하지 않습니다. `plink`·`pscp`·`psftp` 등 별도 CLI 실행파일도 배포하지 않습니다.

## 라이선스와 출처

자체 코드와 wShell 브랜딩 자산은 [MIT License](LICENSE)로 배포합니다.
PuTTY·SwiftTerm·Flexoki는 MIT, JetBrains Mono는 SIL OFL 1.1이며 정적으로 연결한 런타임 고지도 포함합니다.
원문 및 버전/수정 범위는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), 이미지 생성 기록은 [assets/branding/README.md](assets/branding/README.md)를 참고하세요.
재배포에 필요한 라이선스 원문은 EXE에 내장되어 있으며 `Tools → Open-source licenses`에서 읽을 수 있습니다.

PuTTY 기반 엔진은 **공식 배포본이 아닌 수정 빌드**입니다. 이 프로젝트는 PuTTY, Termius, JetBrains와 제휴 관계가 없으며 Termius의 독점 폰트나 이미지를 포함하지 않습니다.

## Host aliases and split input

Windows and Mac support optional host aliases and tab colors, a split-view command bar with **Send to all panes**, and opt-in **Sync keyboard**. Both modes default off and target visible connected terminals only. See [usage and input scope](docs/split-view.md).

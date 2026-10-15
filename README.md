<p align="center"><img src="assets/branding/wshell-icon.png" width="116" alt="wShell icon"></p>

# wShell

**여러 서버의 SSH 접속과 파일 전송을 한 창에서 관리하는 네이티브 터미널입니다.**
Windows는 설치 없이 `wShell.exe` 하나로 실행합니다. Flexoki Dark 테마와 JetBrains Mono 폰트를 기본 제공합니다.

[Windows 다운로드](https://github.com/parkhw328/wook-shell/raw/refs/heads/main/release/latest/windows-x64/wShell.exe) · [전체 다운로드](release/README.md) · [상세 사용법](docs/usage.md) · [작업 규칙](AGENTS.md) · [문제 신고](https://github.com/parkhw328/wook-shell/issues)

![wShell 작업 공간](assets/screenshots/workspace.png)

## 주요 기능

아래는 현재 Windows 배포 기준입니다. macOS와의 차이는 [지원 플랫폼](#지원-플랫폼)을 확인하세요.

| 기능 | 할 수 있는 일 |
| --- | --- |
| 연결 관리 | 호스트 저장·검색, 폴더 분류·순서 변경, 별칭·탭 색상 지정 |
| 탭과 창 | 연결 복제·재접속, 탭 재정렬, 탭을 별도 창으로 분리하거나 다시 합치기 |
| 분할 화면 | 2·3·4분할, 활성 영역 확대·복귀, 영역별 독립 입력 |
| 공통 입력 | 선택한 분할 영역에 명령 전송, 키보드 입력 동기화 |
| SFTP | 로컬·원격 파일 탐색, 파일 업로드·다운로드·드래그 전송, 진행률·취소 |
| SSH 인증 | 비밀번호·공개키 인증, Ed25519/RSA 키 생성, PPK·OpenSSH·PEM 개인키 등록 |
| 로컬 터미널 | CMD·PowerShell을 SSH와 같은 작업 공간에서 사용 |
| 터미널 사용성 | 한글 조합 표시, 드래그 복사·우클릭 붙여넣기, 스크롤 기록, ANSI·True Color |
| 고급 연결 | Serial·Telnet·Rlogin·Raw, 프록시·SSH 터널·X11 등 PuTTY 설정 |
| 설정 이전 | `.wshell` 파일로 설정 백업·복원, 기존 호스트와 신뢰 키 보존 |

## 다운로드와 시작하기

| 플랫폼 | 배포 버전 | 다운로드 및 상태 |
| --- | --- | --- |
| Windows x64 | 1.2.0 | [EXE](https://github.com/parkhw328/wook-shell/raw/refs/heads/main/release/latest/windows-x64/wShell.exe) · [ZIP](https://github.com/parkhw328/wook-shell/raw/refs/heads/main/release/latest/windows-x64/wshell-windows-x64.zip) · 현재 개발 대상 |
| macOS Universal | 0.11.0 | [ZIP](release/0.11.0/macos-universal/wshell-0.11.0-macos-universal.zip) · 기존 배포 유지, 새 빌드 중단 |
| iPad | 미리보기 보류 | [기존 테스트 파일 및 안내](tests/ipad/README.md) |

Windows 고정 다운로드 주소는 새 배포에도 유지됩니다. 버전·SHA-256은 [최신 manifest](release/latest/manifest.json), 변경 내역과 검증 한계는 [1.2.0 릴리스 노트](docs/releases/1.2.0.md)에서 확인할 수 있습니다.

### Windows

1. Windows 10 1903 이상 또는 Windows 11 x64에서 `wShell.exe`를 실행합니다. 설치·관리자 권한·별도 런타임이 필요하지 않습니다.
2. **New host**에서 접속 정보를 저장하거나 빠른 연결 칸에 `user@hostname:22`를 입력합니다.
3. 처음 연결하는 서버의 SSH 키 지문을 확인하고 신뢰 여부를 선택합니다.
4. 여러 연결을 연 뒤 **Split**으로 분할하거나, 호스트를 선택하고 **SFTP**로 파일을 전송합니다.

현재 Windows EXE는 **미서명**입니다. 업데이트할 때는 기존 wShell 창을 모두 종료한 뒤 EXE를 교체하세요. 기존 창이 남아 있으면 추가 실행이 이전 창으로 전달될 수 있습니다.

### macOS

macOS 13 이상에서 ZIP을 풀고 `wShell.app`을 실행합니다. Apple Silicon·Intel을 지원하며 **Open Terminal**로 로그인 셸을 엽니다. 배포본은 ad-hoc 서명 상태로 Developer ID 서명·공증이 없습니다. 첫 실행이 차단되면 출처를 확인한 뒤 시스템 설정의 개인정보 보호 및 보안에서 실행을 허용하세요.

## 자주 쓰는 기능

| 작업 | 사용 방법 |
| --- | --- |
| 호스트 정리 | **+ Folder**로 폴더 생성 → 호스트 우클릭 **Move to folder** → **↑ / ↓**로 순서 변경 |
| 화면 분할 | **Split**에서 2·3·4분할 선택, **Zoom / Back**으로 확대·복귀 |
| 여러 서버에 입력 | **Targets**에서 대상 선택 → **Send to targets** 또는 **Sync keyboard** 활성화 |
| 탭을 새 창으로 이동 | 탭을 창 밖으로 드래그하거나 우클릭 **Move to new window** |
| 파일 전송 | **SFTP** 또는 SSH 탭의 **Files** → 로컬·원격 파일 선택 후 전송 |
| 개인키 사용 | 호스트의 **Public key authentication** → **Choose or import a registered key** |
| 비밀번호 저장 | 호스트의 **Password** → **Save password encrypted on this PC** 선택 |
| 설정 옮기기 | **Tools → Export settings…**로 백업 → 다른 PC에서 **Import settings…** |

공통 입력은 기본 해제이며 연결된 분할 터미널만 대상으로 합니다. SFTP는 **파일 전송**을 지원하며 폴더 전체 재귀 전송·동기화는 지원하지 않습니다. 자세한 동작은 [분할 보기](docs/split-view.md), [SFTP](docs/sftp.md), [상세 사용법](docs/usage.md)을 참고하세요.

### 주요 단축키 (Windows)

| 단축키 | 동작 |
| --- | --- |
| `Ctrl+Shift+T` / `Ctrl+Shift+L` | 새 연결 / 로컬 CMD |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 다음 / 이전 탭 |
| `Ctrl+Shift+D` / `Ctrl+Shift+W` | 탭 복제 / 닫기 |
| `Ctrl+Shift+S` / `Ctrl+Shift+Enter` | 분할 선택 / 활성 영역 확대·복귀 |
| `Ctrl+Alt+방향키` | 인접 분할 영역으로 이동 |
| `Ctrl+Shift+H` / `Ctrl+Shift+P` | 연결 목록 접기·펼치기 / 검색 |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | 복사 / 붙여넣기 |

전체 단축키와 macOS 키 조합은 [상세 사용법](docs/usage.md#단축키와-컬러)에 있습니다.

## 설정 저장과 PC 이동

- Windows 설정은 `%LOCALAPPDATA%\wShell`, macOS 설정은 `~/Library/Application Support/wShell`에 저장합니다. Windows는 EXE를 이동하거나 교체해도 같은 사용자 설정을 사용합니다.
- `.wshell` 백업에는 **비밀번호와 개인키가 포함되지 않습니다.** 다른 PC에서는 비밀번호를 다시 입력하고 개인키를 별도로 준비해 경로를 지정하세요.
- Windows 저장 비밀번호는 현재 사용자 DPAPI, macOS는 동기화하지 않는 로그인 Keychain으로 보호합니다.
- 가져오기는 기존 세션·호스트 키를 덮어쓰지 않습니다. 새 폴더 정보가 포함된 백업은 해당 기능을 지원하는 Windows 빌드에서 복원하세요.
- 클라우드 자동 동기화는 제공하지 않습니다. [백업·이전 상세 안내](docs/usage.md)

## 지원 플랫폼

| 항목 | Windows 1.2.0 | macOS 0.11.0 |
| --- | --- | --- |
| SSH·탭·SFTP·분할·공통 입력 | 지원 | 지원 |
| 로컬 셸 | CMD / PowerShell | 시스템 로그인 셸 |
| 개인키 | PPK·OpenSSH·PEM 가져오기 | OpenSSH |
| 키 생성 | 자체 키 관리자 | 앱 내 터미널에서 OS ssh-keygen 실행 |
| 고급 PuTTY 설정·Serial 등 | 지원 | 미지원 |
| 동기화 대상 개별 선택·Zoom/Back | 지원 | 기존 분할 UI 유지 |
| 연결 폴더 정렬·탭 창 분리·SSH 실행 인자 | 지원 | Windows와 동일하게 지원하지 않음 |

플랫폼별 호스트 신뢰 형식이 달라 PC·플랫폼 이전 시 지문을 다시 확인해야 할 수 있습니다. 기존 macOS·iPad 산출물은 이후 Windows 변경을 포함하지 않습니다.

## 개발 환경

**작업 전 [AGENTS.md](AGENTS.md) → [개발 규칙](rules/development.md) → 해당 기능 규칙을 읽습니다.** PC가 바뀌어도 Git에 저장된 규칙을 공통 기준으로 사용합니다. 현재는 Windows 로컬 빌드·검증이 기본이며, macOS·iPad 빌드와 GitHub Actions 실행은 명시적인 요청이 있을 때만 수행합니다.

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

SSH/UI 검증은 Node.js 22 이상과 잠기지 않은 Windows 데스크톱이 필요합니다.

```powershell
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/integration.cjs
```

Linux x86_64 교차 빌드는 Python 3.12 이상과 호환 libstdc++가 필요합니다. **Windows 실행 검증은 별도로 필요합니다.**

```sh
python3 scripts/build-windows-linux.py --bootstrap
python3 -m unittest discover -s tests -p 'test_*.py'
git diff --check
```

[상세 빌드·기능별 검증·패키징](docs/development.md) · [빌드 문제 해결](docs/build-troubleshooting.md) · [공통 작업 및 문서 관리 규칙](rules/development.md#pc-간-작업과-규칙-관리)

## 라이선스

자체 코드와 브랜딩은 [MIT](LICENSE)입니다. PuTTY 기반 엔진은 공식 배포본이 아닌 수정 빌드입니다. 외부 구성요소와 라이선스는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), 앱의 **Tools → Open-source licenses**에서 확인할 수 있습니다.

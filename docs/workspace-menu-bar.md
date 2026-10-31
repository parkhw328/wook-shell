# Windows 메뉴바 사용과 검증

Windows 1.2.19부터 상단 작업 버튼을 메뉴바로 제공합니다. 배포 파일과 검증 범위는 [1.2.19 릴리스 노트](releases/1.2.19.md)를 확인하세요. macOS·iPad UI는 변경하지 않았습니다.

## 명령 위치

| 메뉴 | 기능 | 적용 대상 |
| --- | --- | --- |
| File | New connection, Command Prompt, PowerShell, Export/Import settings, Exit | 작업 공간·이 PC의 설정 |
| Host | New host, New folder, Connect, Open SFTP, Edit host, Host settings, Delete host | 탐색창에서 선택한 저장 호스트 |
| Session | Duplicate tab, Reconnect, Open Files, Settings, Save as host, Close tab | 현재 활성 탭 |
| View | Hosts panel, Find a connection, All tabs, 탭 전환, 1~4분할, Zoom, Sync keyboard, Command bar, Full screen, Color preview | 현재 작업 공간 |
| Tools | SSH key manager, Application settings, Open data folder | 이 PC의 사용자 데이터 |
| Help | Keyboard shortcuts, Open-source licenses, About wShell | 도움말·프로그램 정보 |

`Host → New host`는 선택한 폴더를 기본값으로 사용합니다. 선택한 저장 호스트가 없으면 접속·SFTP·편집·삭제가 비활성화됩니다. `Host settings`는 이때 새 연결 설정을 엽니다. 상태 표시줄은 선택한 호스트와 메뉴 명령의 적용 대상을 안내합니다.

`Session`의 항목은 탭 종류·연결 상태에 맞게 활성화됩니다. 기존 세션 도구 모음의 Files·Settings도 계속 활성 탭에 적용됩니다. 닫기·삭제·재접속의 기존 확인 절차와 설정 백업의 비밀번호 제외 정책을 유지합니다.

## 키보드 사용

- `F10`: 메뉴바로 이동합니다.
- `Alt+F/H/S/V/T/E`: File / Host / Session / View / Tools / Help를 엽니다.
- 방향키와 Enter: 메뉴를 탐색하고 명령을 실행합니다. 밑줄 친 문자로도 선택할 수 있습니다.
- Esc: 하위 메뉴를 닫고 메뉴바에서 다시 누르면 이전 입력 위치로 돌아갑니다.
- 기존 `Ctrl+Shift+T`, `Ctrl+Shift+P`, `Ctrl+Tab`, `Alt+1…9` 등 작업 공간 단축키를 유지합니다.

내장 SSH·로컬 터미널에서도 F10과 위 Alt 조합은 앱 메뉴를 조작합니다. 해당 키는 원격 프로그램으로 전달되지 않습니다. Ctrl/Shift와 조합한 키 및 AltGr 입력은 이 메뉴 단축키에 포함되지 않습니다.

## 구현과 검증

메뉴 상태·기존 명령 연결은 [main.cpp](../src/main.cpp), 네이티브 메뉴 표시와 접근성 이름은 [menu_bar.cpp](../src/menu_bar.cpp), 작업 공간과 터미널이 공유하는 메뉴 키 판별은 [menu_keys.h](../src/menu_keys.h)에 있습니다. Win32 메뉴가 마우스·키보드 탐색을 처리하며, 사용자 정의 그리기는 기존 글꼴·색상에 적용합니다. 접근성 이름은 Microsoft의 [owner-drawn 메뉴 지침](https://learn.microsoft.com/en-us/windows/win32/winauto/exposing-owner-drawn-menu-items)을 따릅니다.

Linux에서 Python 회귀 검사와 Windows 교차 빌드·정적 패키지 검사를 수행합니다. Windows에서는 다음 명령으로 격리 데이터와 루프백 서버만 사용해 메뉴 상태, 실제 키 입력·포커스 복귀와 화면을 확인합니다.

```powershell
node tests/ssh/connections-integration.cjs
node tests/ssh/focus-integration.cjs
node tests/ssh/message-integration.cjs
node tests/ssh/integration.cjs
```

`tests/workspace_smoke.inc`는 최소 크기 메뉴 배치·접근성 이름·호스트 상태별 활성 조건, `tests/focus_smoke.inc`는 내장 터미널의 F10·Alt+H·방향키·Esc와 취소 후 입력을 검사합니다. 테스트 화면에는 `ui-menu-bar.bmp`, `ui-host-menu.bmp`, `ui-navigation-minimum.bmp`가 포함됩니다. 실제 Windows 실행·다중 DPI 화면·스크린샷 확인은 Linux 빌드 통과와 별개입니다.

Linux 작업 환경이므로 위 Windows UI·SSH 검사와 화면 캡처 검토는 **미실행**입니다. 버전별 Python 회귀·교차 빌드·정적 검사 결과는 [릴리스 노트](releases/1.2.19.md)와 [배포 manifest](../release/1.2.19/manifest.json)에 기록합니다. GitHub Actions는 실행하지 않았습니다.

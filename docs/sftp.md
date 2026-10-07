# SFTP 파일 탐색과 전송

저장한 SSH 호스트를 선택하고 **SFTP**를 누르면 전용 탭이 열립니다. Windows의 연결된 SSH 탭에서는 **Files**, macOS에서는 **Tools → Open SFTP**도 사용할 수 있습니다. SFTP 연결은 터미널과 독립적이므로 파일을 전송하면서 명령을 실행할 수 있습니다.

## 파일 이동

![Windows SFTP 중앙 전송 버튼](../assets/screenshots/sftp.png)

1. 왼쪽 **LOCAL**, 오른쪽 **REMOTE** 목록에서 폴더를 더블 클릭하거나 경로를 입력하고 **Go**를 누릅니다. **↑**는 상위 폴더로 이동합니다.
2. Windows에서는 로컬 파일을 선택하고 **두 목록 사이의 `>` (Upload)**, 원격 파일을 선택하고 **`<` (Download)**를 누릅니다. 선택한 쪽의 전송 버튼이 주황색으로 켜지고 전송 중에는 양쪽 버튼이 비활성화됩니다. macOS는 기존 **Upload → / ← Download** 버튼을 사용합니다. Ctrl/Command 또는 Shift로 여러 파일을 선택할 수 있습니다.
3. 같은 이름의 파일이 있으면 대상 경로와 파일 목록을 확인하고 교체를 선택합니다. 진행 상태에 현재 파일, 순서, 바이트 수가 표시됩니다.
4. **Cancel**은 해당 SFTP 연결을 종료합니다. 다른 터미널 탭은 유지되며, **Reconnect**로 파일 탐색을 다시 시작할 수 있습니다.

**New folder / Rename / Delete**는 주황색 제목으로 표시된 선택한 쪽에 적용됩니다. Delete는 휴지통을 거치지 않습니다. 폴더 삭제는 빈 폴더에만 적용됩니다. Refresh로 외부에서 바뀐 목록을 갱신하세요.

## 인증과 저장

저장한 호스트의 사용자·포트·개인키·암호와 wShell 호스트 키 확인을 재사용합니다. 새 연결이므로 키 암호나 추가 인증을 다시 요청할 수 있습니다. 저장 비밀번호는 Windows DPAPI / macOS Keychain의 기존 정책을 그대로 따릅니다. **Export / Import에는 암호와 개인키가 포함되지 않습니다.**

Windows는 단일 EXE 내부 SSH 엔진, macOS는 OS OpenSSH의 SFTP subsystem을 사용합니다. 별도 `psftp.exe`, `sftp.exe` 설치나 추출이 필요하지 않습니다. 파일 전송 연결에는 터미널 PTY·포트 전달·세션 로깅을 적용하지 않습니다.

## 전송 보호와 현재 범위

- UTF-8 파일명, 여러 일반 파일의 순차 전송, 빈 파일, 폴더 탐색·생성·이름 변경을 지원합니다.
- 다운로드는 목적 폴더의 임시 파일에 쓴 뒤 교체합니다. 업로드도 원격 임시 파일에 전송하며, 기존 파일 교체에는 서버의 `posix-rename@openssh.com` 지원이 필요합니다. 지원하지 않는 서버에서는 다른 이름을 사용하세요.
- 중단된 업로드는 원격 폴더에 `.wshell-*.part`가 남을 수 있습니다. 재연결 후 해당 임시 파일만 확인하여 삭제하세요. 이미 완료된 파일은 취소해도 되돌리지 않습니다.
- 폴더 전체의 재귀 전송·동기화·재개·드래그 앤 드롭·원격 파일 편집·권한 변경은 아직 지원하지 않습니다. 폴더 안으로 이동해서 파일을 선택하세요. 심볼릭 링크는 자동으로 따라가거나 전송하지 않습니다.
- Windows에서도 이동할 수 있는 파일명만 전송합니다. 경로 구분자, 장치명, 끝의 점/공백 등은 거부합니다. 목록은 폴더당 100,000개까지이며 SFTP v3 서버가 필요합니다.

## 검증

`node tests/ssh/sftp-integration.cjs`는 독립 ssh2 루프백 서버에서 UTF-8 이름, 다중 패킷·짧은 읽기·응답 순서 변경, 전송 내용 일치, 폴더 작업, 덮어쓰기 거부와 취소를 확인합니다. `sftp-codec-tests`는 손상된 프레임·잘못된 ID·경로 순회와 확장 협상을 검사합니다. 두 플랫폼의 UI 테스트는 실제 SSH 키/저장 암호 연결과 업로드·다운로드를 실행합니다. 실제 사용자 서버에는 테스트 파일을 쓰지 않습니다.
## Hidden files

`Show hidden files` is **unchecked by default** in each SFTP tab. Check it to show dotfiles/dotfolders (such as `.env`, `.ssh`, `.config`) in both local and remote panes. Windows local files with the Hidden attribute and macOS local files marked hidden are also filtered. Unchecking immediately hides them again and clears selection; files are not deleted. Showing or hiding files does not change permissions or server settings.

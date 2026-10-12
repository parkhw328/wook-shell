# Windows Shift·Ctrl·Alt 방향키

Codex 같은 터미널 앱에서 Shift+왼쪽이 일반 왼쪽과 똑같이 처리되면
SSH 서버 설정뿐 아니라 터미널이 전송한 키 바이트를 확인한다.

## 원인과 변경

PuTTY 0.85의 기본 `Ctrl toggles app mode`는 Shift 정보를 전송하지 않는다.
또한 `xterm-style bitmap`에서도 애플리케이션 커서 모드(DECCKM)를 켜면
기존 인코더가 `ESC O D`를 먼저 선택하여 Shift 정보를 잃었다.

wShell은 방향키 수정키 기본값을 `xterm-style bitmap`으로 바꾸고,
수정키가 있을 때는 애플리케이션 커서 모드보다 수정키 인코딩을 우선한다.
Shift+왼쪽은 두 커서 모드 모두 `ESC [ 1 ; 2 D`를 전송한다.
일반 방향키의 애플리케이션 모드, VT52와 명시적으로 선택한 기존 모드는 유지한다.
[xterm 수정키 규격](https://invisible-island.net/xterm/manpage/xterm.html)을 따른다.

기존 저장 연결에도 새 기본값을 적용하도록 `WookShiftedArrowKeys`를 사용한다.
기존 `ShiftedArrowKeys` 선택은 새 설정에 자동 승계하지 않는다.
기존 동작이 필요한 연결은 업데이트 후 Host settings → Terminal → Keyboard의
방향키 수정키 항목에서 `Ctrl toggles app mode`를 다시 선택하고 저장한다.
이 변경은 Windows 입력 인코딩에 관한 것이며 Kitty 키보드 프로토콜 전체 지원은 아니다.

## 검증

```bash
python3 scripts/patch-putty.py
python3 -m unittest discover -s tests -p 'test_*.py'
python3 scripts/build-windows-linux.py
```

`test_arrow_keys.py`는 실제 패치된 인코더를 호스트 C 컴파일러로 컴파일하여
네 방향·수정키 조합·일반/애플리케이션 커서 모드의 출력 바이트를 확인한다.
Windows 키 이벤트나 SSH 전송 전체를 실행하는 검사는 아니다.

Windows에서는 기존 wShell 창을 모두 종료한 뒤 수정 EXE로 다시 연결한다.
별도 SSH 셸에서 `cat -v`를 실행하고 Shift+왼쪽, Enter를 누르면
`^[[1;2D`가 나와야 한다. Ctrl+C로 종료한다. 이후 Codex의 질문이 대기 중일 때
Shift+왼쪽으로 질문 입력이 열리는지 확인한다. 2026-10-12 사용자 보고로 수정 후 정상 동작을 확인했다. 이번 배포 EXE의 Windows 자동 실행 검사는 별도로 수행하지 않았다.

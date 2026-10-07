# 제품 요구사항

- 제품명은 **wShell**, 대상은 Windows 10 1903 이상/Windows 11 x64다. 저장소 이름 `wook-shell`은 유지한다.
- 설치 프로그램, WebView2, Electron, 별도 런타임 없이 `wShell.exe` 하나로 실행한다. 런타임 보조 EXE·폰트·자산을 추출하지 않는다.
- 네이티브 Win32 UI와 PuTTY 엔진을 사용하여 용량과 메모리 사용을 줄인다.
- Flexoki Dark와 번들 JetBrains Mono를 기본으로 사용한다. Termius의 비공개 자산은 사용하지 않는다.
- UI 글꼴은 JetBrains Mono Regular/Bold, 공통 크기는 보조 9pt·본문 11pt·섹션 13pt·제목 20pt로 통일한다. 개별 화면에서 임의 크기를 추가하지 않는다.
- 생성한 wShell 이미지 브랜딩을 앱 헤더와 README에 사용한다. 다중 해상도 ICO를 실행파일·작업 표시줄 아이콘으로 내장한다.
- ANSI, 256색, 24비트 True Color, UTF-8, 터미널 리사이즈를 지원한다.
- SSH, Telnet, Rlogin, Raw, Serial과 PuTTY의 고급 설정을 유지한다.
- 세션 저장·편집·검색, 새 탭, 탭 복제·전환·재정렬·닫기를 제공한다.
- 자체 키 관리에서 Ed25519/RSA 생성, OpenSSH/PPK 가져오기, PPK v3 암호화 저장과 공개키 내보내기를 제공한다.
- 암호를 자체 저장하지 않는다. 외부 Pageant·SSH 에이전트·연결 공유를 사용하지 않는다.
- 설정과 호스트 키는 실행 폴더의 `data/`에 저장한다. 기존 PuTTY 레지스트리를 변경하지 않는다.
- 설정 export/import는 개인키·암호를 포함하지 않고 기존 세션과 호스트 키를 덮어쓰지 않는다.
- 구현 완료와 실제 검증 완료를 구분하고, 미지원 항목을 문서화한다.

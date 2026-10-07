# 제품 요구사항

- 제품명은 Wook Shell, 대상은 Windows 10/11 x64다.
- 설치 프로그램, WebView2, Electron, 별도 런타임 설치 없이 ZIP을 풀어 실행한다.
- 네이티브 Win32 UI와 PuTTY 엔진을 사용하여 용량과 메모리 사용을 줄인다.
- Flexoki Dark와 번들 JetBrains Mono를 기본으로 사용한다. Termius의 비공개 자산은 사용하지 않는다.
- ANSI, 256색, 24비트 True Color, UTF-8, 터미널 리사이즈를 지원한다.
- SSH, Telnet, Rlogin, Raw, Serial과 PuTTY의 고급 설정을 유지한다.
- 세션 저장·편집·검색, 새 탭, 탭 복제·전환·재정렬·닫기를 제공한다.
- 암호를 자체 저장하지 않는다. 개인키와 Pageant 인증 및 호스트 키 검증은 PuTTY가 처리한다.
- 설정과 호스트 키는 실행 폴더의 `data/`에 저장한다. 기존 PuTTY 레지스트리를 변경하지 않는다.
- 구현 완료와 실제 검증 완료를 구분하고, 미지원 항목을 문서화한다.

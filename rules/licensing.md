# 라이선스 및 배포

- wShell의 자체 코드와 브랜딩 자산은 루트 `LICENSE`에 명시한 MIT 라이선스를 따른다.
- 외부 코드와 폰트에는 각 원저작자의 라이선스를 적용한다.
- PuTTY·SwiftTerm·Flexoki: MIT, JetBrains Mono: SIL Open Font License 1.1.
- 원문 라이선스와 저작권 고지를 `licenses/`에 보관하고 단일 배포 EXE의 리소스로 내장한다. 앱의 라이선스 화면에서 원문을 제공한다.
- 수정한 PuTTY는 wShell의 수정 빌드임을 명확히 알린다. 공식 PuTTY 배포본으로 표시하지 않는다.
- 정적으로 연결한 LLVM/MinGW 런타임의 라이선스도 배포에 포함한다.
- `THIRD_PARTY_NOTICES.md`에 출처, 버전, 수정 범위와 재현 방법을 기록한다.
- 인증서·개인키·암호·실제 접속 프로필은 Git이나 배포 ZIP에 포함하지 않는다.
- macOS는 OS의 OpenSSH·ssh-keygen을 사용하며 해당 바이너리를 재배포하지 않는다. 앱 내에 적용 라이선스를 포함한다.
- Developer ID 서명과 공증을 수행하지 않은 macOS 패키지는 ad-hoc 서명 상태를 배포 안내에 명시한다.

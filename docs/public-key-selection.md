# 공개키 파일로 접속 키 선택하기

이 기능은 Windows 1.2.18부터 제공됩니다. 배포 파일과 검증 범위는 [1.2.18 릴리스 노트](releases/1.2.18.md)를 확인하세요. macOS·iPad에는 적용하지 않았습니다.

SSH 공개키 인증은 서버에 등록한 공개키와 그에 대응하는 개인키를 사용합니다. `.pub`는 사용할 키를 식별하며, 실제 서명에는 로컬 개인키가 필요합니다. 외부 에이전트는 사용하지 않습니다. [OpenSSH 인증 설명](https://man.openbsd.org/ssh#AUTHENTICATION)

## 선택 방법

호스트의 `Authentication → Public key authentication`에서 Browse로 `.pub`를 선택하고 저장하거나 접속합니다. `Host settings → Connection/SSH/Auth/Credentials`와 키 관리자의 `Import & register…`에서도 선택할 수 있습니다. OpenSSH 한 줄 공개키와 RFC4716 공개키를 읽으며, 확장자와 주석이 달라도 실제 키 내용이 같으면 일치합니다.

1. 등록된 키 목록의 PPK를 먼저 검사합니다. 암호화된 PPK도 암호 입력 없이 식별하고 재사용합니다.
2. 같은 폴더의 PPK를 검사하고, 다음으로 암호 없는 OpenSSH·PEM·SSH.com 개인키를 읽어 비교합니다. 하위 폴더는 검색하지 않습니다.
3. 아직 일치하는 키가 없으면 파일명이 대응하는 암호화된 외부 형식의 개인키를 확인합니다. 호스트 화면에서는 해당 파일의 암호를 묻고, 키 관리자에서는 미리 입력한 Passphrase를 사용합니다.

| 공개키 이름 | 암호화된 개인키 이름 예시 |
| --- | --- |
| `id_ed25519.pub` | `id_ed25519` |
| `server.pub` | `server.pem`, `server.key`, `server.openssh` |
| `server.pem.pub` | `server.pem` |

다른 이름의 암호화된 개인키는 키 관리자로 먼저 등록하세요. 그 뒤에는 공개키 파일 이름과 관계없이 등록 목록에서 찾습니다. 일치하는 PPK가 여러 개면 경로를 대소문자 구분 없이 정렬한 첫 항목을 재사용합니다. 특정 사본을 원하면 해당 개인키를 직접 선택하세요.

일치하는 외부 형식의 개인키는 원래 암호를 유지한 PPK로 등록합니다. 호스트에는 최종 개인키 경로가 저장되므로 이후 공개키 파일을 옮겨도 재접속에 사용하지 않습니다. 같은 폴더의 기존 PPK를 선택했다면 그 PPK는 계속 원래 경로에 있어야 합니다. 등록·파일 권한·암호 정리 정책은 [SSH 키 요구사항](../rules/requirements/keys.md)을 따릅니다.

일치하는 개인키가 없거나 선택한 공개키가 손상되었으면 이유를 알리고 접속을 진행하지 않습니다. 암호 입력 취소·잘못된 암호·키 불일치로는 새 키를 등록하지 않습니다. 개인키가 없다면 `.pub`에서 복구할 수 없으므로 새 키 쌍을 만들고 서버에 새 공개키를 등록해야 합니다.

## 검증 방법

Linux에서는 다음 명령으로 실제 PuTTY 파서·암호 연산과 제품의 파일 검색·등록 결정 로직을 실행합니다. Windows UI·ACL·원자적 파일 작성 부분은 테스트 어댑터이며 Windows 실행 검증을 대체하지 않습니다.

```sh
python3 -m unittest discover -s tests -p test_public_key.py -v
python3 -m unittest discover -s tests -p 'test_*.py'
python3 scripts/build-windows-linux.py --jobs 4
git diff --check
```

Windows에서는 개발 빌드 후 다음 검사를 실행합니다. 테스트는 `build/` 아래에서 생성한 임시 키와 루프백 서버만 사용합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -EngineOnly
npm --prefix tests/ssh ci --ignore-scripts --omit=optional
node tests/ssh/key-import-integration.cjs
node tests/ssh/integration.cjs
node tests/ssh/sftp-integration.cjs
```

키 통합 검사는 공개키로 PEM RSA·암호화된 PEM RSA·OpenSSH Ed25519를 찾아 등록하고, PPK 재사용·RFC4716·주석 변경·잘못된 암호·취소·불일치·손상 입력을 확인합니다. 보호된 파일 ACL과 루프백 서버가 검증한 서명 5회도 확인합니다. 전체 UI 검사의 호스트 편집·키 관리자·Host Settings 캡처를 열어 새 문구와 파일 선택 흐름을 확인해야 합니다.

## 2026-10-31 점검 결과

- Linux 전체 Python 회귀 검사 54개 통과. 이 중 공개키 파싱·일치·검색·등록 결정 검사 14개를 추가했습니다. 같은 알고리즘의 다른 키 거부, 등록 우선순위, 암호화 유지, 취소와 실패 시 미등록, 기존 개인키 직접 선택도 검사했습니다.
- Windows 앱과 테스트 EXE 교차 컴파일, x64 시스템 DLL 의존성, 내장 폰트·라이선스·아이콘, 단일 EXE ZIP 정적 검사 통과. 개발 산출물은 `build/packages/1.2.17/windows-x64/`에 있습니다.
- 키 통합 스크립트의 Node.js 문법 검사, 문서 상대 링크 77개 확인과 `git diff --check` 통과.
- Windows 실제 SSH 서명·SFTP·UI·암호 입력창·ACL·CTest 실행은 Linux 환경이므로 미실행입니다. 위 Windows 검사를 잠기지 않은 데스크톱에서 실행하고 화면 캡처를 확인해야 합니다. macOS·iPad 빌드와 GitHub Actions는 실행하지 않았습니다.

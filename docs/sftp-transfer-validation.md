# SFTP 대용량 다운로드와 Explorer 전달 검증

## 보고와 변경 범위

2026-10-23 Windows에서 약 1.5GB 파일 다운로드 중 속도가 0으로 내려갔다가 회복하는 현상이 2~3회 발생하고, Explorer로 끌어 놓으면 Windows 파일 복사 진행창도 표시된다는 보고를 점검했다. 재교환 뒤 입력 재개와 64개 READ 요청 보충은 이미 적용되어 있다. 사용자 서버에서의 정지 원인은 아직 확정하지 못했다.

- 다운로드 임시 파일에 공간을 먼저 예약하여 수신 중 반복적인 디스크 할당을 줄인다. 공간 예약 미지원 파일 시스템은 기존 쓰기를 사용하고 실제 공간 부족·권한 오류는 전송 전에 반환한다. 기존 목적 파일 보호·취소 시 임시 파일 정리·마지막 flush를 유지한다. 디스크 캐시 지연이나 SSH 재교환 자체를 제거하는 변경은 아니다.
- 원격 드롭 데이터에 비동기 추출을 제공한다. Explorer가 드롭을 반환한 뒤 내용을 요청해도 콜백과 탭을 유지하고, 완료 통지까지 UI 메시지를 처리한다. 취소 결과는 추가 실패창으로 바꾸지 않는다.
- 파일 설명자의 `FD_PROGRESSUI`를 제거한다. Explorer 자체의 복사 진행창까지 숨기는 것은 아니다. 전체 파일을 임시 저장한 뒤 Explorer가 목적지에 복사하는 구조는 유지한다.

API 근거: [Windows 파일 공간 예약](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_allocation_info), [Shell 비동기 데이터 추출](https://learn.microsoft.com/en-us/windows/win32/api/shldisp/nn-shldisp-idataobjectasynccapability).

## Windows에서 실행할 검증

[개발 안내](development.md)에 따라 로컬 빌드와 SSH 테스트 의존성을 준비한 뒤 실행한다. 기존 공개 배포본 대신 이번 소스로 빌드한 `build/native` 실행파일을 사용한다.

```powershell
node tests/ssh/sftp-benchmark.cjs --large
node tests/ssh/sftp-benchmark.cjs --large --rekey
node tests/ssh/sftp-benchmark.cjs --rekey
node tests/ssh/drag-integration.cjs
node tests/ssh/sftp-integration.cjs
node tests/ssh/sftp-cancel-integration.cjs
```

`--large`는 1.5GiB+3 bytes 파일을 작은 버퍼로 생성하며 다운로드·재업로드의 크기와 SHA-256을 확인한다. 실행당 약 4.5GiB 이상의 디스크 공간이 필요하며 결과는 `build/sftp-benchmark-*`에 남는다. 기본 재교환 설정과 `--rekey`의 256MiB 한도를 각각 검사한다. 작은 fixture의 `--rekey`는 기존 256KiB 한도를 유지한다. 한 번의 대용량 왕복 제한 시간은 30분이다.

출력에는 최장 진행 지연과 1초 이상 정지한 바이트 위치, 500ms 이상 걸린 전송 파이프 읽기/쓰기·로컬 파일 쓰기·공간 예약·flush가 포함된다. 긴 호출의 진단은 테스트 빌드에만 포함하며 경로·인증정보를 기록하지 않는다. 서버 handshake 기록으로 반복 키 교환 완료 여부도 확인한다. 느린 호출이 없다고 사용자 환경의 정지가 없다고 단정하지 않는다.

드롭 자동 검사는 드롭 반환 후 지연된 내용 요청, 비동기 시작·종료, 진행창 강제 플래그 제거, 스트림 수명과 바이트 일치를 검사한다. 실제 Explorer와의 프로세스 간 COM 호출·복사창 표시는 수동 검증이 필요하다. 잠기지 않은 Windows에서 1.5GB 파일을 Explorer로 끌어 놓고, 다운로드 중 취소·여러 파일·동명 파일 확인·완료 후 해시 일치와 창 응답을 확인한다.

## 이번 환경의 검증 한계

2026-10-23 Linux에서 Python 회귀 테스트 40개, Windows x64 앱·테스트 EXE 교차 컴파일, EXE/ZIP 정적 검증, JavaScript 구문 검사와 `git diff --check`를 통과했다. 작업용 실행파일은 `build/packages/1.2.15/windows-x64/wShell.exe`이며 공개된 1.2.15 배포 파일은 변경하지 않았다.

Linux 호스트의 공간 예약 정책 검사는 0 bytes·1.5GiB·5GiB·uint64 초과 범위·미지원 파일 시스템·공간 부족·권한 오류를 모의 Win32 API로 검사한다. Windows 파일 시스템 성능 측정이 아니다. Windows SSH/UI/Explorer 실행과 실제 1.5GB 증상 재현은 이 환경에서 실행하지 못했다. 교차 빌드와 호스트 테스트 결과를 Windows 실행 검증으로 표시하지 않는다.

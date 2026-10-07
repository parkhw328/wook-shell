# iPadOS 26.6: Derpy와 무료 계정 서명 검토

2026-10-07 확인. 대상은 직접 만든 wShell, 개인 무료 Apple 계정, 유료 개발자 멤버십 없는 환경이다.

## Derpy로 자체 IPA를 설치할 수 있는가

제시된 [블로그](https://blog.naver.com/saucecompany_/224366161847)는 iTunes·iDescriptor 등으로 **App Store에서 내려받은 IPA**를 준비하는 절차다. [Derpy 1.0.1 릴리스](https://github.com/dr-sauce/derpy/releases/tag/1.0.1)도 지원 대상을 App Store에서 받은 IPA로 제한하고, 복호화된 IPA를 선택하면 iDescriptor로 안내한다고 설명한다. README의 “signed IPAs”라는 짧은 설명은 이 릴리스의 구체적인 조건과 함께 읽어야 한다.

개발자가 자신의 앱에 하는 코드 서명은 앱의 무결성과 서명 주체를 증명한다. 기기 설치에는 그 기기·앱·개발 팀을 허용하는 프로비저닝도 필요하다. 이것만으로 App Store 배포 패키지가 되지는 않는다. 따라서 **wShell의 unsigned IPA에 Personal Team 서명을 추가하는 작업으로는 위 Derpy 경로의 조건을 충족하지 못한다**. 이것은 제작자가 밝힌 입력 조건에 근거한 판단이며, Derpy 또는 iPadOS 26.6 실기기에서 wShell을 설치해 성공했다는 주장이 아니다.

## 무료 계정으로 가능한 경로

| 방법 | wShell 설치 | 유효기간과 준비물 |
| --- | --- | --- |
| Windows + Sideloadly + 개인 Apple 계정 | 로컬 서명 후 개인 iPad에 설치 가능 | 연결·신뢰한 기기와 계정 인증이 필요. 무료 프로비저닝은 7일 |
| Mac + Xcode Personal Team | 개발 서명 후 개인 iPad에 설치 가능 | 연결된 기기, Xcode 계정 로그인, 고유 Bundle ID. 7일 후 갱신 |
| Derpy 1.0.1 + 현재 wShell IPA | 지원 입력 조건에 해당하지 않음 | App Store에서 받은 IPA 대상. 자체 개발 서명의 대체 수단이 아님 |

[Apple 공식 문서](https://developer.apple.com/help/account/basics/about-your-developer-account)에 따르면 무료 Personal Team의 프로비저닝은 발급 7일 후 만료된다. 기기당 설치 앱 3개 등 제한도 있다. 팀원들에게 동일한 unsigned 원본을 전달할 수 있지만, 각자의 개인 계정과 기기에 맞는 서명·설치가 필요하다. 한 번 서명한 파일을 모두에게 영구 배포하는 경로로 사용할 수 없다.

## 매주 수동 갱신을 줄이는 방법

[Sideloadly 공식 사이트](https://sideloadly.io/)는 무료 계정, Windows, iOS 26+ 및 자동 갱신을 지원한다고 안내한다. 다음은 이 도구를 사용할 때의 절차이며, 해당 iPadOS 26.6 기기에 대한 설치 검증은 아직 남아 있다.

1. 공식 사이트에서 Windows용 Sideloadly와 현재 안내된 Apple 드라이버 필수 구성요소를 준비한다.
2. iPad를 USB로 연결하고 기기에서 PC를 신뢰한다.
3. `dist/ipad/0.1.0/wshell-ipad-0.1.0-unsigned.ipa`를 선택한다.
4. 본인이 도구에 직접 Apple 계정과 인증 코드를 입력해 서명·설치한다. 채팅이나 GitHub에 자격 증명을 제공할 필요가 없다.
5. 요구되는 경우 iPad의 개발자 모드와 개발자 프로파일 신뢰를 활성화한다.
6. Sideloadly의 자동 갱신을 켠다. 최초 USB 설치 후 Wi-Fi 사용 시 PC와 iPad를 같은 네트워크에 두고, 갱신 데몬이 실행되는 PC에서 주기적으로 기기에 접근할 수 있게 한다.

자동 갱신은 만료 전에 다시 서명하는 작업을 자동화한다. **7일 제한을 제거하지 않는다.** PC가 장기간 꺼져 있거나 기기에 접근하지 못하면 만료되어 다시 갱신해야 한다. 계정과 Bundle ID를 유지하고 설정을 미리 내보낸다. 백업에는 개인키·암호가 없으므로 원본 키를 별도로 보관한다.

## 저장소에 있는 빌드 경로와 제공 파일

- 규칙: [`rules/ipad.md`](../rules/ipad.md)
- 워크플로: [`.github/workflows/ipad.yml`](../.github/workflows/ipad.yml), 표시 이름 **iPad personal preview**
- 빌드: [`scripts/build-ios.py`](../scripts/build-ios.py), `macos-15`에서 시뮬레이터와 arm64 기기 앱을 빌드
- 검증: `node tests/ssh/ios-integration.cjs`로 SSH·SFTP·Keychain을 시뮬레이터에서 확인
- 산출물: Actions의 `wshell-ipad-personal-unsigned` 아티팩트 → `dist/ipad/<version>/`에 IPA와 SHA-256 파일

워크플로는 `CODE_SIGNING_ALLOWED=NO`로 빌드한다. 현재 IPA에는 `embedded.mobileprovision`이 없다. Diawi의 `4001009: missing embedded mobileprovision`은 이 파일이 기기용으로 서명·프로비저닝되지 않았기 때문에 발생한다. Diawi에 업로드하는 작업 자체가 서명을 만들어 주지는 않는다.

현재 환경에는 사용자의 기기에 맞는 프로비저닝과 서명 키가 제공되어 있지 않으므로, **설치 가능한 서명 완료 IPA는 생성하지 않았다**. 검증한 unsigned 원본을 제공하고, 무료 계정으로 기기에 설치하는 마지막 서명 단계는 위의 로컬 도구에서 수행한다. 유료 멤버십이나 App Store 게시를 전제로 하지 않는다.

# iPad test archive

iPad 작업은 잠정 보류 중입니다. 나중에 테스트를 재개할 때 참고하도록 기존 미서명 빌드와 검증 정보를 보관합니다. 현재 배포용 다운로드가 아닙니다.

- [0.1.0 unsigned IPA](0.1.0/wshell-ipad-0.1.0-unsigned.ipa)
- [SHA-256](0.1.0/wshell-ipad-0.1.0-unsigned.ipa.sha256)
- [기존 manifest](0.1.0/manifest.json)
- [빌드 안내](../../docs/ipad.md) · [개인 서명 안내](../../docs/ipad-signing.md)

설치하려면 별도 Apple 계정 서명이 필요합니다. iPad 빌드는 명시적으로 재개 요청을 받을 때만 수행합니다. `python scripts/index-dist.py --catalog-only`는 이 보관본의 해시와 ZIP 무결성도 확인합니다.

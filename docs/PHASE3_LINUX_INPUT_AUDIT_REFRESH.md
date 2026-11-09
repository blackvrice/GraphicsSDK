# Linux 설치 패키지와 XCB 입력 감사 재검증

Linux 설치 패키지와 XCB 표면 CI는 GNU/Ninja가 실제로 읽은
`src/foundation/DeviceGeneration.hpp`를 감사하는 단계에서 실패했다. 이 헤더는
SDK 내부에서 백엔드 간 장치 세대 번호를 관리한다. [이전 패키지 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42546284373) ·
[이전 XCB CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42545907614)

공통 [감사 정책](../licenses/linux-vulkan-policy.json)의 SDK 자체 입력에 이 경로를
추가했다. 소비자는 옮겨진 공개 설치 헤더와 복사된 소비자 소스로 구성하며, 패키지
감사는 정확한 파일 목록과 알 수 없는 입력의 거부를 계속 검사한다. XCB 워크플로는
공통 감사 정책과 공통 입력 분류기 수정에도 실행된다.
[수정 커밋](https://github.com/blackvrice/GraphicsSDK/commit/d9aeb4c)

## 현재 소스 검증

- **설치 패키지.** Linux CI가 SDK 컴파일 6개, 설치본을 옮긴 뒤 저장소 밖에서
  실행한 소비자 9개, lavapipe Vulkan 검사 2,670개와 정확히 20개 파일의 검증용
  ZIP을 통과했다. ZIP의 파일별 해시와 감사 목록을 다시 대조했다. 세대 헤더는
  SDK 입력의 `own`으로 확인했다. 외부 소비자의 컴파일에는 공개 헤더 13개의
  설치 목록을 적용한다. [설치 패키지 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42590797537)
- **XCB.** Xvfb/lavapipe에서 일반 표면, 제어 오류, headless, XCB 비활성의
  8개 사례와 205개 검사가 통과했다. XCB ON/OFF의 실제 입력에서 세대 헤더의
  해시가 Git 원본 및 설치 패키지 감사와 일치했다. 두 설치본은 각각 20개 파일이며,
  XCB 헤더와 링크 참조는 SDK 내부 구현 범위로 분리했다. [XCB CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42590797484)

로컬 Python 감사·XCB 테스트는 총 19개가 통과했다. 내려받은 CI 원자료의 SHA-256은
아래와 같다.

```text
Package receipt   5fb11057530c6fc402d8b3bdaaaa278e42d66a5ea3cee119d6ea43243588fed9
Package ZIP       e3cb8a396b7f5d47ba1da068a88feabf0b24f712442bb3245b4c991ef9535020
XCB receipt       3b958a89c019ba321ab365c1fda8d5464c3de52765f6f4104b79489610918401
XCB input audit   208184fc6a1312dc85374cf2a21c09fd731cd96aeaff866eef4c161c1fc34641
```

## 재현 범위

[설치 패키지 재현 절차](PHASE3_LINUX_PACKAGE_REPORT.md)와
[XCB ON/OFF 워크플로](../.github/workflows/linux-xcb-surface.yml)를 사용한다.
이전 재생 도구는 당시의 SDK fingerprint를 확인한다. 현재 소스의 결과는 이 보고서의
새 CI 기록을 사용한다. 실행 환경은 Linux lavapipe와 Xvfb이며, 실제 Linux GPU와
사람이 보는 표시의 수용은 각각 `P3-LNX-15`와 `P3-WSI-08`의 근거를 따른다.

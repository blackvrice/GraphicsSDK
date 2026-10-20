# Sandbox 읽기 전용 조사

조사일: 2026-10-20. 기준 커밋: `afe563557367a0f2b5cef88488708da36f578b70`.
[원본 트리](https://github.com/blackvrice/Sandbox/tree/afe563557367a0f2b5cef88488708da36f578b70).

원격 GitHub GET으로 파일을 읽었다. Sandbox의 작업 사본, 소스, 빌드, 브랜치,
Git index, 의존성 및 개발 상태를 변경하는 명령을 실행하지 않았다.
참고 사본은 GraphicsSDK의 무시 디렉터리 `.deps/references/sandbox`에만 있다.
Sandbox 코드를 GraphicsSDK에 복사하거나 Sandbox 연동을 구현하지 않는다.

## 확인한 설계

* [RenderDevice.hpp](https://github.com/blackvrice/Sandbox/blob/afe563557367a0f2b5cef88488708da36f578b70/render/rhi/RenderDevice.hpp):
  장치/명령 목록/큐/스왑체인 인터페이스, 명시적 배리어, 프레임 슬롯,
  타임스탬프, 플랫폼 Window 의존성이 있다. 생성·파괴·기록의 단일 렌더 스레드 계약이다.
* [HandlePool.hpp](https://github.com/blackvrice/Sandbox/blob/afe563557367a0f2b5cef88488708da36f578b70/render/rhi/HandlePool.hpp):
  인덱스+세대 핸들로 오래된 리소스 참조를 거부한다. GraphicsSDK에서는 장치 간
  핸들 혼용과 세대 카운터 고갈까지 별도 검증할 필요가 있다.
* [DeferredDestruction.hpp](https://github.com/blackvrice/Sandbox/blob/afe563557367a0f2b5cef88488708da36f578b70/render/rhi/DeferredDestruction.hpp):
  완료 펜스까지 실제 객체 파괴를 미룬다. 마지막 제출 펜스만으로는 아직 제출하지 않은
  명령 목록의 참조를 보호하지 못하므로 새 SDK의 소유권 계약은 이 경우도 다룬다.
* [UploadRing.hpp](https://github.com/blackvrice/Sandbox/blob/afe563557367a0f2b5cef88488708da36f578b70/render/rhi/UploadRing.hpp):
  정렬·랩어라운드 낭비와 프레임 펜스에 따른 회수량을 추적한다.
* `Dx12Device.cpp`, `VulkanDevice.cpp`, `MetalDevice.mm`, `RhiTypes.hpp`를 읽어
  백엔드 분리, 포맷, 메모리 위치와 프레임 수명 정책을 참고했다.

## 독립 SDK에 적용할 판단

플랫폼 Window, 로그 시스템, 에셋 로더, 게임 모듈에 의존하지 않는 공개 API를
별도로 작성한다. 네이티브 윈도우 표면은 SDK 자체의 명세로 다룬다.
명령 기록 전/후, 제출 전/후, 펜스 완료 전/후의 수명을 테스트한다.
설계 참고와 소스 재사용은 구분한다. 이번 조사에서 소스 재사용은 없다.
Sandbox의 전체 테스트 또는 실제 GPU 동작을 검증한 것으로 간주하지 않는다.

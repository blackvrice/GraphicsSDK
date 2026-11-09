# P5-SHD-07 장치별 그래픽스 파이프라인 캐시

`Device::createCachedGraphicsPipeline`은 D3D12, Vulkan, Metal에 공통으로 제공하는
선택형 API다. `CachedGraphicsPipeline::hit`가 참이면 이 장치에서 이전에 생성한 동일
파이프라인을 반환한다. 미스에서는 백엔드의 기존 생성·검증 경로를 사용한다.

```cpp
auto cached = device.createCachedGraphicsPipeline(desc);
if (!cached) {
    return std::unexpected(cached.error());
}
return commands.setPipeline(cached->pipeline);
```

키는 `GraphicsPipelineDesc` 전체를 값으로 비교한다. 셰이더의 형식·단계·바이트, 정점
버퍼와 속성, 리소스와 정적 샘플러, attachment 형식, 깊이와 블렌드 상태가 포함된다.
벡터 순서와 선택 필드의 유무도 키에 반영된다. 명시적으로 지정한 기본값은 생략된 값과
별도 키다. 셰이더 바이트와 vertex semantic 문자열 합계가 1MiB를 넘으면 정상 생성 경로를
사용하고 `hit=false`로 반환한다.

각 장치가 최근 사용한 최대 8개 항목과 파이프라인 소유권을 보관한다. 적중하면 최근
사용 순서가 갱신되며, 초과하면 가장 오래 사용하지 않은 항목부터 내보낸다. 실패한 생성은
항목을 추가하거나 제거하지 않는다. `clearGraphicsPipelineCache`는 캐시의 소유권을
해제한다. 호출자가 보유한 파이프라인과 이미 기록된 작업은 기존 수명 규칙을 따른다.

두 캐시 API는 장치 생성 스레드에서 실행하며 장치 상태를 먼저 확인한다. 손실된 장치에서는
기존 파이프라인으로 적중하더라도 `DeviceLost`를 반환하고, 복구한 장치는 빈 캐시로
시작한다. 캐시를 사용한 장치는 캐시가 소유한 파이프라인을 자원 통계에 포함할 수 있다.
기존 `createGraphicsPipeline`은 독립적인 생성 경로다. 공개 `Device` 레이아웃이 변경되므로
소비자는 헤더와 라이브러리를 함께 재빌드한다.

캐시 범위는 프로세스 안에서 같은 장치의 SDK 파이프라인 객체를 재사용하는 것이다.
드라이버 캐시 파일, Vulkan `VkPipelineCache`, D3D12 파이프라인 라이브러리와는 별도다.

## 재현

각 플랫폼의 설치 소비자 실행은 같은 파이프라인의 적중, 셰이더·attachment 레이아웃·
장치 변경에 따른 미스, 잘못된 셰이더 단계 재시도, 스레드 제한, 비우기 및 LRU 퇴출을
검사한다. 캐시를 비운 뒤에도 호출자가 보유한 적중 파이프라인으로 참조 이미지를 그린다.
Vulkan 장치 복구 테스트는 손실 전 적중과 손실 후 거부, 복구 세대의 첫 미스를 검사한다.

구현 커밋 `b6fff0602575ed3e5bec140feee80b0a7819b599`에서 WARP, lavapipe,
GitHub macOS의 설치 소비자를 실행했다. 각 소비자는 캐시 적중 파이프라인으로
13×7 RGBA8 참조 이미지의 364바이트를 확인한다. [D3D12 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42545907709) ·
[Vulkan CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42545907776) ·
[Metal ARM64·Intel CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/42545907660)

로컬 Linux lavapipe 복구 검사는 합성 장치 손실 4회와 재구성 이미지 8개를 통과했다.
종료 시 관측 레이어의 활성 인스턴스와 장치가 모두 0개였다.

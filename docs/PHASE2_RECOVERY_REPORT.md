# Phase 2 장치 손실 복구

2026-10-21. 손실 상태 확인과 원인 보존, 명시적 native 세대 정리·재생성, native 장치 제거 기반의
복구 회귀 검증을 구현했다. **Phase 2 전체 완료는 아니다.** 장시간 메모리 stress, 사용자 조작을
포함하는 presentation, 예상치 못한 driver TDR/물리 hot-unplug acceptance는 남아 있다.

## 구현과 계약

- `Device::checkStatus()`: native 제거 원인을 ErrorCode::DeviceLost/nativeCode로 저장.
- `recover()`: 손실된 세대의 native objects/pending batches를 정리하고 새 Device 반환.
- Retirable 객체의 weak registry. 퇴역 시 전체 strong snapshot을 먼저 고정하여 안전하게 해제.
- buffer/texture/RTV/DSV, pipeline/root, command allocator/list/descriptors/query/readback,
  swapchain/backbuffer 및 pending native 참조를 모두 정리. GPU 완료를 기다리지 않는다.
- 이전 wrapper의 CPU metadata는 유지하되 조작은 DeviceLost, 새 세대에서의 사용은 WrongDevice.
- 정상 장치의 recover와 성공한 이전 세대의 재복구를 거부. 재생성 실패 뒤에는 재시도 가능.
- 같은 hardware/WARP 선택 정책을 유지. 자원 내용은 애플리케이션 CPU 데이터에서 다시 구축.
- native API 실패에서도 실제 제거 원인을 확인해 보존. command recording은 관찰한 손실을
  매 draw의 driver polling 없이 거부하며, 비동기 손실은 상태 확인 지점에서 관찰한다.
- `enableDeviceLossTesting` 기본 false와 opt-in `removeForTesting()`, capability 두 항목.

`removeForTesting()`은 mock 오류가 아니라 Microsoft의
[ID3D12Device5::RemoveDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice)를 호출한다.
제거 상태와 fence sentinel은 [GetDeviceRemovedReason 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason)을 따른다.
무한 GPU 작업, driver TDR 유발, registry/policy 변경은 사용하지 않았다. 제거는 각 독립 검증
process의 native 장치에만 명시적으로 요청했다. 이 API의 검증 환경은 Windows build 20348 이상이다.

D3D12 native 장치는 [process/adapter당 singleton](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice)이다.
따라서 같은 native 장치를 다른 SDK Device/외부 owner가 보유하면 재생성이 실패할 수 있다.
recover는 자기 세대만 정리하며 다른 owner를 대신 폐기하지 않는다. 다른 owner 해제 후 재시도한다.
전체 공개 계약과 애플리케이션 복구 흐름은 [공개 API](PUBLIC_API.md)에 있다.

## 실제 검증

| 검사 | 결과 |
|---|---|
| SDK CTest | 19/19, skip 없음 |
| Windows D3D12 OFF | CPU CTest 4/4 |
| hardware / debug+GBV / WARP | 각각 native 제거·복구 4회, 제거 전후 전체 이미지 8개 |
| 복구 assertions | hardware/WARP 각각 24,742, debug+GBV 24,751 |
| native 원인 | 모든 기록된 제거에서 -2005270523, DXGI_ERROR_DEVICE_REMOVED |
| GPU validation | 명시적 제거 직전 및 재생성한 장치의 ERROR/CORRUPTION 0 |
| 공통 benchmark correctness | 최신 core를 연결해 72/72 |
| Python | 56/56 |
| 설치 consumer | 기본 opt-out/정상 복구 거부와 기존 1,024-pixel 렌더링 통과 |
| 설치 recovery consumer | hardware/WARP 각각 native 제거 전후 2,048 pixels 일치 |
| 패키지·의존성 | 새 SDK ZIP, SDK/common benchmark inventory 및 재검사 보존 |

각 recovery test는 MRT + D32 + constant buffer pipeline을 CPU source에서 만들고 32×24
화면 전체를 읽는다. 4회 복구의 전후 8개 이미지, 총 6,144 pixels/24,576 RGBA bytes가 CPU
적색·녹색 참조와 정확히 일치했다. 제출 후 아직 wait하지 않은 command와 별도 ended command,
render pass가 열린 command, texture/pipeline/buffer/fence/swapchain/backbuffer wrapper를
보관한 상태에서 제거한다. 제거 당시 GPU가 반드시 미완료였다는 시간적 보장은 주장하지 않는다.

손실된 세대의 생성/CPU 접근/기록/제출/대기/표시/resize를 거부하고 같은 native reason을
보존함을 확인한다. 복구 뒤에도 이전 핸들·fence는 새 장치에 사용할 수 없다. 한 사이클에서는
의도적으로 두 번째 native owner를 남겨 첫 recover가 실패하게 한 뒤, owner 해제 후 같은
lost Device에서 재시도해 성공했다. 정상 상태와 다른 thread의 잘못된 복구·제거도 거부한다.

각 새 세대에서 shader bytecode를 재사용해 pipeline과 GPU data를 재구축하고 전체 이미지를
확인한다. 같은 숨겨진 HWND에 새 swapchain을 만들고 present/resize/present를 검사한다.
이전 네 세대의 wrapper를 끝까지 보관한 뒤 한꺼번에 파괴해도 마지막 정상 세대가 유지된다.
weak registration 정리를 위한 Upload buffer 생성·해제를 세대당 600회 수행했다. 이는 장시간
memory peak/누수 acceptance가 아닌 bounded churn 회귀 검사다.

의도적 native 제거 이후 이전 장치의 info queue에 생기는 제거 진단을 0이라고 주장하지 않는다.
테스트는 제거 전과 fresh device에서 예상치 못한 ERROR/CORRUPTION이 없는지 검사하고,
제거 후 이전 API는 DeviceLost를 반환하도록 검증한다. 정상 상태로 위장하거나 진단을 지우지 않는다.

[원본 로그](../benchmarks/reports/validation/2026-10-21-phase2-recovery/)와
[재현 소스·artifact 해시](../benchmarks/reports/2026-10-21-phase2-recovery/evidence.json)를 보존한다.
설치 consumer는 설치된 headers/library/CMake export만 사용한다. recovery consumer 두 실행은
별도 process이며 예전 upload wrapper도 보관한 채 native 재생성과 전체 렌더링을 검사한다.

## 패키지와 남은 범위

새 로컬 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-recovery.zip`이다. 최초 SDK,
vertex-input/bindings/depth-mrt ZIP과 과거 source archive·측정·inventory를 덮어쓰지 않았다.
virtual 함수와 DeviceDesc/capability가 확장되어 0.0.1 개발 API consumer는 함께 재빌드해야 한다.

신규 외부 dependency와 competitor 수정은 없다. 새 감사 폴더는
`licenses/generated/windows-sdk-recovery-release`,
`licenses/generated/windows-benchmarks-recovery-release`다. compiler-read 입력은 각각
19개/자체 구성요소 1개와 977개/22개다. 새 ZIP과 두 inventory의 `--check`가 통과했다.
이는 build/package 입력 확인이며 포괄적인 재배포 법적 승인을 뜻하지 않는다.

새 성능 캠페인은 실행하지 않았다. 자원 추적 비용의 성능 개선도 주장하지 않는다. 이전 수치는
당시 source fingerprint의 역사 자료다. Vulkan/Metal/native Linux, remote CI, driver TDR/
물리 hot-unplug 및 adapter 변경 시 실제 복귀, 장시간 메모리 안정성과 interactive presentation은
이번 완료 범위 밖이다. Native RemoveDevice와 숨겨진 창의 재표시 검증으로 이 항목들을 완료 처리하지 않았다.

# Phase 3 — Vulkan Win32 화면 출력

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

기존 Surface/SwapChain API에 실제 Win32 Vulkan surface·swapchain·acquire·present·resize를
연결했다. 공개 API 형태/ABI 변경 없이 SDK와 설치 소비자를 재빌드했다. **출력 자원 완료 펜스가
있는 장치만 지원한다. 현재 NVIDIA GTX 1660 SUPER에서 실제 출력 경로를 검증했고 Intel UHD
Graphics 770의 현재 드라이버는 해당 기능이 없어 명시적 Unsupported를 검증했다.** Intel의
기존 여덟 offscreen GPU suite는 계속 실행한다. Phase 3 전체 완료나 성능 우위를 뜻하지 않는다.

## 구현과 계약

Instance는 surface/win32_surface/get_surface_capabilities2/surface_maintenance1을 조회하고,
device는 swapchain/swapchain_maintenance1 확장과 swapchainMaintenance1 feature를 확인한다.
caps.swapChain은 이 기능들이 있을 때만 true다. 실제 생성에서는 선택 graphics queue의 해당
surface 지원도 확인한다. 다른 adapter/queue로 대체하지 않는다. Linux WSI는 Unsupported다.
창은 동일 process·생성 thread에 속한 유효 HWND여야 한다. 창은 호출자 소유다.

RGBA8 UNORM + SRGB nonlinear color space, color attachment/transfer-src/transfer-dst usage를
요구한다. BGRA를 RGBA로 가장하지 않는다. VSync는 FIFO, 비동기 출력은 IMMEDIATE 우선,
MAILBOX 차선이고 둘 다 없으면 Unsupported다. 요청 buffers는 2..4이며 surface 최소 개수와
실제 반환 image count를 따른다. 순환 순서나 매번 다른 index를 보장하지 않는다.

acquire는 1초 native 획득 제한을 사용하고 성공 시 acquisition fence를 host에서 기다린다.
현재 acquisition이 있으면 같은 wrapper를 반환한다. 처음 획득한 image는 내부 일회성 명령으로
UNDEFINED→PRESENT layout을 설정해 그리지 않은 present도 유효하게 한다. 초기 texel 값은
정의하지 않으며 초기 Load를 허용하지 않는다. 버퍼는 RenderTarget, CopySource,
CopyDestination, Present 전환을 지원한다. ShaderRead는 Unsupported, offscreen Present는
InvalidArgument다. public footprint는 256-byte pitch를 쓰는 staging layout이다.

present는 제출된 Present 상태를 요구한다. 기존 queue 명령 뒤의 signal 제출과 native
present wait semaphore를 연결한다. 세마포어는 실제 image index별로 소유한다. 매 출력에
VK_EXT_swapchain_maintenance1의 완료 펜스를 지정하고 완료 뒤 반환하므로, 이후 획득 없이
swapchain·semaphore를 파괴할 때도 native 자원 수명을 보장한다. 이 펜스는 모니터 표시 완료를
보장하지 않는다. Host 획득/출력 대기가 있는 초기 정확성 구현이며 frame overlap·지연·처리량
최적화 또는 성능 baseline은 없다.

기본 Vulkan에서 WaitIdle만으로 출력 자원 해제를 보장할 수 없는 점과 image별 세마포어
규칙은 [Khronos guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)를
검토했다. 완료 펜스의 자원 해제 계약은
[native specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)을
따른다. 구형 드라이버에 불완전한 teardown fallback을 제공하지 않는다. Intel 미지원은 이
기능 조회 결과이며 이전 Intel AV의 원인에 관한 판단이 아니다.

출력은 매번 image version을 증가시킨다. 현재 획득되지 않았거나 chain/window가 사라진
backbuffer는 기록과 submit에서 거부한다. 같은 image가 나중에 다시 획득되어도 이전 명령은
stale이다. 제출 recording이 image view·framebuffer·swapchain 부모를 보유한다. 외부 image는
DestroyImage/FreeMemory하지 않는다. resourceStatistics는 외부 image 수를 포함하지만 조회하지
않은 native allocation 크기를 만들어 더하지 않는다. 장치 retire는 recording/framebuffer/view,
swapchain/semaphore 순서로 해제한 뒤 device를 파괴한다. 남은 wrapper는 CPU 설명만 제공한다.

Win32 surface가 고정 currentExtent를 제공하면 요청 크기가 native client extent와 정확히
같아야 한다. SDK가 HWND 크기를 바꾸지 않는다. 호출자가 창을 먼저 변경하고 backbuffer
wrapper·recording을 해제한 뒤 resize한다. 잘못된 크기/남은 참조의 거부는 기존 chain을
보존한다. native 재생성을 시작하면 Vulkan 규약상 oldSwapchain은 실패 시에도 retire되므로
그 단계의 실패는 명시적으로 반환하고 resize 재시도가 필요하다. OUT_OF_DATE/SURFACE_LOST는
native code를 보존한 InvalidState이며 재생성 또는 새 surface/chain이 필요하다. SUBOPTIMAL은
성공으로 처리한다. 손실은 기존 DeviceLost 경로를 따른다.

## 최종 검증

Pinned runtime DLL/JSON/lock hash 확인, 상속 Vulkan/VVL 설정 제거, 명시적 layer 경로,
빈 설정과 implicit layer 비활성화는 child에만 적용했다. 시스템·드라이버 설정 변경은 없다.
정상 SDK core/synchronization validation ERROR는 0이다.

| Windows 구성 | 결과 |
|---|---|
| 통합 required validation | 선택 54/54, skip 없음 (등록 57, 보류 메모리 3 제외) |
| Vulkan-only required validation | 29/29, skip 없음 |
| 두 backend OFF | CPU 10/10, skip 없음 |
| 설치 SDK GPU suite 9개 | 기존 8개는 NVIDIA/Intel, 새 출력은 NVIDIA 실행·Intel 미지원 계약 |
| Python 회귀 | 208/208 |

최초 통합 회귀는 52/54였다. 기존 texture suite 두 항목이 여전히 offscreen Present에
"미구현 Unsupported"를 기대해 실패했다. 새 계약의 InvalidArgument 기대값으로 수정하고
전체 최종 검증을 다시 실행했다. [최초 로그와 입력](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-presentation/initial-regression/)을
보존하며 실패를 숨기거나 보류 메모리 테스트를 재시도하지 않았다.

새 출력 suite는 실행당 **721 assertion / 145 presents / 132 full images / 60 draws /
7 resizes / 35,918,112 texel bytes / shown window 1개**를 검증한다. 요청 2/3/4-buffer ×
VSync on/off, 두 독립 창, 홀수 크기와 padding, clear/depth-tested draw, changing upload,
사용자 fence 대기 없는 present, dropped recording, 제출 중 chain 파괴, retained wrapper,
stale 재제출, window 파괴, foreign device, thread와 장치 retire를 포함한다. 지원 장치가
하나도 없으면 suite는 실패하므로 모든 장치를 건너뛰고 통과하지 않는다.

통합·Vulkan-only는 driver-only/required validation을 각각 실행하고 설치 소비자는 required
validation을 반복한다. 최종 새 suite 5회에서 725 presents / 660 images / 300 draws /
179,590,560 texel bytes를 확인하며 그중 required validation 3회는 435 presents /
396 images / 107,754,336 bytes다. 생성한 창을 프로그램으로 표시한 검사와 readback은
사람의 화면 관찰, compositor/monitor pixel, 최소화·cross-monitor DPI, TDR/hot-unplug
수용을 대체하지 않는다. 일반 경쟁 성능·Linux·장시간 메모리 수용도 포함하지 않는다.

기존 181/61/43 CPU SPIR-V assertion, 3,775 timestamp math assertion, 여덟 GPU suite,
native validation counter/error controls와 전체 Python 회귀를 재실행했다. Unknown/extra
option은 GPU 이전 실패, missing layer는 명시적 실패다. Shader fixture 4개와 local tool/runtime은
기존 SHA receipt를 재사용했으며 새 shader 조립/semantic validation 실행 주장은 없다.

## 배포와 보존

새 roots는 out/vulkan-presentation, -install, -consumer, -only, -portable이다. 실제
compiler-read closure 83 files / components 2 / compiler logs 36 / linker logs 35를
별도 [policy](../licenses/vulkan-presentation-policy.json)로 감사한다. 새 private implementation과
GPU test 두 자체 파일, 기존 pinned checkout의 Apache-2.0 Win32 header 한 개를 추가했다.
User32는 Windows SDK 시스템 prerequisite다. public Vulkan header/import lib나 vendor
source·loader·ICD·validation DLL을 번들하지 않는다.

새 SDK는 out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-presentation.zip이다. 정확한 install
payload와 notices를 검사하고 notices 누락/library 변경/추가 library를 거부한다. 기존 SDK
ZIP 21개와 과거 evidence·D3D12 구현·memory policy를 hash로 보존한다.

[결과](../benchmarks/reports/2026-10-23-phase3-vulkan-presentation/summary.json),
[증거](../benchmarks/reports/2026-10-23-phase3-vulkan-presentation/evidence.json),
[실행·동결·검증 scripts](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-presentation/verification-scripts/run.py)에
명령·환경·원본 로그·빌드 입력·설치 결과와 source ZIP receipt를 연결했다. 통합 실행은 처음부터
기존 ^memory_reclamation_ 세 항목을 제외한다. 이 정책과 기존 장시간 메모리 기준/Intel AV
보류는 바꾸지 않았으며 원인을 하드웨어로 확정하지 않았다.

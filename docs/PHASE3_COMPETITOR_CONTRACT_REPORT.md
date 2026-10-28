# Phase 3 경쟁 SDK 지원·동등 계약 검토

검토일: 2026-10-28. **P3-CMP-01 완료: 추가 우선 비교 대상은 LLGL**이다. 기존 GraphicsSDK/SDL 기준에 LLGL을 더하는 순서로 `P3-CMP-02`~`10`을 진행한다. 이번 결과는 고정 소스·공개 API·프로젝트 루트 라이선스 검토이며, LLGL Vulkan 빌드·GPU 실행·정확성·성능 수용은 다음 작업이다. 새 성능 표본은 0개다.

## 후보별 결정과 근거

아래 링크는 모두 [기존 고정 목록](../benchmarks/competitors/sources.lock.json)의 불변 커밋이다. 세부 파일 SHA-256/Git blob SHA-1, API 식별자와 원본 줄 위치는 [source-review.json](../benchmarks/phase3_comparison/competitors/source-review.json)의 30개 기록에서 확인한다. 소스 원문이나 외부 바이너리는 이 검토에 복사하지 않았다.

| 후보 / 루트 라이선스 | 고정 소스에서 확인한 지원과 제약 | 선택·연결 작업 |
|---|---|---|
| [LLGL](https://github.com/LukasBanana/LLGL/tree/649c0cc8fa608312c5e4b94dae409a5859fc7553) / BSD-3-Clause | 실제 `LLGL_Vulkan` target, Vulkan 구현, Windows/Linux 플랫폼 분기. Vulkan은 experimental·기본 OFF. 공개 buffer/texture 복사·draw/instancing·PSO·fence와 native physical device/queue 조회 경로가 있다 | **추가 우선 대상**, `P3-CMP-02` build/probe → `03`~`06` 정확성 → `07`~`10` 새 비교 |
| [SDL](https://github.com/libsdl-org/SDL/tree/3b2bc951500ca6a1f428af77e036e732d9a8afe3) / Zlib | 실제 Vulkan backend와 GPU 공개 API. 기존 프로필의 정렬·usage·API version 차이 및 native handle 미검증 귀속을 유지 | **기존 기준 대상**, `P3-BEN-01`~`09`; Linux 물리 GPU 수용과 draw GPU timestamp는 미완료 |
| [DiligentCore](https://github.com/DiligentGraphics/DiligentCore/tree/4678e4393cd0194f3c56e5d6611221d46497cc95) / Apache-2.0 | Win32/Linux `VULKAN_SUPPORTED`, Vulkan device factory와 buffer/texture/graphics PSO API 확인. Core를 독립적으로 고정했다. [DiligentEngine 부모](https://github.com/DiligentGraphics/DiligentEngine/tree/9c63c12ec2a9f541b6a42851d30d851605e6782a)의 submodule이 이 Core pin과 같다는 주장은 하지 않는다 | **추가 검토 보류**, 조건부 `P3-CMP-11` 유지. transfer·shader·fence의 측정 경계와 Vulkan 빌드 입력 감사 필요 |
| [bgfx](https://github.com/bkaradzic/bgfx/tree/d8d6acd94b64e01da3c4d1bc869df2dc2a8eb1ba) / BSD-2-Clause | Windows/Linux Vulkan 설정과 실제 `renderer_vk.cpp`. 이 pin의 공개 readback은 `read(TextureRegion, data)`이며 결과가 준비될 frame 번호를 반환한다 | **추가 검토 보류**, 조건부 `P3-CMP-12` 유지. frame/render thread 완료와 fence 완료를 맞추고 shader/program 생성과 PSO 생성의 차이를 별도 검증해야 함 |
| [The Forge](https://github.com/ConfettiFX/The-Forge/tree/cd5046893faba2dc7869243873bf01f02a6f0df9) / Apache-2.0 | 실제 파일은 `Common_3/Graphics/Vulkan/Vulkan.c`. Windows 명시 Vulkan/Linux 분기, queue/fence/instancing/PSO API 확인 | **추가 검토 보류**, 조건부 `P3-CMP-13` 유지. ResourceLoader·shader·vendor 입력과 경계 감사 필요. Vulkan.c는 NOTICE를 참조하지만 이 pin에 프로젝트 루트 NOTICE가 없어 적용 고지 확인도 필요 |
| [NVRHI](https://github.com/NVIDIA-RTX/NVRHI/tree/6b96fb03e07539f08327aea76c56d55f1de9d906) / MIT | README가 Windows/Linux Vulkan 1.3 및 `nvrhi_vk`를 명시. 이번에는 README/루트 라이선스까지만 확인했고 전체 backend 빌드는 검토하지 않음 | **후속 후보 유지**, 새 수용 작업은 선택하지 않음. 현재 1.1 기준과의 API floor, caller-native device 구성, 숨은 upload suballocation·수명 정책 검토 필요 |
| [Dawn](https://github.com/google/dawn/tree/f7b0358d022045bca67dda4127e5790d3fd1ae7b) / BSD-3-Clause | README의 native WebGPU Vulkan backend 선언과 루트 라이선스 확인. 전체 플랫폼/backend 구현·빌드는 이번 검토 밖 | **후속 후보 유지**, 새 수용 작업은 선택하지 않음. WebGPU 장치·shader validation·비동기 완료의 동등 계약 필요. README만으로 SPIR-V 확장·timestamp 가능 여부를 단정하지 않음 |
| [Sandbox](https://github.com/blackvrice/Sandbox/tree/afe563557367a0f2b5cef88488708da36f578b70) / 프로젝트 루트 라이선스 **미확인** | 완전한 고정 Git tree와 캐시 VulkanDevice.cpp blob이 일치하며 실제 Vulkan 구현이 존재한다. tree 루트에 LICENSE/COPYING이 없다 | **후속 후보 유지**, 새 수용 작업은 선택하지 않음. 의존성 라이선스를 프로젝트 전체 권한으로 전용하지 않는다. 통합 전 권한·빌드·네 작업 대응을 별도로 확인 |

지원 소스의 존재는 현재 장치에서 실행 가능하다는 증거가 아니다. LLGL·SDL·DiligentCore·bgfx·The Forge는 HEAD가 고정 커밋과 일치하고 tracked 변경이 없는 checkout에서 Git blob을 읽었다. NVRHI/Dawn README와 루트 라이선스는 고정 GitHub contents와 캐시를 대조했다. Sandbox는 고정 완전 tree의 blob ID와 캐시를 대조했다.

루트 라이선스 분류는 **새 Vulkan 빌드 전체의 입력 허가나 SDK 전체 재배포 승인으로 사용하지 않는다**. 기존 D3D12 감사는 실제 선택했던 입력에만 적용된다. 후속 빌드에서는 사용한 source/header/compiler/linker/runtime·shader 도구·전이 의존성·선택 라이선스·고지를 다시 기록해야 한다. proprietary vendor SDK나 캐시된 바이너리를 자동으로 포함하지 않는다.

## LLGL 네 작업의 고정 대응 계약

[selection.json](../benchmarks/phase3_comparison/competitors/selection.json)은 기존 공통 계약과 각 프로필 JSON의 **의미 해시**를 연결한다. Windows/Linux 줄바꿈 차이로 해시가 바뀌지 않으며, 작업 크기·횟수·경계·통계가 달라지면 검사를 거부한다. 기존 13개 시나리오를 축소하지 않는다. 아래 API 대응은 구현 계획이며 실제 adapter 통과를 뜻하지 않는다.

| 작업 / 연결 ID | 그대로 유지할 입력·측정 구간 | LLGL 공개 API 대응과 반드시 확인할 차이 |
|---|---|---|
| upload / `P3-CMP-03`, `07` | odd 65×37 RGBA8, 1080p/4K RGBA8, 4K RGBA16F. payload 생성·resource setup·readback 밖, staging map/CPU copy부터 record/submit/업로드 fence 완료까지 total CPU 시간 | `CreateBuffer`, `MapBuffer`/`UnmapBuffer`, `CopyTextureFromBuffer`, queue Submit + fence Submit/WaitFence. readback은 `CopyBufferFromTexture`/`ReadBuffer` 또는 `ReadTexture`로 측정 밖. row/layer stride·포맷 변환·정렬 기록. 과거 LLGL D3D12 layout 문제를 Vulkan 결과로 재사용하지 않음 |
| draw / `P3-CMP-04`, `08` | 65×37 RGBA8, scissor (5,3,55,31), triangle 3 vertices, first vertex/instance 0. draw 1/1000회와 1000 instances. 동일 SPIR-V·변하는 triangle/clear 전체 참조. CPU record/submit/wait/total | `BeginRenderPass`, `SetScissor`, `Draw`/`DrawInstanced`, Submit/WaitFence. setup/readback 밖. CPU wait는 GPU timestamp가 아니며 `P3-BEN-09`는 별도 필수로 유지 |
| PSO / `P3-CMP-05`, `09` | PSO 1/8개. shader object 생성·해제를 포함하는 public create, pipeline public release를 각 CPU 구간으로 측정. GPU 사용·완료·전체 readback 밖 | `CreateShader`, `CreatePipelineState`, `Release`. 동일 shader/target/영역. native pipeline 생성 수·driver cache 상태 미검증. public release로 물리 메모리 회수를 주장하지 않음 |
| allocation / `P3-CMP-06`, `10` | 4096-byte buffer와 65×37 RGBA8 texture 각 1/1000개. public create/release만 CPU 측정. staging·payload·GPU 사용·완료·모든 자원의 변하는 전체 readback 밖 | `CreateBuffer`/`CreateTexture`, `Release`. LLGL bind/usage/default allocator를 기록하고 SDK general/SDL vertex usage와의 차이를 공개. 기본 memory chunk 최소 1MiB·내부 suballocation은 native allocation 개수의 동등성을 뜻하지 않음 |

`P3-CMP-02`에서 먼저 Release Vulkan 빌드를 고정하고 다른 renderer는 OFF로 제한한다. `LLGL_VK_ENABLE_SPIRV_REFLECT=OFF`, **`LLGL_VK_ENABLE_GPU_ASSISTED_VALIDATION=OFF`**를 명시한다. 후자는 upstream 기본 ON이므로 기존 core+sync validation 조건을 바꾸지 않기 위한 설정이다. 의존성 감사 후 실제 선택 target/link/runtime을 기록한다.

장치 귀속에는 `RenderSystem::GetNativeHandle`의 실제 `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, queueFamily를 사용해 UUID/driver/queue를 대조한다. vendor preference는 힌트이므로 장치가 다르면 실패해야 한다. LLGL은 loader가 지원하는 instance version을 요청하는 소스 경로가 있다. 기존 SDK/SDL의 프로필별 요청 차이와 함께 실제 요청·지원 API version을 기록하고 동일 요청이라고 가정하지 않는다.

LLGL 설정은 지원되지 않는 enabled layer를 무시할 수 있으므로 이름을 설정했다는 사실만으로 validation 통과 처리하지 않는다. 필수 core+sync layer의 **실제 로드와 동작**을 관측하고 missing-layer 및 wrong-target 거부 검사를 통과해야 한다. Vulkan workload 자체를 native API로 우회해서 LLGL 비용으로 측정하지 않는다. native 조회는 귀속·검증용이다.

정확성/validation과 성능은 분리 프로세스에서 실행한다. 모든 sample의 변하는 전체 readback을 확인하고, 같은 실제 GPU/driver에서 세 SDK를 새로 실행해 순서를 섞고 모든 원본 표본·process median·quantile을 보존한다. 기존 SDK/SDL 쌍의 결과를 새 LLGL 실행에 붙여 세 SDK 비교로 만들지 않는다. 세 대상 순서 일반화는 `P3-CMP-07` 전에 프로토콜에 명시하고 기존 쌍 전용 실행기를 그대로 세 대상에 사용하지 않는다.

## 확인 방법과 남은 작업

```text
python tools/check_vulkan_competitors.py
python tools/check_vulkan_competitors.py --live
python -m unittest discover -s tests -p test_vulkan_competitors.py -v
python tools/roadmap.py --check
```

offline 검사는 기록된 metadata의 연결·고정 pin·계약·경계·작업 대응을 확인한다. 원본 소스의 독립 재획득이나 GPU 실행을 재현하지는 않는다. `--live`는 다섯 checkout의 pin/clean 상태, 30개 원본 파일의 해시·Git blob·모든 식별자 줄 위치를 추가 확인하며 컴파일이나 GPU 실행은 하지 않는다. 저장한 metadata의 자체 해시는 외부 신뢰 서명이 아니다.

새 거부/회귀 테스트 19개는 후보·필수 조건 누락, runtime 수용 위조, pin/측정 계약 변경, 조건부 작업 자동 선택, 라이선스 전용, mutable URL·경로 이탈·source 변조 등을 확인한다. 전체 검사 결과는 [검증 기록](../benchmarks/reports/validation/2026-10-28-phase3-competitor-contracts/summary.json)에 보존한다.

다음 작업은 **`P3-CMP-02` LLGL Vulkan build·장치 probe**다. `P3-CMP-02`~`10`, 조건부 `11`~`13`, `P3-BEN-07`/`08` 물리 GPU 수용, `P3-BEN-09` GPU timestamp, Linux 비교와 final gate의 완료 기준·의존성·필수 여부는 변경하지 않았다. 환경 부재나 API 제약으로 필수 요구를 자동 축소하지 않는다.

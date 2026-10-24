# Phase 3 — Vulkan 네이티브 할당·해제 구간 관측

2026-10-24 Windows x64 / MSVC 19.44. 기존 할당 baseline의 공개 API 지연 차이를
해석할 근거로 자체 Vulkan explicit layer, 구간 marker, 객체·바이트 ledger와 검증 도구를
구현했다. 제품 SDK·공개 API·ABI와 vendor 소스는 변경하지 않았다. 이전
[성능 baseline](PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md)은 그대로 보존했다.
이번 결과는 **호출 수·요청 바이트 진단**이며 성능 비교 자료가 아니다.

## 구현과 관측 경계

자체 레이어는 [Khronos loader/layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md)의
version 2 협상과 instance/device dispatch chain을 사용한다. 공개 handle을 감싸거나
자원을 대체하지 않고 다음 레이어/driver로 호출을 전달한 뒤 기록한다. SDL·SDK 정적
library는 기존 설치본을 재사용했다. Child의 VK_LAYER_PATH / VK_INSTANCE_LAYERS에만
자체 DLL과 고정 VVL을 지정한다. Registry 설치·시스템 정책·driver 설정 변경은 없다.
실제 로드된 DLL 경로와 binary SHA, main/layer source fingerprint를 모두 확인한다.

관측 명령은 **vkCreateBuffer, vkDestroyBuffer, vkCreateImage, vkDestroyImage,
vkAllocateMemory, vkFreeMemory, vkBindBufferMemory, vkBindImageMemory** 8개다.
그 밖의 명령은 다음 dispatch로 전달한다. 고정 SDK/SDL의 이 workload가 사용하는
명령 부분 집합을 관측하며, Vulkan 전체 명령·extension·다른 platform을 지원하는
범용 profiler를 주장하지 않는다. 상태는 device별 native handle로 구분한다.

Allocate/free bytes는 성공한 VkDeviceMemory allocationSize 요청과 대응 해제의
누적 바이트다. Native VkDeviceMemory 객체 수이며 OS commitment·residency·VRAM peak,
실제 물리 allocation 수, driver 내부 allocation 수 또는 물리 메모리 회수가 아니다.
성공한 vkFreeMemory가 OS의 즉시 메모리 회수를 입증하지는 않는다.

프로세스 전체에 한 active phase를 두고 mutex로 상태와 구간 전환을 보호한다.
이 고정 workload는 한 CPU 기록 thread/한 완료 대기 흐름이다. 타 SDK의 비동기
worker 동작 전체를 일반적으로 귀속시키는 기능은 검증하지 않았다.
Ledger는 객체 중복·미확인 해제, 100,000개 초과 live handles와 예외로 누락된 이벤트를
실패로 기록한다. 최대 2,048개 구간, 반복 번호 0..256으로 제한한다.
JSON은 active 구간이나 누락이 있으면 완성되지 않으며 runner가 실패 처리한다.

## 동일 작업과 구간

buffer1 / buffer1000은 4,096-byte device buffer, texture1 / texture1000은
65×37 RGBA8 sampled 2D texture다. 기존 changing payload를 사용해 모든 자원에
GPU copy 후 전체 tight readback을 비교한다. SDK row footprint pack/unpack과 SDL
tight transfer layout은 기존과 같다. SDK general-buffer usage와 SDL VERTEX usage,
전용 할당과 각 API의 내부 기본 정책 차이를 그대로 포함한다. Shader는 필요 없다.

| 구간 | 관측 작업 |
|---|---|
| target_create | N개 target 공개 생성 API |
| helpers_and_gpu | 2N개 upload/readback helpers, payload, batch copy, fence/idle, SDK readback |
| helper_release | helper 정리; SDL은 이 구간에서 readback 비교와 public helper release |
| target_release | N개 target 공개 핸들 해제 |
| post_release_idle | SDK weak wrapper 만료 확인과 양 SDK의 해제 뒤 idle |
| device_teardown | 마지막 반복 이후 device 종료 |

위 marker는 이전 SDK의 원본 제품 코드가 아닌 별도 진단 executable에만 추가했다.
각 프로세스는 기존 CLI의 warmup=1 + sample_count=3, 총 4개 반복을 모두 관측한다.
구간당 calls/successes, allocate/free bytes와 전후 live memory/buffer/image 수를 기록한다.
인접 구간 continuity, `after-before=create-destroy`, bytes=allocate-free 및 최종 0을
독립 Python validator로 다시 계산한다. Device 파괴로 추적 객체가 암묵적으로 사라진
경우를 free로 꾸미지 않고 cleanup remainder로 별도 집계하며 정상 실행에서 0을 요구한다.

SDK target 생성·해제 구간의 N개 native create/bind/allocate/destroy/free가 실제
계수되는 것을 양 GPU에서 대조했다. CPU ledger는 실패 allocation, 중복·unknown free,
handle 재사용·종류/device 분리·null destroy를 검사한다. DLL control은 협상 실패/성공,
dispatch forwarding, 잘못된 phase/반복 및 active phase의 미완성 JSON을 검사한다.

## 실제 관측

NVIDIA GeForce GTX 1660 SUPER / Intel UHD Graphics 770, GraphicsSDK/SDL, 작업 4개,
VVL 사용/미사용 각각 16개, 총 **32개 진단 프로세스 / 672개 구간**이다.
각 장치·SDK·작업의 두 validation 조건에서 모든 구간의 native counters와 request/free
bytes가 일치했다. 표는 VVL 미사용 관측이다. 생성 값은 반복 0/3, 해제·idle은 반복 3,
device teardown은 프로세스 종료 구간의 **호출 개수**이며 시간값이 아니다.

| GPU | SDK | 작업 | 생성 Allocate 첫/마지막 | 공개 해제 Destroy/Free | 이후 idle Destroy/Free | device teardown Free |
|---|---|---|---:|---:|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | graphicssdk | buffer1000 | 1000/1000 | 1000/1000 | 0/0 | 0 |
| NVIDIA GeForce GTX 1660 SUPER | graphicssdk | texture1000 | 1000/1000 | 1000/1000 | 0/0 | 0 |
| NVIDIA GeForce GTX 1660 SUPER | sdl | buffer1000 | 1/0 | 0/0 | 3000/0 | 2002 |
| NVIDIA GeForce GTX 1660 SUPER | sdl | texture1000 | 2/1 | 0/0 | 3000/0 | 2003 |
| Intel(R) UHD Graphics 770 | graphicssdk | buffer1000 | 1000/1000 | 1000/1000 | 0/0 | 0 |
| Intel(R) UHD Graphics 770 | graphicssdk | texture1000 | 1000/1000 | 1000/1000 | 0/0 | 0 |
| Intel(R) UHD Graphics 770 | sdl | buffer1000 | 1/0 | 0/0 | 3000/0 | 2002 |
| Intel(R) UHD Graphics 770 | sdl | texture1000 | 2/1 | 0/0 | 3000/0 | 2003 |

Destroy는 DestroyBuffer+DestroyImage다. Post-release idle의 SDL 3,000개 파괴에는
**target 1,000개와 helper 2,000개가 함께 포함**된다. Target 해제 API 안에서 모두
파괴된다는 해석은 틀리다. SDL의 device teardown Free에는 helper/pool 및 device
기본 자원이 포함되므로 target만의 allocation 수로 읽을 수 없다.

관측한 SDK는 target마다 native memory allocation/free를 수행한다. SDL buffer는
첫 target batch의 1회 allocation 뒤 마지막 batch에는 allocation 호출이 없었다.
SDL texture1000은 첫 batch 2회, 마지막 batch 1회였다. 따라서 모든 texture 반복을
allocation 없는 cache hit로 분류할 수 없다. SDK는 target release 구간에서 N개 native
자원과 memory를 해제했고, SDL의 해당 구간에서는 파괴/free 호출이 없었다.
SDL은 이후 idle에서 자원을 파괴하고, 관측된 memory pool의 free는 teardown에서 나타났다.

이는 이전 공개 API 시간 비교가 서로 다른 allocator/disposal 작업량을 포함한다는
직접 관측이다. 순수 native 함수 시간이나 차이의 비율·전체 병목 원인을 확정하지 않는다.
레이어 기록·VVL이 실행을 바꾸므로 본 실행의 내부 CPU 시간은 JSON/CSV에 출력하지 않았고,
기존 성능 통계에 합치거나 baseline을 대체하지 않았다. Allocator 개선은 별도 후보 구현과
동일 조건 before/after·회귀 검증이 필요하다. 제품에 pooling 최적화를 추가하지 않았다.

## 검증·감사·재현

총 **64,064개 logical target / 버퍼 32,032개 / 이미지 32,032개 /
439,350,912 tight bytes**를 확인했다. 모든 구간의 ledger 오류·누락과 device cleanup
remainder가 0이며 최종 layer-visible live memory/buffer/image도 0이다. 이는 bounded
프로세스의 명령·수지 확인이며 장시간 메모리 안정성이나 물리 메모리 회수 승인과 다르다.
새 CPU CTest 2/2(ledger 17 + DLL interface 13 assertions), Python 417/417(신규 30),
report 변조 controls 8개와 license negative controls 2개가 통과했다.
Missing trace/VVL, 다른 DLL 경로/GPU, invalid phase/options 16개는 결과 없이 실패했다.
전체 native 실행 명령은 probes 2 + profile 32 + controls 16 = 50개다.

초기 pilot은 자체 manifest implementation_version의 잘못된 정수 형식으로 레이어를
로드하지 못했다. 문자열로 수정한 별도 fresh build의 pilot과 최종 matrix는 통과했다.
그 시작 실패·manifest·pilot 원본을 보존하며 성공 workload에 포함하지 않았다.
라이선스 notice 보완 때 기존 출력 경로 재사용은 exclusive-output guard가 거부했다.
새 `-final` 감사 경로를 사용했고 초기 inventory와 거부 기록도 보존했다.

SDK fingerprint `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`, diagnostic fingerprint
`4e00b0bc59f4674efdbc4414356ac18f1c566d6664b8b4443703f0aab1fc6ea9`다. 기존 SDK 패키지 23개와 이전 allocation/PSO/draw/upload
증거·ZIP을 보존했다. 원래 SDK 선택 CTest 93개를 재실행하지 않았고 새 SDK binary package도
만들지 않았다. 장시간 메모리 기준과 과거 Intel AV 조사는 보류 상태다.

[최종 dependency inventory](../licenses/generated/windows-benchmarks-vulkan-allocation-trace-release-final/inventory.json)는
653 conservative inputs / 21 components / compiler·linker read logs 각 4개다.
Apache-2.0 vk_layer.h의 pin/SHA·source notice를 추가 정책에 기록했다. Compiler/linker
입력과 재사용 static closure의 확인이며 symbol reachability나 법적 승인을 뜻하지 않는다.
Source ZIP에는 own sources·scripts·fixtures·문서·policy·notices를 넣고 vendor tree,
EXE/DLL/LIB/PDB·dump·validation runtime은 넣지 않는다.

```powershell
cmake -S benchmarks/vulkan_allocation_trace -B out/your-new-trace-build -G "Visual Studio 17 2022" -A x64
cmake --build out/your-new-trace-build --config Release --parallel 1
ctest --test-dir out/your-new-trace-build -C Release --output-on-failure
python tools/run_vulkan_allocation_trace.py --build out/your-new-trace-build --output benchmarks/results/your-new-trace-run
python tools/report_vulkan_allocation_trace.py --input benchmarks/results/your-new-trace-run --output benchmarks/reports/your-new-trace-report
```

Prerequisite는 보존한 out/vulkan-presentation-install, out/sdl와 고정 VVL runtime이다.
현재 감사 build는 out/vulkan-allocation-trace-verified다. 새 build/result root로 재현한다.
[원본](../benchmarks/results/2026-10-24-vulkan-allocation-trace/results.json),
[요약](../benchmarks/reports/2026-10-24-phase3-vulkan-allocation-trace/summary.json),
[구간 CSV](../benchmarks/reports/2026-10-24-phase3-vulkan-allocation-trace/native-counts.csv),
[증거](../benchmarks/reports/2026-10-24-phase3-vulkan-allocation-trace/evidence.json),
[검증 script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-allocation-trace/verification-scripts/verify.py)를 따른다.
OS/driver profiling, allocator 후보 최적화, reuse/in-flight release, 다른 경쟁 SDK 및 Linux는
남아 있다. Phase 3 전체 완료는 아니다.

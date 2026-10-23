# Phase 3 — 동일 Vulkan draw / instancing 비교

2026-10-23~24 Windows x64 / MSVC 19.44. GraphicsSDK와 고정 SDL3의 첫 동일 Vulkan draw 비교다.
새 harness·runner·전체 이미지 CPU reference·구간 통계·원본 검사와 라이선스 감사를 구현했다.
제품 SDK 소스·공개 API·ABI는 변경하지 않고 기존 설치 SDK와 SDL static library를 재사용했다.
Linux 검증 및 Phase 3 전체 완료는 아니다.

## 계약

Vulkan 1.1 요청 / Release / CPU thread 1개 / headless / frame-in-flight 1개다.
RGBA8 65×37 texture, 동일하게 검증된 SPIR-V 1.3 fullscreen vertex shader와 red/green fragment
shader를 두 SDK에 같은 bytes로 공급한다. Vertex buffer, descriptor, blend, depth는 없다.
1개 clear/store pass의 scissor (5,3,55,31)에 삼각형을 그린다. 매 반복 red/green pipeline과
blue/cyan clear를 교대로 사용해 이전 프레임의 readback을 수용하지 않는다. Pipeline 두 개,
texture, shader 생성은 측정 밖이며 pipeline switching 자체는 기록 구간 안이다.

| 작업 | draw 호출/frame | instances/call | vertices/frame |
|---|---:|---:|---:|
| draw1 | 1 | 1 | 3 |
| draw1000 | 1,000 | 1 | 3,000 |
| instanced1000 | 1 | 1,000 | 3,000 |

인스턴스는 같은 geometry에 겹쳐 그리며 instance ID를 사용하지 않는다. 두 1,000-triangle
경로의 논리적 geometry는 같지만 명령 수가 다르다. 실제 게임의 분산 geometry, indexed draw,
binding 변경, fill-rate 포화, 해상도 스케일링 또는 native draw-count capture를 대표하지 않는다.
first_vertex/first_instance는 항상 0이다. SDL [draw 규약](https://wiki.libsdl.org/SDL3/SDL_DrawGPUPrimitives)의
builtin ID 조건과 [render pass](https://wiki.libsdl.org/SDL3/SDL_BeginGPURenderPass) 기본 viewport를 따른다.

Primary wall time은 command begin/acquire부터 기록, submit, 해당 fence 완료 및 SDL fence
release까지다. SDK begin의 query-pool 같은 내부 정책도 포함한다. record/submit/wait는 같은
CPU 구간의 분할이며 개별 표본의 합이 total과 같은지 검사한다. 각 열 median의 합이 total
median일 필요는 없다. GPU timestamp나 GPU 실행 시간으로 해석하지 않는다. Device/resource
생성, shader compile, 전체 readback/비교와 teardown idle은 측정 밖이다. Readback은 반복 사이
cache·온도·메모리 상태에 영향을 주므로 이 검증 포함 주기의 비교다.

## 호환성과 실패 기록

처음에는 고정 SDL의 기본 인스턴스 요청이 Vulkan 1.0이어서 SPIR-V 1.3 native validation
VUID-08737이 발생했다. stdout에도 validation ERROR가 나올 수 있어 runner가 두 stream을
검사하도록 구현했다. 그 실행은 JSON/검증 검사에서 중단했으며 성능 자료를 생성하지 않았다.
SPIR-V 1.0으로 조립·검증하는 별도 local tool 실험도 수행했지만 SDK의 명시적 SPIR-V 1.3
제한에 거부되어 채택하지 않았다. SDK 제한을 우회하거나 header version만 바꾸지 않았다.

최종 경로는 SDL 공개 [SDL_GPUVulkanOptions](https://wiki.libsdl.org/SDL3/SDL_GPUVulkanOptions)의
vulkan_api_version을 1.1로 요청한다. SDK의 기존 1.1과 맞추고 원래 검증된 동일 SPIR-V 1.3을
재사용했다. SDL vendor나 SDK는 수정하지 않았다. requested_instance_api_version=4198400은
요청 값이며 native_api_version은 physical device가 보고한 지원 버전이다. Native SDL instance
handle을 직접 캡처했다고 주장하지 않는다. 초기 CMake/Python 경로 정렬 차이와 audit target
개수 수정 기록도 별도 보존했다. 최종 두 fingerprint를 대조한 뒤에만 측정을 시작했다.

## GPU 식별과 검증

고정 SDL `3b2bc951500ca6a1f428af77e036e732d9a8afe3`의 high/low 선호로 서로 다른 NVIDIA/Intel을 발견한다.
고유한 정확 이름을 native Vulkan UUID/driver UUID/LUID와 DXGI에 대응시키고 CPU/OS/RAM,
driver/API/compiler 환경을 모든 run에서 대조한다. 이는 이름 대응에 의한 SDL 귀속이며 SDL
native physical-device handle 직접 증명은 아니다. 모호하거나 서로 다른 환경은 거부한다.

상속 VK_/VVL_/SDL_ 설정을 제거하고 implicit layer를 비활성화한다. 정확성 12개 프로세스는
고정 DLL SHA·경로·required layer와 synchronization/submit-time validation으로 먼저 실행했다.
SDL의 silent fallback을 막기 위한 SDK required-layer 확인과 loader trace도 보존한다.
SDK validationErrorCount, SDL error-priority log 및 양 stream의 native errors를 검사한다.
SDL에 별도 native callback error counter가 있다고 주장하지 않는다. 이후 unvalidated 성능
36개 프로세스를 serial 실행했으며 validation DLL 미로드를 확인한다. 다른 GPU 테스트와
겹치지 않았고 시스템 정책이나 driver 설정을 변경하지 않았다.

## 실제 결과

장치×SDK×작업별 3개 독립 프로세스, warmup 5회 / 측정 30개다. seed 20261025로 순서를
섞었고 **primary 표본은 1,080개**다. 적은 표본의 tail 추정과 공유 GPU 변동에 유의해야 한다.
Outlier 삭제 없이 P50/P95/P99, MAD, min/max와 프로세스별 median을 JSON/CSV에 보존한다.

| GPU | 작업 | GraphicsSDK P50 ms | SDL P50 ms | SDK / SDL |
|---|---|---:|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | draw1 | 0.1224 | 0.0847 | 1.445 |
| NVIDIA GeForce GTX 1660 SUPER | draw1000 | 0.2084 | 0.1692 | 1.231 |
| NVIDIA GeForce GTX 1660 SUPER | instanced1000 | 0.1707 | 0.1325 | 1.288 |
| Intel(R) UHD Graphics 770 | draw1 | 1.1378 | 0.2586 | 4.400 |
| Intel(R) UHD Graphics 770 | draw1000 | 1.3158 | 0.4171 | 3.155 |
| Intel(R) UHD Graphics 770 | instanced1000 | 1.0532 | 0.4092 | 2.574 |

이번 제한된 조건에서 모든 cell의 GraphicsSDK P50이 SDL보다 길다. GraphicsSDK의 instanced1000은
draw1000보다 짧지만 이는 전체 SDK 순위나 보편적인 instancing 이득, native driver 병목
규명의 증거는 아니다. 다음은 같은 표본의 CPU 구간 median이다.

| GPU | 작업 | SDK record / submit / wait ms | SDL record / submit / wait ms |
|---|---|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | draw1 | 0.0382 / 0.0296 / 0.0524 | 0.0188 / 0.0273 / 0.0363 |
| NVIDIA GeForce GTX 1660 SUPER | draw1000 | 0.0743 / 0.0275 / 0.1005 | 0.0440 / 0.0277 / 0.0935 |
| NVIDIA GeForce GTX 1660 SUPER | instanced1000 | 0.0435 / 0.0306 / 0.0947 | 0.0184 / 0.0258 / 0.0871 |
| Intel(R) UHD Graphics 770 | draw1 | 0.7581 / 0.0316 / 0.3227 | 0.0163 / 0.0169 / 0.2242 |
| Intel(R) UHD Graphics 770 | draw1000 | 0.8301 / 0.0270 / 0.4520 | 0.0532 / 0.0184 / 0.3371 |
| Intel(R) UHD Graphics 770 | instanced1000 | 0.5552 / 0.0241 / 0.4435 | 0.0186 / 0.0190 / 0.3669 |

wait에는 GPU fence와 host scheduling 비용이 포함된다. record/submit에도 각 SDK 내부 확인,
pool 관리 및 자원 참조 비용이 포함되므로 순수 Vulkan 호출 오버헤드로 단정할 수 없다.
GPU hardware profiler, allocation/PSO 비교나 추가 최적화는 이번에 수행하지 않았다.

## 검증·보존·재현

각 warmup/측정 뒤 2,405 pixels 전체를 CPU 사각 영역·색상 reference와 비교했다.
48개 workload GPU 프로세스에서 **1,308개 이미지 / 12,582,960 bytes**가 일치했다.
논리적 제출 draw 호출은 **436,872개**이며 native hardware counter로 검증한 수치는 아니다.
CPU reference는 1,705 covered / 700 clear pixels와 경계·프레임 변경을 12 assertions로 검사한다.
새 CTest 3/3, Python 295/295(신규 계약 43개), report 변조 controls 7개가 통과했다.
Missing required layer, 틀린 GPU와 잘못된 option 10개도 정상 결과 없이 실패했다.

SDK fingerprint `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`는 직전과 같고, benchmark fingerprint는
`6ed1d7cb7d0be93a20db5c0d6e3f2f68c2fc5c39f662c60a543dd8dbf3a05290`다. SDK의 이전 93개 선택 CTest와 후보 실험의 96개 선택 CTest는
이전 증거로 보존하며 이번에 재실행했다고 주장하지 않는다. 기존 SDK ZIP 23개(기각된 실험
패키지 포함)를 SHA로 보존하고 새 SDK binary ZIP은 만들지 않았다. 메모리 policy와 장시간
수용·과거 Intel AV 보류는 변경하지 않았으며 원인을 하드웨어로 확정하지 않았다.

[감사 inventory](../licenses/generated/windows-benchmarks-vulkan-draw-release/inventory.json)는
648 conservative compiler inputs / 21 components / 새 compiler·linker logs 각 2개다.
실제 linker reads와 고정 SDK/SDL libraries를 검사하며 SDL static closure를 hash로 재사용한다.
미사용 compiler members도 포함한 보수적 목록이며 symbol reachability나 vendor 재빌드 주장은 아니다.
기존 외부 SPIRV-Tools는 local-only 검증 전제이며 final benchmark binary에 링크하지 않는다.
새 소스 ZIP에 vendor tree·VVL runtime·EXE/DLL/LIB/PDB를 넣지 않는다. 감사 변조와 notices
누락을 거부한다.

```powershell
cmake -S benchmarks/vulkan_draw -B out/your-new-draw-build -G "Visual Studio 17 2022" -A x64
cmake --build out/your-new-draw-build --config Release --parallel 1
ctest --test-dir out/your-new-draw-build -C Release --output-on-failure
python tools/run_vulkan_draw.py --exe out/your-new-draw-build/Release/vulkan_draw.exe --output benchmarks/results/your-new-draw-run
python tools/report_vulkan_draw.py --input benchmarks/results/your-new-draw-run --output benchmarks/reports/your-new-draw-report
```

기본 의존성은 보존한 out/vulkan-presentation-install과 out/sdl이다. 현재 감사 profile의
build root는 out/vulkan-draw-benchmark-vulkan11이다. 재측정은 새로운 build/result root를 사용한다.
[원본 결과](../benchmarks/results/2026-10-23-vulkan-draw/results.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-draw-benchmark/summary.json),
[CSV](../benchmarks/reports/2026-10-23-phase3-vulkan-draw-benchmark/metrics.csv),
[증거](../benchmarks/reports/2026-10-23-phase3-vulkan-draw-benchmark/evidence.json)와
[검증 script](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw-benchmark/verification-scripts/verify.py)를 참조한다.
Linux native/WSI, interactive 출력, 다른 Vulkan 경쟁 SDK, allocation/PSO 비교는 남아 있다.

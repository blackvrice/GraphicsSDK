# Phase 3 — 동일 Vulkan 파이프라인 생성·공개 핸들 해제 비교

2026-10-24 Windows x64 / MSVC 19.44. 새 파이프라인 benchmark, 독립 프로세스 runner,
전체 이미지 reference와 원본·통계·라이선스 검사를 구현했다. 제품 SDK 소스·공개 API·ABI는
변경하지 않고 기존 설치 SDK와 SDL static library를 재사용했다. Phase 3 전체 완료는 아니다.

## 측정 계약

동일 Vulkan 1.1 요청, Release, CPU thread 1개, headless, frame-in-flight 1개다.
두 SDK에 같은 externally validated SPIR-V 1.3 fullscreen vertex와 red/green fragment bytes를
제공한다. RGBA8 65×37 target, blend/depth/bindings/vertex buffer 없음, 동일 clear/store pass다.
파이프라인을 반복 생성하고 매번 실제 GPU 그리기에 사용한 뒤 공개 핸들을 해제한다.

| 작업 | 생성·사용·해제 파이프라인/반복 | 확인할 화면 영역 |
|---|---:|---|
| pso1 | 1 | scissor (5,3,55,31) |
| pso8 | 8 | 위 사각형을 폭 6 또는 7 pixels인 8개 세로 영역으로 분할 |

각 영역에 해당 파이프라인으로 triangle 하나를 그린다. 반복 번호와 영역 번호에 따라
red/green shader 및 blue/cyan clear를 바꾼다. 매 반복 GPU 완료 뒤 2,405 pixels 전체를
독립 CPU 색상·영역 reference와 비교한다. 1,705 colored / 700 clear pixels이며 padding은
tight bytes로 추출한다. Native pipeline handle identity를 GPU capture로 입증한 자료는 아니다.

**create_ms**는 한 번의 batch에서 N개 새 pipeline과 각각의 vertex/fragment shader object
생성 및 shader handle 정리를 포함한다. SDK createGraphicsPipeline은 내부에서 shader module을
생성하고, SDL은 공개 CreateGPUShader 두 번 + CreateGPUGraphicsPipeline + ReleaseGPUShader를
각 pipeline마다 호출한다. 입력 SPIR-V와 pipeline descriptions·handle container 준비는 구간 밖이다.
SDK reflection, layout/render-pass 관리와 각 API 내부 정책은 포함한다. 순수 vkCreateGraphicsPipelines
호출이나 HLSL/GLSL source compilation 시간의 비교가 아니다.

**public_release_ms**는 GPU 사용과 fence 완료 및 untimed idle 뒤 공개 pipeline handle을
N개 해제하는 API wall latency다. SDK는 완료된 recording wrapper를 먼저 파괴하고 각
shared_ptr을 reset한 뒤 weak_ptr 만료를 확인한다. SDL은 ReleaseGPUGraphicsPipeline을 호출한다.
SDL 공개 [release 규약](https://wiki.libsdl.org/SDL3/SDL_ReleaseGPUGraphicsPipeline)은 안전한 시점의
해제를 보장하며 즉시 native destruction을 요구하지 않는다. 고정 SDL 소스의 release는 dispose
queue에 넣고, SDK NativePipeline destructor는 native 파괴를 호출한다. 따라서 이 수치는 서로
다른 public release 정책을 포함하며 **실제 GPU 메모리 회수 속도나 native destruction 비용의
동등 비교가 아니다**. 매우 작은 SDL 값은 host timer의 양자화·고정 호출 비용 영향도 받는다.

그리기, submit, fence 대기, recording 종료, 앞뒤 [GPU idle](https://wiki.libsdl.org/SDL3/SDL_WaitForGPUIdle),
readback/비교, device·texture 생성과 teardown은 두 구간 밖이다. 해제 뒤 untimed idle도
실행해 다음 반복 전 안전한 정리를 요청한다. 두 CPU 구간은 GPU 사용 간격으로 떨어져 있으므로
합쳐서 프레임 전체 wall time으로 보고하지 않는다. GPU timestamp·메모리 사용량도 측정하지 않는다.

## 반복 생성과 cache 범위

동일한 두 shader·같은 layout의 반복 생성이다. pso8은 red/green 두 종류를 각 4개 만든다.
Warmup 5회 뒤 측정하므로 첫 process/device의 cold pipeline 생성은 측정 밖이다. Driver와 SDL의
cache 정책은 그대로 두었고 disk cache를 지우거나 pipeline cache를 주입하지 않았다.
Cache hit/miss, cold compilation 또는 known warm cache로 분류하지 않는다. Batch 비용을
개당 cold creation 비용이나 unique-shader 처리량으로 일반화할 수 없다.

## 장치·검증 조건

SDL pin은 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`다. SDL의 공개 Vulkan 옵션으로 1.1을 요청해 SDK와 맞추고
SPIR-V 1.3을 그대로 사용한다. Vendor와 SDK를 수정하지 않았다. 요청 instance 버전은 4198400,
native_api_version은 physical device 지원 버전이다. SDL native instance/physical-device handle
직접 캡처를 주장하지 않는다. 고유한 정확 GPU 이름과 native Vulkan UUID/driver UUID/LUID 및
DXGI 대응, CPU/OS/RAM/driver/compiler를 원본에 보존하고 모든 비교에서 일치를 요구한다.

상속 VK_/VVL_/SDL_ 설정을 child에서 제거하고 implicit layers를 비활성화했다. 정확성은
고정 Khronos validation DLL SHA·경로, required layer와 synchronization/submit-time 설정을
사용한다. SDK error counter, SDL error-priority logs와 양 stream의 validation ERROR를 검사한다.
SDL의 fallback 차단용 SDK required-layer 생성 및 loader trace도 보존했다. 이후 성능 프로세스는
validation DLL 미로드를 확인하고 serial 실행했다. GPU 회귀 검사와 측정을 겹치지 않았으며
시스템/driver 설정을 바꾸지 않았다.

## 실제 결과

장치×SDK×작업마다 독립 프로세스 3개, warmup 5회 / 측정 30회다. seed 20261026로 순서를
섞었다. **측정 반복은 720회, 별도 CPU 구간 관측값은 1,440개**다. Validation 시간과 warmup은
통계에서 제외했다. Outlier 삭제 없이 P50/P95/P99, MAD, min/max와 프로세스별 median을
JSON/CSV에 남겼다. 아래는 batch당 P50이며 생성은 ms, 공개 해제는 µs다.

| GPU | 작업 | SDK 생성 ms | SDL 생성 ms | SDK 공개 해제 µs | SDL 공개 해제 µs |
|---|---|---:|---:|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | pso1 | 0.09790 | 0.07080 | 3.350 | 0.100 |
| NVIDIA GeForce GTX 1660 SUPER | pso8 | 0.20370 | 0.11305 | 6.400 | 0.200 |
| Intel(R) UHD Graphics 770 | pso1 | 0.06355 | 0.05195 | 27.100 | 0.100 |
| Intel(R) UHD Graphics 770 | pso8 | 0.15905 | 0.11690 | 55.850 | 0.200 |

이 조건에서는 GraphicsSDK의 공개 pipeline 생성 비용이 SDL보다 길다. SDK와 SDL 내부 shader,
layout, render-pass, cache 및 해제 정책이 다르므로 어느 한 native 함수나 driver가 원인이라고
단정하지 않는다. 공개 해제 차이를 회수 성능 우위로 해석하지 않는다. 제한된 표본·두 장치와
반복 생성 계약의 결과이며 전체 SDK 순위나 다른 workload/API의 우위를 뜻하지 않는다.
이전 upload와 draw 구간을 합치거나 비교 가능한 같은 시간으로 처리하지 않는다.

## 검증·감사·재현

Required-layer workload 8개와 성능 workload 24개에서 **872개 이미지 / 8,388,640 bytes**가
모두 일치했다. 논리적으로 **3,924개 pipeline 생성·공개 해제·화면 영역 사용**을 수행했다.
Native hardware counter로 pipeline identity/count를 증명한 수치는 아니다. CPU reference는
불균등 stripe 경계·전체 coverage·반복별 색상 변경 등 38 assertions를 검사했다.
새 CTest 3/3, Python 339/339(신규 계약 44개), report 변조 controls 8개가 통과했다.
Missing layer/다른 GPU/잘못된 option 10개는 정상 결과 없이 실패했다. 전체 native 명령은
probes 2 + correctness 8 + measurement 24 + controls 10 = 44개다.

SDK fingerprint는 `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`, benchmark fingerprint는
`7274fa502bdbfa48a5eadfa98090a01574f25dd4208d3ef53fb9298fc6f830f0`다. 제품 SDK는 그대로이므로 기존 93개 선택 CTest를 이번에
재실행했다고 주장하지 않는다. 원래 SDK ZIP 23개(기각된 실험용 package 포함), 이전 draw·upload
원본과 source ZIP을 SHA로 보존했다. 새 SDK binary package는 만들지 않았다.
장시간 메모리 기준과 과거 Intel AV 조사는 계속 보류이며 하드웨어 원인을 확정하지 않았다.

[라이선스 inventory](../licenses/generated/windows-benchmarks-vulkan-pso-release/inventory.json)는
648 conservative inputs / 21 components / 새 compiler·linker read logs 각 2개다. Benchmark와
CPU reference의 실제 입력 및 linked library hashes를 검사하고 기존 SDK/SDL static closure를
재사용했다. Symbol reachability나 vendor 재빌드 주장은 아니다. 누락 notices·변경 inventory를
거부하며 소스 ZIP에는 vendor tree·EXE/DLL/LIB/PDB·VVL runtime을 넣지 않는다. 기존 own shader
validator receipt를 재사용했고 이번에 shader assembler를 새로 실행하지 않았다.

```powershell
cmake -S benchmarks/vulkan_pso -B out/your-new-pso-build -G "Visual Studio 17 2022" -A x64
cmake --build out/your-new-pso-build --config Release --parallel 1
ctest --test-dir out/your-new-pso-build -C Release --output-on-failure
python tools/run_vulkan_pso.py --exe out/your-new-pso-build/Release/vulkan_pso.exe --output benchmarks/results/your-new-pso-run
python tools/report_vulkan_pso.py --input benchmarks/results/your-new-pso-run --output benchmarks/reports/your-new-pso-report
```

기본 전제는 out/vulkan-presentation-install과 out/sdl의 보존한 설치본이다. 현재 감사 profile
build root는 out/vulkan-pso-benchmark이며 재측정은 새로운 build/result root를 사용한다.
[원본 결과](../benchmarks/results/2026-10-24-vulkan-pso/results.json),
[요약](../benchmarks/reports/2026-10-24-phase3-vulkan-pso/summary.json),
[CSV](../benchmarks/reports/2026-10-24-phase3-vulkan-pso/metrics.csv),
[증거](../benchmarks/reports/2026-10-24-phase3-vulkan-pso/evidence.json)와
[검증 script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-pso/verification-scripts/verify.py)를 따른다.
Linux native/WSI, interactive 출력, 다른 Vulkan 경쟁 SDK, allocation workload 및 내부 병목
구간 진단은 남아 있다. 관측 근거 없이 cache·allocator 최적화를 추가하지 않았다.

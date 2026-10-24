import json
import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
x=json.loads((root/'benchmarks/results/2026-10-24-vulkan-allocation/results.json').read_text(encoding='utf-8'))
table='| GPU | 작업 | SDK 생성 ms | SDL 생성 ms | SDK 공개 해제 ms | SDL 공개 해제 ms |\n|---|---|---:|---:|---:|---:|\n'
for row in x['groups']:
    a=row['statistics']['graphicssdk'];b=row['statistics']['sdl']
    table+=f"| {row['device_name']} | {row['scenario']} | {a['create_ms']['p50_ms']:.5f} | {b['create_ms']['p50_ms']:.5f} | {a['public_release_ms']['p50_ms']:.5f} | {b['public_release_ms']['p50_ms']:.5f} |\n"
text=f'''# Phase 3 — 동일 Vulkan 버퍼·텍스처 생성·공개 핸들 해제 비교

2026-10-24 Windows x64 / MSVC 19.44. Shaderless resource benchmark, 독립 프로세스
runner, 전체 데이터 reference와 원본·통계·라이선스 검사를 구현했다. 기존 설치 SDK와
SDL static library를 재사용했다. 제품 SDK 소스·공개 API·ABI는 변경하지 않았다.
Phase 3 전체 완료는 아니다.

## 측정 계약

두 SDK 모두 Vulkan 1.1 요청, Release, CPU thread 1개, headless, 동시 submit 1개다.
buffer1 / buffer1000은 device buffer 4,096 bytes를 1개 / 1,000개 생성한다.
texture1 / texture1000은 65×37 RGBA8 UNorm, sampled 2D, mip/layer/sample 1인
texture를 1개 / 1,000개 생성한다. Texture당 tight data는 9,620 bytes다.
GraphicsSDK general device buffer와 SDL VERTEX usage를 사용한다. SDK buffer는 더 넓은
native usage flags를 제공한다. 같은 논리 작업이지만 같은 native allocation flags는 아니다.
Texture는 양쪽 모두 sampled usage로 생성하며 shader sampling은 실행하지 않는다.

**create_ms**는 준비된 description과 handle container로 N개 target을 생성하는 공개 API
wall latency다. Target은 미초기화 상태로 생성되며 payload 작성은 측정 밖이다.
각 반복·자원 번호·byte 위치를 섞은 payload를 각 target에 GPU copy로 쓰고 다시
download한다. 하나의 command submission에 N개 자원의 업로드·다운로드를 묶고 fence와
idle 후 모든 버퍼 byte / 모든 texture texel channel을 CPU 기대값과 비교한다.
Texture row padding은 SDK footprint에 맞춰 pack/unpack하며 tight data만 byte 총계에 넣는다.
SDL transfer는 tight row layout이다. 자원별 helper upload/readback도 N개씩 존재하지만
target 생성·해제 논리 개수에는 포함하지 않는다.

공개 [버퍼 생성](https://wiki.libsdl.org/SDL3/SDL_CreateGPUBuffer)과
[텍스처 생성](https://wiki.libsdl.org/SDL3/SDL_CreateGPUTexture) 규약에서 초기 내용은
정해져 있지 않다. 따라서 실제로 payload를 쓴 뒤 확인한다.
[업로드](https://wiki.libsdl.org/SDL3/SDL_UploadToGPUBuffer)의 GPU 순서와
[다운로드](https://wiki.libsdl.org/SDL3/SDL_DownloadFromGPUBuffer)의 fence 완료 조건을 따른다.
Reference는 1,000개 자원 prefix 구분과 반복별 변화, tight/padded rows 및 잘못된 layout을
CPU에서 검사한다. 초기 CPU 검사가 prefix 충돌을 발견해 비선형 혼합으로 수정했으며,
실패 기록을 별도 보존했다. 수정 전에는 workload GPU 측정을 실행하지 않았다.

**public_release_ms**는 모든 target의 GPU 사용·전체 readback 완료와 helper 정리 뒤
target 공개 핸들 N개를 해제하는 wall latency다. SDK recording wrapper를 먼저 파괴하고,
각 target shared_ptr을 reset하며 untimed weak_ptr 만료를 확인한다. SDL은 ReleaseGPUBuffer
또는 ReleaseGPUTexture를 호출한다. 공개 [buffer release](https://wiki.libsdl.org/SDL3/SDL_ReleaseGPUBuffer)와
[texture release](https://wiki.libsdl.org/SDL3/SDL_ReleaseGPUTexture)는 안전한 시점의 해제를
규정한다. 즉시 native destruction을 요구하지 않는다. SDL의 dispose queue와 SDK의
native destructor 등 내부 정책 차이를 포함하므로 **실제 GPU 메모리 회수 속도 비교가 아니다**.
매우 짧은 값은 timer 양자화·호출 고정 비용의 영향도 받는다.

SDK Vulkan은 resource별 dedicated native allocation이고 SDL은 자체 pooling/suballocation
정책을 사용한다. Helper 정리도 SDK는 destructor, SDL은 public release를 통해 수행한다.
이러한 allocator/disposal 정책과 직전 반복의 상태는 공개 API 비용에 영향을 줄 수 있다.
Logical resource 개수로 vkAllocateMemory 호출 수나 물리 allocation 수를 추정하지 않는다.
생성·해제 앞뒤 GPU idle을 요청하지만 driver/OS의 실제 회수·pool 비움은 입증하지 않는다.

Helper 생성·map/write/read, payload 준비·비교, GPU command 기록·submit·fence 대기,
recording 해제, device 설정·teardown과 target 해제 후 idle은 두 CPU 구간 밖이다.
두 구간은 떨어진 interval이므로 합쳐서 frame wall time으로 부르지 않는다.
GPU timestamp, CPU/GPU memory budget/peak, in-flight release, 자원 재사용은 측정하지 않는다.

## 장치·검증 조건

SDL pin `3b2bc951500ca6a1f428af77e036e732d9a8afe3`, Vulkan-Headers pin
`b379292b2ab6df5771ba9870d53cf8b2c9295daf`다. SDL 공개 Vulkan 옵션으로 instance 1.1
(4198400)을 요청한다. native_api_version은 physical device 지원 버전이다.
SDL native instance/device handle 캡처는 아니다. 고유한 정확 이름 → native Vulkan UUID,
driver UUID/LUID → DXGI 일치를 확인하고 CPU/OS/RAM/driver/compiler를 원본에 기록했다.

Child의 상속 VK_/VVL_/SDL_ 설정 제거와 implicit layer 비활성화를 수행했다.
정확성 프로세스는 SHA로 고정한 Khronos validation DLL과 synchronization/submit-time
설정, SDK error counter, SDL error logs 및 양 stream의 validation ERROR를 검사한다.
SDL fallback 차단용 SDK required-layer 생성과 loader trace를 보존했다.
그 뒤 성능 프로세스는 validation DLL 미로드를 확인하고 순차 실행했다.
별도 GPU 검사와 측정을 겹치지 않았고 시스템/driver 설정을 바꾸지 않았다.

## 실제 결과

장치×SDK×작업별 독립 프로세스 3개, warmup 5회 / 측정 30회, seed 20261027이다.
**측정 반복 1,440회 / 별도 CPU 구간 관측값 2,880개**다. Validation과 warmup 시간은
통계에서 제외하고 outlier를 삭제하지 않았다. P50/P95/P99, MAD, min/max와 각 process
median을 JSON/CSV에 보존했다. 아래 값은 batch당 P50이며 전부 ms다.

{table}
공개 생성·해제 지연은 위 논리 작업과 API 기본 정책에 한정한다. Dedicated allocation과
pooling 등 정책이 다르므로 특정 native 함수가 차이의 원인이라고 확정하지 않는다.
SDK 전체 순위, 물리 메모리 효율·회수 속도, GPU 처리량 또는 다른 API의 우위를 뜻하지 않는다.
이전 mixed-backend allocation 및 Vulkan upload/draw/PSO 결과는 계약이 다르므로 합산하지 않는다.

## 검증·감사·재현

정확성 workload 16개 + 성능 workload 48개에서 **872,872개 logical resource**를 생성·사용·
공개 해제했고 전체 데이터를 확인했다. **버퍼 436,436개 / 이미지 436,436개 /
5,986,156,176 tight bytes**다. Native hardware counter나 GPU capture로 handle 개수를
입증한 자료는 아니다. 전체 native 명령은 probes 2 + correctness 16 + measurement 48 +
negative controls 10 = 76개다. Missing layer / 다른 GPU / 잘못된 option은 결과 없이 실패했다.
새 CTest 3/3, Python 387/387(신규 계약 48개), CPU reference 53 assertions,
report 변조 controls 8개와 라이선스 negative controls 2개가 통과했다.

SDK fingerprint `{x['SDK_source_fingerprint']}`, benchmark fingerprint
`{x['source_fingerprint']}`다. 제품 SDK는 그대로이며 과거 93개 선택 CTest는 이번에
재실행하지 않았다. 기존 SDK ZIP 23개(기각된 실험 package 포함), 이전 PSO/draw/upload
원본·source ZIP을 SHA로 보존했다. 새 SDK binary package는 만들지 않았다.
장시간 메모리 기준과 과거 Intel AV 조사는 보류이며 하드웨어 원인을 확정하지 않았다.

[라이선스 inventory](../licenses/generated/windows-benchmarks-vulkan-allocation-release/inventory.json)는
648 conservative inputs / 21 components / 새 compiler·linker read logs 각 2개다.
실제 compiler/linker 입력과 기존 SDK/SDL static closure를 검사했다. 이번 실행은 shader가
필요 없다. 과거 SDK closure의 보수적 shader 항목은 남아 있으며 사용 증거를 뜻하지 않는다.
Symbol reachability나 법적 승인을 주장하지 않는다. Source ZIP은 vendor tree·EXE/DLL/LIB/PDB·
dump·VVL runtime을 제외하고 own sources·fixture·scripts·문서·감사 자료를 포함한다.

```powershell
cmake -S benchmarks/vulkan_allocation -B out/your-new-allocation-build -G "Visual Studio 17 2022" -A x64
cmake --build out/your-new-allocation-build --config Release --parallel 1
ctest --test-dir out/your-new-allocation-build -C Release --output-on-failure
python tools/run_vulkan_allocation.py --exe out/your-new-allocation-build/Release/vulkan_allocation.exe --output benchmarks/results/your-new-allocation-run
python tools/report_vulkan_allocation.py --input benchmarks/results/your-new-allocation-run --output benchmarks/reports/your-new-allocation-report
```

고정 prerequisite는 out/vulkan-presentation-install, out/sdl, validation runtime receipt다.
새 build/result root로 재현한다. 이번 감사 build root는 out/vulkan-allocation-benchmark-final이다.
[원본](../benchmarks/results/2026-10-24-vulkan-allocation/results.json),
[요약](../benchmarks/reports/2026-10-24-phase3-vulkan-allocation/summary.json),
[CSV](../benchmarks/reports/2026-10-24-phase3-vulkan-allocation/metrics.csv),
[증거](../benchmarks/reports/2026-10-24-phase3-vulkan-allocation/evidence.json),
[검증 script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-allocation/verification-scripts/verify.py)를 따른다.
내부 allocator/native profiling, reuse/in-flight release, 다른 Vulkan 경쟁 SDK와 Linux/WSI 검증은
남아 있다. 이 baseline만으로 allocator/pooling 최적화를 제품에 추가하지 않았다.
'''
(root/'docs/PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md').write_bytes(text.replace('\n','\r\n').encode('utf-8'))

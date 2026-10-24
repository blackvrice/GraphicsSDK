# Phase 3 — 소형 Vulkan 버퍼 블록 할당 후보 평가

2026-10-24 Windows x64 / MSVC 19.44. 네이티브 관측에서 확인한 resource별 allocation/free를
줄이는 **실제 SDK 후보 구현**을 개발하고, 설치 소비자·기능 회귀·새 성능 비교로 평가했다.
후보는 제품 소스의 별도 `out/` 복사본에 적용했다. **기본 제품에는 채택하지 않았다**.
1,000개 batch 지연은 크게 감소했지만 단일 4KiB 버퍼도 64KiB를 요청하고 생성 지연이
늘었다. 작은 사용량의 예약 비용을 해결하는 정책과 추가 실패 경로 검증이 먼저 필요하다.

## 구현

[코드·설명](../experiments/vulkan_buffer_pool/README.md),
[검토용 diff](../experiments/vulkan_buffer_pool/candidate.patch),
[복사본 생성 도구](../tools/prepare_vulkan_buffer_pool.py)를 제공한다.
생성 도구는 정확한 기존 SDK fingerprint와 한 번만 일치하는 patch anchor를 요구하고,
새 `out/` 경로에만 복사본을 만든다. 제품·vendor 소스를 직접 수정하지 않는다.
원본 SDK source와 후보 root·각 파일 SHA 및 생성 도구를 receipt로 연결했다.

후보는 `Memory::Device` buffer의 native requirements size/alignment가 4KiB 이하일 때
64KiB memory block을 사용한다. 64..4,096 bytes의 power-of-two stride별 슬롯을 bitmap으로
관리하며 memory type과 stride별로 block을 분리한다. Public buffer마다 독립 VkBuffer를
생성하고 block의 서로 다른 offset에 bind한다. Image·Upload·Readback 및 큰 buffer는
기존 dedicated 경로다. Buffer requirements2의 dedicated required/preferred도 존중한다.
필요한 정렬·메모리 타입·범위 조건은
[Vulkan bind 계약](https://docs.vulkan.org/refpages/latest/refpages/source/vkBindBufferMemory.html)을 따르고,
[dedicated 요구 조회](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedRequirements.html)를 사용한다.

Slot lease는 public wrapper가 아닌 기존 native Allocation의 수명에 묶었다. Ended recording과
pending submission이 보존하는 자원이 사라지기 전에는 slot을 반환하지 않는다. 빈 block을
cache하지 않고 마지막 lease가 끝나면 즉시 vkFreeMemory를 호출한다. 약한 block 목록과
block 내부 mutex를 사용하며 retired generation의 자식 정리도 기존 수명 규칙을 따른다.
Block 요청 OOM은 원래 exact-size allocation을 시도하지만 fault injection으로 그 경로를
승인한 자료는 아니다. Device operation thread 정책은 기존과 같고 일반 다중 thread allocator
부하·device loss/OOM 조합을 완료한 구현이라고 주장하지 않는다.

후보 `resourceStatistics`는 uniqueResources를 자원별로 유지하면서 공유 block의 예약 바이트를
한 번만 더한다. Host buffer·image는 기존 allocation bytes다. 이는 SDK가 보유한 Vulkan
요청 바이트이며 OS commitment·residency·driver 내부 할당이나 실제 VRAM 사용량이 아니다.
Public API·ABI와 제품의 통계 동작은 변경하지 않았다.

## 검증

독립 후보 SDK를 D3D12+Vulkan, required Vulkan validation 설정으로 빌드했다.
통합 CTest는 시작부터 `-E '^memory_reclamation_'`로 보류한 3개만 제외했다.
**선택 CTest 57/57**(기존 54 + 신규 3)이 통과했다. 과거 다른 build의 93개 전체 선택
gate를 재실행했다고 주장하지 않는다. D3D12 hardware/GBV/WARP와 Vulkan NVIDIA/Intel의
transfer, textures, draw, vertex/index, bindings, depth, timestamp, presentation,
validation control 및 관련 수명 검사가 포함된다.

신규 CPU bitmap 검사는 34,810 assertions로 잘못된 크기/정렬, 모든 slot의 비중첩,
exhaustion, 부분 반환·재사용, double-free 거부와 size class 조건을 검사했다.
신규 GPU 검사는 validation OFF/ON에서 양 GPU를 실행했다. 1,000개 buffer의 63 block
통계, 부분 slot 재사용, wrapper 해제 뒤 ended recording이 지키는 데이터, 다른 payload로
쓴 신규 buffer와의 비별칭, 1..65,537-byte copy/readback 및 device 종료 뒤 자식·새 generation을
검사했다. 각 mode에서 164,742 bytes가 일치했고 assertions는 OFF 57 / ON 59개다.
새 설치본을 find_package로 사용하는 benchmark/trace 실행 파일도 빌드·실행했다.

Python은 **444/444**(신규 27)다. 최초 정상 fixture 3건의 validation 조건 불일치는
수정한 뒤 통과했으며 초기 실패 로그를 보존했다. Report 변조 controls 8개와 license
negative controls 2개가 통과했다. Missing layer, 다른 GPU, 잘못된 option 12개는 결과 없이
실패했다. 기존 memory policy와 장시간 메모리·Intel AV 보류 상태는 변경하지 않았다.

## 네이티브 증명과 예약 비용

동일한 기존 진단 main/layer를 후보 설치본에 링크하고, 별도 baseline/candidate × 두 GPU ×
buffer1/buffer1000의 **8개 validation trace 프로세스 / 168개 구간**을 실행했다.
각 4회 반복에서 모든 target/helper를 실제 GPU copy/full readback으로 확인했다.
16,016개 buffer / 65,601,536 bytes가 일치했다. 구간별 객체·바이트 conservation과 마지막
live=0, accounting/drop/device cleanup remainder=0을 검사했다.

| Target batch | 기존 Allocate/Free | 후보 Allocate/Free | 기존 요청 bytes | 후보 요청 bytes |
|---|---:|---:|---:|---:|
| 4KiB ×1 | 1/1 | 1/1 | 4,096 | 65,536 |
| 4KiB ×1000 | 1,000/1,000 | 63/63 | 4,096,000 | 4,128,768 |

이 값은 target 생성·공개 해제 구간이며 helper allocation은 별도 구간이다. 모든 buffer의
create/bind/destroy 호출은 N회로 유지된다. 1,000개 batch의 예약 증가는 32KiB(0.8%)지만
단일 buffer는 요청 바이트가 16배다. 후자는 물리 VRAM이 16배라는 뜻이 아니다.
Block free가 관측됐다는 이유로 OS/driver의 실제 메모리 회수나 장시간 plateau를 승인하지 않는다.

## 새 성능 비교

기존 결과를 재사용해 비율을 만들지 않고 **기존 SDK / 후보 / pinned SDL**을 새로 실행했다.
각 GPU×작업×variant마다 독립 process 3개, warmup 5회 + 측정 30회이며 seed 20261029로
순서를 섞었다. buffer1/buffer1000은 4KiB, headless, 한 CPU 기록 thread/한 submission이다.
모든 자원에 반복·자원별 다른 데이터가 실제로 전달되어야 한다.
Create와 public release는 별도 CPU API interval이다. Helpers, GPU 기록·submit·wait,
readback·비교와 idle은 구간 밖이다. SDL의 내부 pooling·deferred disposal과 더 좁은 VERTEX
usage 차이는 포함한다. Public release를 물리 메모리 회수 시간으로 해석하지 않는다.

정확성 process 12개가 먼저 통과했고, 그 뒤 validation/trace DLL 미로드를 확인한
성능 process 36개를 순차 실행했다. Native regression/trace 작업과 성능을 겹치지 않았다.
성능은 **1,080개 측정 반복 / 별도 interval 2,160개**다. Warmup과 validation 시간은
통계에서 제외하고 outlier는 삭제하지 않았다. P50/P95/P99, MAD, min/max와 process별
median을 JSON/CSV에 보존했다. 아래는 batch당 P50, 전부 ms다.

| GPU | 작업 | 기존 생성 ms | 후보 생성 ms | SDL 생성 ms | 기존 해제 ms | 후보 해제 ms | SDL 해제 ms |
|---|---|---:|---:|---:|---:|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | buffer1 | 0.06785 | 0.07255 | 0.00185 | 0.12885 | 0.13815 | 0.00010 |
| NVIDIA GeForce GTX 1660 SUPER | buffer1000 | 54.43110 | 6.21600 | 0.33775 | 157.15185 | 10.47450 | 0.06740 |
| Intel(R) UHD Graphics 770 | buffer1 | 0.03335 | 0.03675 | 0.00360 | 0.02480 | 0.02410 | 0.00010 |
| Intel(R) UHD Graphics 770 | buffer1000 | 30.58680 | 5.17490 | 1.64965 | 32.75280 | 2.87845 | 0.06555 |

이 campaign에서 1,000개 생성·해제는 양 GPU 모두 감소했다. 단일 생성은 양 GPU에서
소폭 증가했고 NVIDIA 단일 해제도 늘었다. 이전 campaign과 이번 baseline의 절대값도
다르므로 옛 숫자와 섞지 않는다. Driver 내부 원인, 전체 SDK 순위, GPU 실행 시간이나
다른 크기·사용 패턴의 우위를 주장하지 않는다.

성능·정확성 process에서 **654,654개 buffer / 2,681,462,784 bytes**, 별도 trace에서
16,016개 / 65,601,536 bytes를 확인했다. Native 실행 명령은 probes 2 + trace 8 +
correctness 12 + measurement 36 + controls 12 = **70개**다.

## 결론과 재현

이 고정 block 후보를 **제품 기본값으로 채택하지 않는다**. 대량 생성의 효과는 관측됐으나
작은 사용량의 예약·지연 회귀가 있고, fault-injected OOM/bind failure와 더 다양한 workload의
정책 검증도 남아 있다. 다음 후보는 첫 자원/낮은 점유율의 예약 비용을 제한하는 정책을
같은 검증 방식으로 평가해야 한다. 검토 가능한 실제 구현·diff와 결과를 실험으로 보존했다.

제품 fingerprint `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`, 후보 fingerprint
`6a675e995d9ec778aacbc8046bee660292b80aa182c23b0db61b32757bc81476`다. 공통 allocation benchmark fingerprint는
`9be610abfe51bd2ac20b6f07f3816bae3040d09bab776b2a657393ced7be3a9c`, trace는 `4e00b0bc59f4674efdbc4414356ac18f1c566d6664b8b4443703f0aab1fc6ea9`다.
기존 SDK package 23개와 이전 allocation/PSO/draw/upload/native trace 증거를 SHA로 보존했다.
새 SDK binary package는 만들지 않았다.

[Dependency inventory](../licenses/generated/windows-vulkan-buffer-pool-candidate-release/inventory.json)는
730 conservative inputs / 21 components / compiler read logs 35개 / linker logs 34개다.
후보 SDK와 설치 소비자의 실제 compiler/linker 입력, 후보 source receipt, 보존한 SDK/SDL
closure와 notices를 검사했다. 새로운 third-party allocator나 vendor 수정은 없다.
Symbol reachability나 법적 승인을 뜻하지 않는다. Source ZIP은 own sources, fixtures,
실험 코드·patch·scripts·문서·audit를 포함하며 vendor tree·EXE/DLL/LIB/PDB·dump·VVL은 제외한다.

```powershell
python tools/prepare_vulkan_buffer_pool.py --output out/your-new-pool-source
cmake -S out/your-new-pool-source -B out/your-new-pool-build -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_VULKAN=ON -DGFX_ENABLE_D3D12=ON -DGFX_VULKAN_VALIDATION_TESTS=ON -DGFX_BUILD_EXAMPLES=OFF -DGFX_VULKAN_HEADERS=D:/source/GraphicsSDK/.deps/Vulkan-Headers
cmake --build out/your-new-pool-build --config Release --parallel 1
```

VVL child 환경과 memory CTest 제외를 설정하는 실제
[build script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool/verification-scripts/build.py),
[runner](../tools/run_vulkan_buffer_pool.py),
[원본](../benchmarks/results/2026-10-24-vulkan-buffer-pool/results.json),
[요약](../benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool/summary.json),
[CSV](../benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool/metrics.csv),
[증거](../benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool/evidence.json),
[검증 script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool/verification-scripts/verify.py)를 따른다.
현재 profile root는 build script에 고정돼 있으므로 재실행은 새 source/build/install/result root로
분리하고 runner/audit 경로도 맞춰야 한다. Product·vendor 원본을 패치하지 않는다.
Linux, 다른 경쟁 SDK, 실제 물리 메모리/장시간 안정성과 Phase 3 전체 완료는 여전히 별도다.

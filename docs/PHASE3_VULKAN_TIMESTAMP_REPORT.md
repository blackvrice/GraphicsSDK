# Phase 3 — Vulkan GPU 타임스탬프

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

기존 2-slot API에 실제 Vulkan GPU query를 연결했다. 선택 큐 지원 조회, 펜스·availability
검사, 유효 비트 wrap 변환, recording/result/query-pool 수명과 재사용을 구현했다.
공개 API 형태/ABI 변경 없이 SDK와 해당 소비자를 재빌드했다. Presentation·Linux native와
동일 Vulkan 경쟁 측정을 포함한 Phase 3 전체 완료나 성능 우위를 뜻하지 않는다.

## 구현과 계약

선택 graphics queue의 timestampValidBits가 36..64이고 timestampPeriod가 유한 양수일 때
timestampQueries capability를 표시한다. 미지원은 Unsupported이며 CPU 시간으로 GPU 결과를
대체하지 않는다. 지원 장치의 begin마다 2-slot query pool을 생성하고 pass 밖에서 reset한다.
Pool은 recording/submission에 보유하고 device retire 시 device보다 먼저 해제한다.
Pool은 Buffer/Image allocation bytes에 포함하지 않으며 pendingObjectReferences에 한 항목을 더한다.

Slot 0은 TOP_OF_PIPE, slot 1은 BOTTOM_OF_PIPE에 각각 한 번 기록한다. Pass 안/밖 write가
가능하고 index/중복/resolve 후 write 오류는 기존 pair를 바꾸지 않는다. 두 write 뒤 pass 밖에서
한 번 resolve한다. Vulkan은 별도 intermediate readback buffer 없이 host query를 읽으므로
resolve는 result read를 허용하는 논리 계약이다. 1-then-0 역순도 기록·resolve는 가능하지만
read는 InvalidState다. Counter가 같거나 wrap된 경우에도 기록 순서를 이용해 구분한다.

elapsedGpuMilliseconds는 생성 thread에서 poll하고 resolve·성공 submit·해당 fence 완료를
확인한다. 64-bit value/availability 쌍을 WAIT/PARTIAL 없이 읽고 두 availability가 모두 있어야
결과를 반환한다. 미제출·미완료·미resolve는 InvalidState다. 즉시 read가 GPU 완료를 관찰하면
성공할 수 있으며 빈 GPU 구간의 0은 유효하다. 성공 결과는 캐시하고 begin은 이전 결과를 버린다.

변환은 유효 비트 mask를 적용한 modular tick 차이에 timestampPeriod/1,000,000을 곱한다.
Native submit 직전부터 첫 read까지의 steady-clock 상한이 한 counter cycle 이상이면 다중 wrap이
모호하므로 보수적으로 거부한다. 이 host 구간은 GPU duration을 계산하지 않는다. 결과는 일찍
읽어야 하며, 이미 캐시한 값은 나중에도 안정적이다. Calibrated timestamp·CPU/GPU clock correlation·
device-wide absolute time은 제공하지 않는다. Native timestamp 위치가 정한 GPU 구간이며 CPU frame
time이나 benchmark baseline이 아니다. Recording마다 pool을 만드는 초기 정확성 구현이고 pooling
최적화와 성능 평가는 후속이다.

기존 depth shader fixture·SPIRV-Tools local helper/library와 Khronos runtime을 SHA로 확인해
재사용했다. 이번 단계에서 새 shader를 조립하거나 semantic validation을 다시 실행한 것은 아니다.
[재사용 receipt](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-timestamps/reused-shader-receipt.json)는
[이전 검증](PHASE3_VULKAN_DEPTH_REPORT.md)의 원본과 일치한다. SDK의 shader inspector는 여전히
bounded interface 검사이며 외부 검증된 SPIR-V가 필요하다. Vendor tool/source/binary는 배포하지 않는다.

## 최종 검증

Pinned runtime DLL/JSON/lock hash를 확인하고 상속 Vulkan/VVL 설정 제거, 명시적 layer 경로,
빈 설정과 implicit layer 비활성화 요청은 child에만 적용했다. 시스템 등록·레지스트리·드라이버를
변경하지 않았다. 정상 SDK core/synchronization validation ERROR는 0이다.

| Windows 구성 | 결과 |
|---|---|
| 통합 required validation | 선택 52/52, skip 없음 (등록 55개, 보류 메모리 3개 제외) |
| Vulkan-only required validation | 27/27, skip 없음 |
| 두 backend OFF | CPU 10/10, skip 없음 |
| 설치 SDK GPU suite 8개 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

새 CPU conversion suite는 **3,775 assertion / GPU calls 0**을 통과했다. 36/40/48/64비트,
주기 .5/1/10ns, 경계값·3,600개 deterministic interval의 알려진 tick 차이, mask/wrap·같은 값,
잘못된 metadata/host bound, 역순·cycle ambiguity와 변환 overflow를 검사했다.
실제 GPU wrap이나 장시간 cycle 지연을 재현한 것은 아니다.

GPU timestamp suite는 실행당 두 GPU 합계 **311 assertion / 52 measured pairs /
6 dropped query recordings / 6 draws / 18 full images (6 D32) / 331,852,960 texel bytes /
75,644,964 buffer bytes / 6 padded images**를 확인했다.

- Write/resolve/read의 recording·slot·중복·partial·unsubmitted·reverse·pass·thread 계약과
  begin의 이전 결과 폐기, 16회 새 generation, 반복 read의 안정성을 검사했다. 즉시 read는
  완료 시 성공 또는 미완료 InvalidState 중 하나를 허용하며 GPU race를 실패로 만들지 않는다.
- Upload→Device→Readback의 4,099/1,048,579/16,777,216바이트 changing payload를 전부 비교했다.
  별도 pool의 동시 녹화·연속 제출도 대조했다. 실제 copy workload는 양수 GPU 시간을 요구해
  항상 0을 반환하는 구현을 배제했다. CPU로 측정한 제출/대기 구간은 결과의 외부 상한 대조다.
- Mixed RGBA8/RGBA16F MRT+D32의 65×37/1080p/4K를 실제로 draw하고 모든 색·깊이 texel과
  마지막 행까지 sentinel padding을 비교했다. 큰 draw도 양수 GPU 시간을 확인했다.
- Wrapper를 fence 전에 지운 query recording, resource wrapper 해제 후 read, pool 포함 보유
  통계와 전체 회수, pending/cached result가 남은 device retire의 DeviceLost를 검사했다.

통합·Vulkan-only·설치 소비자의 timestamp suite validation 합계는 **156 measured pairs /
54 images (18 D32) / 995,558,880 texel bytes / 226,934,892 buffer bytes**다.
두 driver-only 대조를 포함한 최종 합계는 **260 measured pairs / 90 images (30 D32) /
1,659,264,800 texel bytes / 378,224,820 buffer bytes**다. 기존 CPU SPIR-V 181/61/43 assertion,
일곱 GPU suite와 별도 의도 오류 native probe를 유지했다. Suite별 unknown/extra 인수와
missing-layer 실패 control도 통과했다. 타이밍 수치는 성능 비교 자료로 사용하지 않았다.

## 배포와 후속

SDK fingerprint는 `74e36fd622c0d928b8c351e737794c06f3e2fd7100a699e646d32a9dc6324014`다.
새 패키지는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-timestamps.zip`이다.
[독립 감사](../licenses/generated/windows-sdk-vulkan-timestamps-release/inventory.json)는
80개 compiler input/2개 component/34개 compiler log/33개 linker log, 설치 header/library와 정확한
SDK ZIP payload를 검사한다. 누락 고지·변경 library·추가 library도 거부한다.
MIT/Apache-2.0 고지를 유지하고 SDK ZIP 20개와 과거 근거·D3D12 source·memory policy를 보존한다.

[최종 실행 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-timestamps/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-timestamps/summary.json),
[소스 ZIP·해시 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-timestamps/evidence.json)를 기록한다.
보류 메모리 3개는 처음부터 제외했고 등록·acceptance policy는 그대로다. 전체 등록 92개 통과,
장시간 메모리 acceptance나 Intel AV 원인 해결을 주장하지 않는다. Vulkan presentation, Linux native,
GPU-assisted validation과 동일 Vulkan 경쟁 측정은 후속이다.

Native 계약은 Khronos의 [timestamp write](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html),
[query results](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html),
[valid bits](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html)를 따른다.

# Vulkan 제품 생성 출력 소유권 수정

2026-10-24 Windows x64 / MSVC 19.44. 앞선 격리 allocator 후보에서 확인한 오류 출력
소유권 문제를 제품 Vulkan 생성 API 전반으로 점검하고 **제품 코드에 수정**했다.
일반 단일 출력 생성의 **29개 지점**은 성공한 뒤에만 native 객체의 소유권을 받는다.
Public API/ABI, host memory 선택, 자원별 dedicated allocation 정책은 유지한다.
**소형 buffer pooling은 채택하지 않았다.**

## 제품 변경

[Creation.hpp](../src/backends/vulkan/Creation.hpp)의 private `acquireOutput`은 임시
핸들로 생성 출력을 받고 `VK_SUCCESS`에 해당하는 zero 결과에서만 destination으로
옮긴다. 오류 출력은 읽거나 소유하지 않는다. 원래 VkResult를 그대로 반환하므로
ErrorCode/native error mapping과 호출 횟수를 유지한다. 기존 코드의 validation·입력
검사와 정상 수명 규칙은 바꾸지 않는다.

[Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp)와
[Presentation.hpp](../src/backends/vulkan/Presentation.hpp)의 buffer/image/memory/view,
command/query/descriptor pool, sampler/shader/layout/render-pass/framebuffer/fence,
instance/debug messenger/device와 Win32 surface/swapchain/semaphore 생성을 적용했다.
이전에 성공한 객체는 기존 RAII가 정리한다. Swapchain 재생성 실패 시 old swapchain의
retirement 규칙도 유지한다. 명령·descriptor 배열은 현재 한 개씩 요청하지만 같은
성공 후 이전 helper로 받는다. Pipeline 생성은 의도적으로 별도 취급한다.

[Vulkan 오류 계약](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-errorcodes)은
일반 오류 출력의 내용을 보장하지 않는다. 반면
[command buffer 배열](https://docs.vulkan.org/refpages/latest/refpages/source/vkAllocateCommandBuffers.html),
[descriptor 배열](https://docs.vulkan.org/refpages/latest/refpages/source/vkAllocateDescriptorSets.html)은
실패 시 생성분을 정리하고 null로 반환해야 한다.
[pipeline 배열](https://docs.vulkan.org/spec/latest/chapters/pipelines.html#pipelines-multiple)은
부분 성공의 non-null 결과를 호출자가 정리해야 한다. 제품의 `CreateGraphicsPipelines`
(현재 count=1)는 기존 출력 RAII를 유지하고 설명을 추가했다. 무조건 출력값을 버리는
helper로 바꿔 부분 성공 자원을 누수시키지 않는다.

MapMemory의 지역 포인터는 실패 시 즉시 반환해 읽거나 unmap하지 않는 경로임을
확인했다. Borrowed queue/swapchain image는 생성 소유권 대상이 아니다.
[guarded-call-sites.json](../benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership/guarded-call-sites.json)에
29개 적용 지점을 기록하고, 실제 source diff를 증거 snapshot에 보존했다.

## 실제 구현에 PFN을 주입한 CPU 시험

[시험 코드](../tests/vulkan_creation_fault/README.md)는 실제 제품 Vulkan.cpp를 시험 TU에
포함하고 private native table에 정확한 원형의 가짜 PFN을 넣는다. 제품 API에는 테스트
스위치를 넣지 않았고 Vulkan driver를 로드하지 않는다. 검토된 Vulkan-Headers registry의
Vulkan 전용 원형으로 own stub bodies 84개를 생성했다. Vulkan SC 인자 변형은 제외한다.

동일 시험을 변경 전 원본 snapshot과 현재 제품 소스에 각각 컴파일하고 그에 맞는
설치 SDK를 링크했다. Compiler read logs로 실제 포함된 구현·private header를 확인했다.
**27개 case**는 buffer/image·command/query/fence·shader/layout/pipeline·render pass/
framebuffer·descriptor·swapchain view/semaphore의 오류, 선행 성공 객체 정리와 재시도를
검사한다. Shader/sampler/semaphore의 두 번째 생성 실패도 포함한다.
오류 출력은 가짜 non-null 값이며, null 보장이 있는 배열 API는 규정대로 null이다.
가짜 ledger는 잘못된 정리를 검출하고 driver로 전달하지 않는다.

| 실패 지점 | 수정 전 잘못된 정리 | 수정 후 잘못된 정리 |
|---|---:|---:|
| buffer | 1 | 0 |
| buffer-memory | 1 | 0 |
| upload-memory | 1 | 0 |
| readback-memory | 1 | 0 |
| image | 1 | 0 |
| image-memory | 1 | 0 |
| image-view | 1 | 0 |
| command-pool | 1 | 0 |
| command-array | 0 | 0 |
| query-pool | 1 | 0 |
| submit-fence | 0 | 0 |
| sampler | 0 | 0 |
| sampler-second | 0 | 0 |
| set-layout | 1 | 0 |
| shader | 1 | 0 |
| shader-second | 1 | 0 |
| pipeline-layout | 1 | 0 |
| pipeline-pass | 1 | 0 |
| pipeline-array | 0 | 0 |
| pass | 1 | 0 |
| framebuffer | 1 | 0 |
| descriptor-pool | 1 | 0 |
| descriptor-array | 0 | 0 |
| swapchain | 1 | 0 |
| swap-view | 1 | 0 |
| swap-semaphore | 1 | 0 |
| swap-semaphore-second | 1 | 0 |

수정 전 **21회**, 수정 후 **0회**의 잘못된 정리를 관측했다. 각 variant는 실패와
정상 재시도에서 유효 fake 객체 **254개를 생성·파괴**, live=0을 유지하고
**270 assertions**가 통과했다. 이 값은 GPU 작업·물리 메모리 측정이 아니다.
Instance/device/debug messenger/Win32-surface 생성의 loader-level 실패 주입은 이 시험에
포함하지 않는다. 해당 호출은 source audit와 generic helper의 pointer/integer 형태
**3640 CPU assertions**로 확인했고, 정상 loader/validation/WSI는 하드웨어 회귀가 담당한다.
모든 Vulkan 장애의 실험적 승인을 의미하지 않는다.

## 실제 하드웨어 확인

이전 own guarded explicit layer/client를 수정 없이 새 제품·이전 제품 설치본에 링크했다.
NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 × validation OFF/ON에서 control,
dedicated buffer OOM, Upload OOM, Readback OOM, fresh bind OOM의
**20개 정상 process / 60회 failure-retry cycle**이 통과했다. 합성 오류 **48회**,
full GPU readback **132회 / 589,824 bytes**를 확인했다.
기존 guard 데이터 보존, 오류 code, retry, native allocation/bind/destroy/free counts,
요청 bytes conservation, 마지막 live=0과 암묵적 device cleanup=0을 검사했다.

이전 제품의 dedicated OOM **4개 process / 12회 cycle**에서는 가짜 메모리 핸들의
잘못된 free **12회**를 layer가 안전하게 차단했다. 새 제품은 0회다. 누락 layer·잘못된
GPU·CLI controls **8개**는 결과 없이 실패했다. Probe를 포함해 명령은 **33개**다.
이는 실제 GPU 메모리를 고갈시키거나 device loss/TDR를 일으킨 시험이 아니다.
VVL/synchronization validation은 자식 환경에만 적용하고 pinned DLL 경로를 확인했다.

## 제품 회귀·감사·보존

새 root source/build/install 경로에서 D3D12+Vulkan을 MSVC `--parallel 1` 빌드했다.
통합 CTest는 처음부터 `-E '^memory_reclamation_'`로 보류한 3개만 제외하고
**55/55**(기존 제품 54 + 신규 helper 1)가 통과했다. 이전 격리 pool 후보의 추가 시험
등록과 혼동하지 않는다. Draw/bindings/depth/timestamp/presentation과 D3D12
hardware/GBV/WARP 및 Vulkan 양 GPU validation/lifetime 회귀를 포함한다.
Python **526/526**(신규 20), report 변조 controls **11개**, audit controls **2개**가 통과했다.
초기 CPU harness 빌드는 registry의 Vulkan/Vulkan SC 중복 인자로 실패했으며, 원형을
Vulkan으로 필터링한 뒤 새 `-v2` build에서 통과했다. 최초 실패 로그도 보존했다.

제품 fingerprint는 `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`에서
`2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`로 바뀌었다. Public include의 bytes는 동일하다.
변경 전 자체 소스를 `out/vulkan-creation-before-source`에 원래 fingerprint와 파일 SHA로
보존했고 source ZIP에도 포함했다. 과거 증거가 참조하는 변경 전 product source 경로는
그 복사본의 같은 bytes로 검증한다. 허용한 3개 product 경로 이외의 변경을 우회하지 않는다.
이전 보고서·inventory·runtime·SDK ZIP **23개**는 그대로 보존했다.

이전 실험 도구는 당시 source fingerprint를 고정하므로 새 제품 root에 그대로 실행하면
거부될 수 있다. 과거 재현은 이전 commit/source ZIP 또는 보존한 original source에서 한다.
새 [audit](../tools/audit_vulkan_creation_ownership.py)는 현재 제품 입력과 과거 원본을
구분한다. [inventory](../licenses/generated/windows-vulkan-creation-ownership-release/inventory.json)는
**909 conservative inputs / 21 components**, compiler read **36** / linker read **35**개다.
가짜 PFN 원형에 사용한 pinned registry input도 추가했다. 새로운 third-party dependency나
vendor 변경은 없고 법적 승인·symbol reachability 또는 바이너리 배포의 주장은 아니다.
Source ZIP은 own source·pre-fix snapshot·fixtures·scripts·문서·audit이며 vendor trees,
EXE/DLL/LIB/PDB/dump/ETL/VVL runtime을 포함하지 않는다.

## 범위와 재현

이번에는 소유권 방어를 **제품에 적용**했다. Pool 정책 채택, 새로운 성능 우위나 물리
메모리 회수는 주장하지 않는다. **새 성능 측정은 하지 않았고** 이전 allocator timing을
새 제품 fingerprint의 실측값으로 옮기지 않는다. 혼합 size/점유율/churn pool 정책과
performance acceptance, actual heap exhaustion/device loss 조합, Linux native 검증은 별도다.
장시간 메모리 기준과 과거 Intel AV 원인 조사의 보류도 유지한다.

[build](../benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership/verification-scripts/build.py),
[consumer build](../benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership/verification-scripts/build-consumers.py),
[runner](../tools/run_vulkan_creation_ownership.py),
[원본](../benchmarks/results/2026-10-24-vulkan-creation-ownership/results.json),
[summary](../benchmarks/reports/2026-10-24-vulkan-creation-ownership/summary.json),
[CPU cases CSV](../benchmarks/reports/2026-10-24-vulkan-creation-ownership/cpu-cases.csv),
[evidence](../benchmarks/reports/2026-10-24-vulkan-creation-ownership/evidence.json),
[verifier](../benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership/verification-scripts/verify.py)를 따른다.
현재 프로파일은 새 출력 경로가 필요하며 과거 산출물·전역 설정을 덮어쓰지 않는다.

# Phase 3 — D3D12/Vulkan 기능·미지원 범위

2026-10-25 · 작업 `P3-SCP-01` · 검토 기준 커밋 `3bffac0`.

이 문서는 현재 공개 API와 구현을 대조한 **지원 계약 검토**다. SDK 코드·ABI를 변경하거나 새 GPU 실행을 수행한 결과가 아니다. 검토한 제품 SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`이다. 아래 기존 실행 근거는 각 보고서의 당시 fingerprint·장치·구성 범위이며, 현재 코드 전체에 대한 새 회귀 통과로 재해석하지 않는다.

초기 Phase 3의 필수 범위는 Windows/Linux Vulkan, 기존 기본 그래픽스 RHI의 결과 비교, 명시적 동기화·수명·오류 계약, 동일 Vulkan 환경의 경쟁 비교다. 원래 RHI 요구에 있는 Debug Marker도 필수 잔여에 포함한다. 미지원 응답을 올바르게 반환하는 것과 해당 기능을 구현한 것은 별개다. 전체 잔여 상태와 완료 기준은 [로드맵](ROADMAP.md), 검토 입력 해시는 [검토 receipt](../benchmarks/reports/2026-10-25-phase3-scope/summary.json)에 연결한다.

## 공개 capability 해석

아래 값은 지원 코드의 조건을 뜻한다. 실제 생성·포맷·자원 한도 검사는 별도로 성공해야 한다. `backendCompiled`는 빌드에 포함됐다는 의미이며 loader/ICD/GPU/표시 환경의 실행 성공을 보증하지 않는다.

| 공개 필드 | D3D12 현재 계약 | Vulkan 현재 계약 | 확인 위치·검사 ID |
|---|---|---|---|
| `backend`, `adapterName`, `adapterId` | 실제 선택 adapter, backend 범위의 LUID 기반 ID | 실제 선택 adapter, 16-byte UUID 해시 기반 ID. 충돌·없는 ID·종류 불일치 거부 | [Types](../include/graphics/Types.hpp), `adapter_selection_*`, `vulkan_transfer`; `P3-WIN-01`, `P3-LNX-04` |
| `softwareAdapter` | 선택한 hardware/WARP 종류 | native CPU device 종류. 현재 Windows 증거는 두 hardware GPU이며 software 실행 증거 아님 | `adapter_contracts`, `vulkan_transfer`; `P3-LNX-01`, `P3-SCP-02` |
| `timestampQueries` | true; queue timestamp frequency 기반 | 선택 graphics queue의 유효 비트·period 조건을 만족할 때 true | `vulkan_timestamp_math`, `vulkan_timestamps`; `P3-WIN-10`, `P3-LNX-10` |
| `compute` | false | false. 공개 compute pipeline/dispatch API도 없음 | [CommandList](../include/graphics/CommandList.hpp), [Pipeline](../include/graphics/Pipeline.hpp); 후속 compute 확장 |
| `swapChain` | true; Win32 surface 생성은 별도 검사 | Win32 instance/device 확장과 `swapchainMaintenance1` feature 충족 시 true. 선택 queue의 surface 지원은 생성 시 검사 | `swapchain_guards_*`, `vulkan_presentation`; `P3-WIN-11`, `P3-SCP-02`, `P3-WSI-01`~`08` |
| `resourceBinding`, `bindingLayouts` | true; texture/constant와 immutable sampler layout | true; 지원 SPIR-V interface와 adapter descriptor 한도 내에서 사용 | `binding_layout_contracts`, `resource_bindings_*`, `vulkan_resource_bindings`; `P3-WIN-08`, `P3-LNX-08` |
| `vertexInput`, `indexedDrawing` | true | true. UInt32 전체 index 범위는 추가 native feature가 필요하며 없으면 UInt32 bind는 Unsupported | `vertex_layout_contracts`, `vertex_input_*`, `vulkan_vertex_input`; `P3-WIN-07`, `P3-LNX-08` |
| `depthAttachment` | true; D32 계약 | D32 optimal image의 depth-attachment+transfer usage와 sample 1 조합 지원 시 true | `render_pass_contracts`, `render_pass_*`, `vulkan_depth`; `P3-WIN-09`, `P3-LNX-09` |
| `multipleRenderTargets` | true; 공통 최대 8개 | adapter의 `maxColorAttachments >= 2`일 때 true. 실제 수는 min(8, native limit) | `vulkan_color_pass`, `vulkan_draw`, `vulkan_depth`; `P3-WIN-05`, `P3-LNX-09` |
| `deviceRecovery` | true; 손실 generation 퇴역 후 새 device 반환 | false; 살아 있는 정상 generation의 `recover()`는 Unsupported | `device_recovery_*`, `vulkan_transfer`; `P3-CAP-02` |
| `deviceLossInjection` | `ID3D12Device5` 조회 성공 여부. 실제 제거에는 desc opt-in도 필요 | false; `enableDeviceLossTesting=true` 생성과 정상 device의 `removeForTesting()`는 Unsupported | `device_recovery_*`, `vulkan_transfer`; `P3-CAP-03` |

`DeviceCaps`에는 **debug marker, OS memory budget, validation, 일반 shader reflection 지원 필드가 없다**. 존재하지 않는 bool을 false로 설명하지 않는다. 해당 API/생성 요청의 Result와 아래 계약으로 구분한다. 손실·퇴역·잘못된 thread/state가 먼저 적용되면 아래 정상 상태의 Unsupported 대신 그 오류가 반환될 수 있다.

## 기본 그래픽스·포맷 비교

여기의 “필수”는 지원 플랫폼의 초기 공통 subset과 정확성을 뜻한다. 모든 native 포맷·기능을 요구한다는 뜻은 아니다.

| 기능 / 구분 | D3D12 | Vulkan | 검사·잔여 ID / 기존 근거 |
|---|---|---|---|
| Device/adapter, buffer, command list/queue/fence / 필수 | 구현 | Vulkan 1.1+graphics queue, exact 선택, 동적 loader로 구현 | `P3-WIN-01`, `P3-LNX-04`~`05`; [전송](PHASE3_VULKAN_TRANSFER_REPORT.md) |
| Host/device 전송·상태·버전 / 필수 | Upload/Device/Readback, 명시적 전환 | 동일 memory 종류, noncoherent flush/invalidate, transfer/host barrier, stale recording 거부 | `vulkan_transfer`, `P3-LNX-05`; [계약](VULKAN.md#contract) |
| RGBA8UNorm/RGBA16Float 2D / 필수 | 전송·render target·sampling | 전송·render target·sampling. 실제 image format/usage/extent/filter 지원 확인 | `vulkan_texture_transfer`, `vulkan_resource_bindings`, `P3-LNX-06`; [texture](PHASE3_VULKAN_TEXTURE_REPORT.md), [bindings](PHASE3_VULKAN_BINDINGS_REPORT.md) |
| D32Float / 필수 | 전송·depth attachment | 전송·depth attachment, depth-only/read-only pass. depth sampling 미지원 | `vulkan_depth`, `P3-LNX-09`; [depth](PHASE3_VULKAN_DEPTH_REPORT.md) |
| Texture footprint / 필수 | 256-byte rowPitch staging layout | 동일 공개 layout. 한 mip/layer/sample, 전체 image·offset 0 copy. native allocation 크기와 구분 | `vulkan_texture_transfer`, `vulkan_depth`; `P3-LNX-06`, `P3-LNX-09` |
| RenderPass / 필수 | Clear/Load/Store, color/MRT/선택 D32 | 같은 초기 계약. 초기화 전 Load·pass 중 copy/transition·서로 다른 extent 거부 | `render_pass_contracts`, `vulkan_color_pass`, `vulkan_depth`; `P3-LNX-07`, `P3-LNX-09` |
| Graphics pipeline·draw / 필수 | SM5.1 DXBC, triangle list, scissor, blend/depth | 검증된 SPIR-V 1.3/Vulkan 1.1 VS/FS, triangle list, scissor, blend/depth. 모든 SPIR-V를 지원하지 않음 | `spirv_interface_contracts`, `vulkan_draw`; `P3-LNX-07`; [draw](PHASE3_VULKAN_DRAW_REPORT.md) |
| Vertex/index/instance / 필수 | Float~Float4/UNorm8x4, UInt16/32, instance views | 동일 기본 입력. SPIR-V location/type, native format/limit 확인. index 실제 값의 유효 범위는 호출자 책임 | `spirv_vertex_contracts`, `vulkan_vertex_input`; `P3-LNX-08`; [vertex](PHASE3_VULKAN_VERTEX_REPORT.md) |
| Resource binding / 필수 | texture/constant 배열, layout·static sampler·immutable snapshot | 지원 float Texture2D/float4 uniform block subset, VS/FS visibility, descriptor 한도 내 동일 계약 | `spirv_binding_contracts`, `vulkan_resource_bindings`; `P3-LNX-08`; [bindings](PHASE3_VULKAN_BINDINGS_REPORT.md) |
| GPU timestamp / 필수 | 2-slot 구간, queue frequency | 2-slot query, availability/fence·wrap·period 계약. CPU 시간으로 대체하지 않음 | `vulkan_timestamp_math`, `vulkan_timestamps`; `P3-LNX-10`; [timestamp](PHASE3_VULKAN_TIMESTAMP_REPORT.md) |
| Debug Marker / **필수 잔여** | `SetMarker` 구현 | 정상 recording의 `debugMarker()`는 Unsupported. validation용 debug-utils callback이 label 구현을 뜻하지 않음 | **`P3-CAP-01`을 조건부에서 필수로 변경**, 실제 label 관측·thread/state/수명 검증 필요 |
| SwapChain / 필수, 지원 조합 제한 | Win32 RGBA8 | Win32 RGBA8와 present 완료 보장 조건. Linux WSI 미구현 | `vulkan_presentation`, `P3-SCP-02`, `P3-WSI-01`~`08`; [출력](PHASE3_VULKAN_PRESENTATION_REPORT.md) |
| Shader compiler/reflection / 후속 단계 | Windows D3D bootstrap DXBC compile/reflection | `compileHlsl`은 SPIR-V 생성 안 함. `reflectShader(SPIRV)`는 Unsupported. 내부 bounded interface 검사는 일반 reflection/semantic validator가 아님 | `P5-SHD-01`~`08`; [Shader](../include/graphics/Shader.hpp), [구현](../src/shader/Shader.cpp) |
| Compute·RenderGraph·추가 포맷·stencil/MSAA/mip/array / 후속 확장 | 초기 공통 API 밖 | 초기 공통 API 밖 | 원래 단계별 shader/최적화 및 후속 릴리스 범위 유지. compute benchmark는 SDK compute 지원 이후 실행 |

## 동기화·수명·오류 계약

| 검사할 계약 | 현재 비교 결과와 한계 | 검사 ID / 다음 수용 |
|---|---|---|
| 명령 거부의 원자성 | 상태·범위·foreign/generation 검사를 통과하기 전 새 참조/version을 보유하지 않는다. 잘못된 요청 뒤 기존 유효 상태를 보존한다 | `copy_atomicity_*`, `vulkan_transfer`, `vulkan_color_pass`, `vulkan_resource_bindings`, `vulkan_depth`; `P3-LNX-05`~`09` |
| Native resource 보유·퇴역 | wrapper 소멸 이후 recording/submission이 native 자원을 보유한다. device 종료는 drain/retire, 남은 child native 연산은 DeviceLost. 이는 `recover()` 구현과 별개다 | `vulkan_transfer` 및 각 resource suite; `P3-LNX-05`, `P3-GATE-01` |
| Fence와 timeout | generation-scoped monotonic fence, 완료 조회/0 poll/유한 timeout/재시도. Vulkan의 실제 timeout 발생을 모든 실행에서 보장하지 않는다 | `fence_wait_*`, `vulkan_transfer`; `P3-LNX-05` |
| 실제 Vulkan native loss | 관측된 `VK_ERROR_DEVICE_LOST`를 저장하고 이후 native 사용을 막는 경로는 존재. 예상 밖 TDR·hot-unplug·실제 loss 관측 수용은 미완료 | `P2-ACC-05`는 기존 D3D12 별도 수용; Vulkan은 `P3-GATE-01`에서 지원·미관측 범위 명시. 합성 장애를 실제 TDR로 세지 않음 |
| Creating thread | native 연산은 생성 thread에서 수행. 오류·퇴역의 우선 순위와 CPU description 접근은 해당 API 계약 적용 | `vulkan_transfer` 및 GPU suite; `P3-LNX-05`~`11` |
| Present 완료·재생성 | Vulkan은 image별 semaphore+maintenance1 present fence로 teardown 수명을 보장. monitor scanout/육안 승인 의미 아님. OUT_OF_DATE/SURFACE_LOST와 실패한 재생성은 명시적 재시도 경로 필요 | `vulkan_presentation`; `P3-SCP-02`, `P3-WSI-03`~`08` |
| Native 생성 실패 | loader/생성 output ownership의 제품 수정은 구현됨. 별도 buffer-pool 후보의 allocation fault 관측은 제품 풀 채택 증거 아님 | `vulkan_creation_ownership`, `P3-WIN-12`~`13`; [최적화 기록](OPTIMIZATION_LOG.md), `P6-POOL-01`~`05` |

## 조건부 기능의 처리 결정

원래 필수 요구인 marker는 위에서 필수로 복구했다. 아래 기능은 초기 기능 subset에서의 처리 결정을 기록하며 **어느 자식 작업도 이 문서만으로 완료 체크하지 않는다**. `P3-GATE-01`과 `P3-GATE-03`은 선택·제외 이유와 실제 미지원 응답을 최종 구성에서 재확인해야 한다.

| 작업 | 현재 상태 / 초기 Phase 3 처리 | 이유·남겨 둘 검사 |
|---|---|---|
| `P3-CAP-01` Debug Marker | **필수, 미완료** | 원래 RHI §11의 명시 요구와 D3D12 API에 존재. 미구현을 이유로 선택 기능으로 제외할 수 없음. 실제 label을 관측하고 미지원 환경·상태·수명 검사 |
| `P3-CAP-02` generation recovery | 조건부, 초기 subset에서는 미지원 유지 | 원래 안정성의 Device Loss 처리는 손실 보존·native 사용 차단·안전한 퇴역이 필수다. 모든 backend의 자동/동일 API recovery 성공은 별도 확대 범위로 둔다. D3D12 recover와의 차이를 공개하며 Vulkan caps=false/정상 상태 Unsupported를 최종 회귀에서 확인 |
| `P3-CAP-03` loss injection | 조건부, 초기 subset에서는 미지원 유지 | D3D12의 opt-in native 제거 시험을 Vulkan에 동일 기능으로 주장하지 않는다. 선택 시 시험 방식과 native/합성 차이를 구현·검증. 원래 loss 처리 요구와 실제 TDR 수용을 제거하지 않음 |
| `P3-CAP-04` OS memory budget | 조건부, 공개 API의 Unsupported 유지 | 공개 계약은 정확한 adapter/process의 usage·budget·예약 필드다. heap capacity나 SDK 소유 bytes로 성공을 합성할 수 없다. 같은 의미를 제공할 근거가 확보되면 별도 구현 검토. 검토 작업 자체는 미완료 유지 |
| `P3-CAP-07` GPU-assisted validation | 조건부, 초기 subset에서는 추가하지 않음 | 필수 검증은 실제 core+sync validation과 오류 검출 controls다. GPU-assisted는 별도 계측/오류 control 경로가 필요하고 기존 sync validation 결과로 대체할 수 없음 |
| `P3-CAP-05` Intel 출력 확대, `P3-CAP-06` 추가 GPU/driver | **후속 `P3-SCP-02`에서 정책 결정 완료, 두 구현/검증 작업은 미완료 유지** | [Windows 지원 정책](PHASE3_WINDOWS_SUPPORT.md)의 정확한 조합을 초기 후보로 제한하고 legacy fallback은 미채택. 범위 확대는 해당 ID의 새 근거 필요. 특정 driver 기능 부재와 과거 Intel AV 원인을 연결하지 않음 |

장시간 메모리 수용 `P2-ACC-01`과 [Intel AV 원인 조사 `P2-ACC-02`](PHASE2_INTEL_CRASH_RESUMPTION_REPORT.md)는 2026-10-25 사용자 지시로 각각 재개했고 필수 미완료다. 하드웨어 원인 확정이나 완료 판정으로 바꾸지 않는다. Pooling/upload ring/cache 등 성능 정책은 Phase 6이며 marker 등 필수 기능을 대신하지 않는다.

## 플랫폼·검증·측정 수용 경계

| 범위 | 현재 코드/기존 근거 | 필수 잔여 검사 |
|---|---|---|
| Windows D3D12 | 기본 그래픽스·통제된 recovery·memory budget 구현과 기존 보고서. 장시간/실제 표시·물리 사건 수용은 별도 | 재개 `P2-ACC-01`·`02`, 실제 수용 `P2-ACC-03`~`05` 미완료 |
| Windows Vulkan offscreen | 기존 NVIDIA/Intel 기본 GPU suite 보고서. 모든 장치·driver·현재 source의 새 측정은 아님 | `P3-SCP-02`, `P3-GATE-01` |
| Windows Vulkan WSI | 기존 NVIDIA 실제 present 경로, 해당 Intel driver의 caps=false/Unsupported | `P3-SCP-02`, 조건 선택 시 `P3-CAP-05`~`06`; 사람의 표시 확인과 구분 |
| Linux Vulkan headless | [Native compile/CPU CI](PHASE3_LINUX_COMPILE_REPORT.md), [software ICD loader/device 선택](PHASE3_LINUX_DEVICE_REPORT.md), [버퍼·queue/fence·수명](PHASE3_LINUX_TRANSFER_REPORT.md), [텍스처 전송·texel/padding](PHASE3_LINUX_TEXTURE_REPORT.md), [컬러 pass·기본 draw](PHASE3_LINUX_DRAW_REPORT.md), [입력·바인딩](PHASE3_LINUX_INPUT_REPORT.md), [D32·depth/MRT](PHASE3_LINUX_DEPTH_REPORT.md), [timestamp](PHASE3_LINUX_TIMESTAMP_REPORT.md), [고정 core/sync validation·controls](PHASE3_LINUX_VALIDATION_REPORT.md), [설치 경로 이동·실제 GNU 입력/고지/payload 감사](PHASE3_LINUX_PACKAGE_REPORT.md), [전체 Python CI 정합성](PHASE3_PYTHON_CI_REPORT.md) 성공. [물리 GPU 실행 도구와 software 거부 검증](PHASE3_LINUX_HARDWARE_READINESS.md) 준비. 물리 GPU 미검증; 실제 counter wrap은 미재현 | 실제 Linux GPU 대상 필요, 물리 GPU 수용 `P3-LNX-15` 미완료. software/CPU 성공을 hardware 수용으로 합산하지 않음 |
| Linux Vulkan WSI | 공개 `SurfaceKind`는 Win32만 존재, Linux surface/swapchain 미구현 | `P3-WSI-01`~`08` 필수. API·X11/Wayland 선택, 완료/resize/loss·표시 검증까지 필요 |
| Vulkan validation | `validation=true`는 Khronos layer와 debug-utils/validation-features/layer-settings 필요. core+sync/submit-time 요청, delivered ERROR 집계. 외부 설정 override와 zero-count 한계 존재 | `vulkan_*_validation`, `vulkan_validation_controls`; `P3-LNX-11`, `P3-GATE-01`; [기존 검증](PHASE3_VULKAN_VALIDATION_REPORT.md) |
| CI·installed consumer | native compile/CPU 및 software 공통 suite·설치 소비자의 required validation 성공. 별도 저장소 밖 소비자 9개와 실제 입력·설치 고지·20개 파일 ZIP 감사 완료. 고지는 도구의 설치 후 패키징 단계에서 추가. 현재 전체 CI Python은 Windows/Linux 각각 654개·오류/실패/건너뜀 0. 실제 software의 hardware gate 거부도 검증. 과거 자료와 현재 checkout identity 구분. portable-core 자체는 Vulkan 실행 근거 아님 | 물리 GPU `P3-LNX-15` 미완료, `P3-GATE-01`; 전체 다중 플랫폼 배포는 Phase 7 |
| 동등 결과·경쟁 성능 | 기존 D3D12/SDL/Vulkan 보고서는 당시 workload/fingerprint. API bytes·통계·native 동기화 차이를 보존 | `P3-BEN-01`~`08`, `P3-CMP-01`~`10`, `P3-LBEN-01`~`04`, `P3-GATE-02`. 추가 경쟁 SDK 선택은 `P3-CMP-01`에서 결정 |

`P3-SCP-01`의 완료는 이 지원표·필수/조건부 이유·후속 검사 연결의 완료다. `P3-SCP-02`, marker 구현, Linux 실행, 새 성능 측정이나 Phase 3 최종 수용을 대신 완료하지 않는다. 다음 자동 권장 작업은 로드맵의 선행 조건과 `next_order`로 결정한다.

# Phase 3 — Vulkan 정점·인덱스 입력

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

Vulkan 1.1 backend의 기존 graphics pipeline에 정점 레이아웃과 버퍼 입력,
UInt16/UInt32 indexed draw, instance stream과 float varying 연결을 추가했다.
공개 API 형태와 ABI 변경은 없으며 실제 SDK와 모든 해당 소비자를 재빌드했다.
Descriptor binding·depth·presentation을 포함한 Phase 3 전체 완료는 아니다.

## 입력 계약과 구현

`VertexLayout.attributes`의 배열 순서가 SPIR-V location 0..N-1에 대응한다.
Semantic name/index는 기존 D3D12 매핑용이다. Float/Float2/Float3/Float4와 UNorm8x4를
지원하며, UNorm8x4는 shader float4로 변환된다. 공통 layout 검증에 더해 adapter의
binding/attribute 수, stride/offset 한도와 native format feature를 조회한다.
VS user output과 FS input은 같은 location과 float32 scalar/vector component 수를
요구한다. 기본 보간만 허용하며 정수 attribute, member interface, 특수 interpolation,
descriptor/push/storage interface는 지원하지 않는다.

Device buffer는 copy/vertex/index/combined read state를, Upload는 CopySource와
vertex/index/combined read state를 지원한다. Readback 입력 binding은 거부한다.
Binding은 recording 중 가능하고 같은 recording의 후속 pass에서도 유지된다.
슬롯·stride·offset·view bytes·state를 먼저 검사한 뒤 native 자원을 보유한다.
Draw는 pipeline과 맞는 stride, float alignment, 마지막 attribute extent를 포함한
정점/instance view 범위를 검사한다. 마지막 element 뒤의 불필요한 stride padding은
요구하지 않는다. UInt32는 `fullDrawIndexUint32` feature가 있을 때 활성화한다.
Primitive restart와 UInt8 index는 지원하지 않는다.

Indexed draw는 index view의 firstIndex/count 범위와 양수 baseVertex 하한,
instance stream 범위를 검사한다. **실제 index 값 + signed baseVertex가 view 안의
유효 정점을 가리키는 조건은 호출자의 책임**이다. CPU에서 index 값을 읽거나 검사하지
않는다. 거부된 binding은 기존 binding과 ownership/version을 유지하고, 승인된 입력의
후속 CPU write는 stale submit으로 거부한다. Native 입력은 wrapper 파괴 후에도
recording/submission 수명 동안 보유한다.

Shader 입력은 이전과 같이 외부에서 Vulkan 1.1용으로 검증한 **SPIR-V 1.3**을 요구한다.
SDK의 bounded framing/interface inspector는 전체 semantic validator가 아니다.
새 자체 assembly 7개를 기존 고정 SPIRV-Tools 로컬 helper로 조립·검증했다.
OriginUpperLeft가 빠진 의미 오류 control은 validator가 exit 1로 거부했다.
기존 tool EXE와 prerequisite library의 이전 receipt SHA를 실행 전후 비교했으며
외부 tool/vendor source/binary를 SDK나 소스 ZIP에 넣지 않았다.
[도구 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-vertex/shader-tools/commands.json)과
[별도 receipt](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-vertex/shader-tools/receipt.json)에 기록한다.

## 실제 검증 결과

기존 pinned Khronos validation runtime의 DLL/JSON/lock을 해시 확인 후 재사용했다.
상속 Vulkan/VVL 환경 제거, 명시적 layer 경로와 빈 설정, implicit layer 비활성화
요청은 child process에만 적용했다. 시스템 등록·드라이버·레지스트리 변경은 없다.

| Windows 구성 | 결과 |
|---|---|
| 통합 required validation | 선택 44/44, skip 없음 (등록 47개, 보류 메모리 3개 제외) |
| Vulkan-only required validation | 19/19, skip 없음 |
| 두 backend OFF | CPU 8/8, skip 없음 |
| 설치 SDK buffer·texture·color-pass·draw·vertex 소비자 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

기존 CPU SPIR-V suite 181 assertion을 유지하고 새 typed input/varying suite는
61 assertion을 통과했다. 두 suite 모두 GPU call 0이며 전체 semantic validation을
주장하지 않는다.

정점 suite는 실행당 두 GPU 합계 **771 assertion / 150 draws (78 indexed) /
146 full images / 250,813,720 texel bytes / 138 padded images**를 비교했다.

- Scalar/vec2/vec3/vec4 position, float4/UNorm color와 barycentric 보간을 독립 CPU
  reference로 비교했다. Odd 65×37 이미지의 edge 위 pixel center를 피하며 모든 픽셀과
  sentinel row padding을 검사했다. 두 target format의 indexed 1080p/4K quad도 포함한다.
- Upload/Device, prefix offset, padded stride와 tight 마지막 element 범위,
  UInt16/UInt32, firstIndex=1, baseVertex=-1/0/+1, combined vertex/index allocation을
  실제 GPU에서 검사했다. 슬롯 2의 vertex와 슬롯 5의 instance offset/color stream,
  firstVertex=1/firstInstance=1과 2 instances를 indexed/nonindexed로 비교했다.
- 32회 반복 invalid binding 뒤 capture가 없는지 검사하고 write 후 empty submit이
  성공하는지 확인했다. 승인된 binding 뒤 CPU write는 stale submit으로 거부했다.
  Null/foreign/readback, format/range/alignment/stride/state, 부족한 instance view,
  거부 후 기존 binding, creating thread, wrapper/native 수명과 device retire도 검사했다.

통합·Vulkan-only·설치 소비자의 required validation 합계는 **450 draws / 234 indexed /
438 images / 752,441,160 texel bytes**다. 두 driver-only 대조를 더한 최종 합계는
**750 draws / 390 indexed / 730 images / 1,254,068,600 texel bytes**다.
정상 SDK core/sync validation ERROR는 0이다. 기존 buffer 717, texture 835,
color-pass 593, draw 637 assertion과 native probe 274 assertion도 유지했다.
Probe의 의도 오류는 정상 workload와 분리하며 invalid command를 submit하지 않는다.
각 GPU suite의 unknown/extra 인수와 missing-layer 실패 control도 통과했다.

## 배포 근거와 남은 범위

SDK fingerprint는 `a7be7cfa0d274fbcf54a8097260e4e61c328ad704808dbe1aaaca8290934af1a`다.
새 패키지는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-vertex.zip`이다.
[독립 감사](../licenses/generated/windows-sdk-vulkan-vertex-release/inventory.json)는
70개 compiler input/2개 component/26개 compiler log/25개 linker log와 정확한 SDK
payload를 확인한다. MIT/Apache-2.0 고지를 보존하고 기존 SDK ZIP 17개를 변경하지 않았다.
누락 고지·변경 library·추가 library의 잘못된 패키지도 거부했다.

[최종 실행 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-vertex/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-vertex/summary.json),
[소스 ZIP·해시 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-vertex/evidence.json)를 보존한다.
이전 근거·D3D12 source·memory policy와 로컬 artifact 해시도 재검사한다.

보류 메모리 3개는 처음부터 제외했고 CTest 등록이나 acceptance policy를 바꾸지 않았다.
과거 충돌 원본을 보존하며 장시간 메모리 acceptance와 Intel AV 원인 규명은 여전히 보류다.
전체 74개 등록 검사의 통과, 원인 해결, 성능 향상이나 경쟁 우위를 주장하지 않는다.
Vulkan descriptor binding, depth, timestamps, presentation과 경쟁 측정은 후속이다.
Linux native, GPU-assisted validation, 실제 device loss와 다른 adapter는 미검증이다.

Native 계약은 Khronos의 [vertex binding](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindVertexBuffers.html),
[indexed draw](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexed.html),
[attribute layout](https://docs.vulkan.org/refpages/latest/refpages/source/VkVertexInputAttributeDescription.html)를 따른다.

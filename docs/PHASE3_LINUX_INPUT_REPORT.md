# Phase 3 — Linux 입력·리소스 바인딩 계약

2026-10-25 · `P3-LNX-08` · 검증 커밋 `4e3d525`.

실제 Ubuntu에서 기존 `VulkanVertexTests.cpp`와 `VulkanBindingTests.cpp`를 변경 없이 빌드했다. **Linux software ICD에서 vertex/index/instance 입력과 texture/constant 배열·sampler·snapshot의 전체 이미지 참조 및 경계/foreign/thread/version/lifetime 계약을 완료**한다. 관측 adapter는 CPU 종류의 Mesa lavapipe이며 hardware adapter는 0개다. 물리 GPU의 같은 공통 suite 수용은 최종 gate의 필수 의존성 `P3-LNX-15`에 유지한다.

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40178114363)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40178114363/job/121123118694)이 성공했다. SDK C++·공개 API·root `CMakeLists.txt`·기존 C++ suite/support·SPIR-V fixture header는 변경하지 않았다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-input/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-input/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-input/job.log)에 결과를 보존했다.

## 실제 결과

| 실행 | Assertion | Draw (indexed 포함) | Indexed / snapshot | 전체 비교 이미지 | 검증 texel bytes | Padding 있는 이미지 |
|---|---:|---:|---:|---:|---:|---:|
| 입력 in-tree CTest | 386 | 75 | 39 indexed | 73 | 125,406,860 | 69 |
| 입력 설치 SDK 소비자 | 386 | 75 | 39 indexed | 73 | 125,406,860 | 69 |
| 바인딩 in-tree CTest | 287 | 78 | 129 snapshot | 40 | 124,944,768 | 36 |
| 바인딩 설치 SDK 소비자 | 287 | 78 | 129 snapshot | 40 | 124,944,768 | 36 |
| 합계 | **1,346** | **306** | **78 indexed / 258 snapshot** | **226** | **500,703,256** | **210** |

`vulkan_vertex_input`와 `vulkan_resource_bindings`를 각각 정확히 선택했고 각 JUnit에서 testcase 1개 통과, skip/failure/error 부재를 확인했다. 설치 소비자는 별도 project의 `find_package(GraphicsSDK CONFIG REQUIRED)`로 `vulkan_vertex_consumer`와 `vulkan_binding_consumer`만 빌드해 같은 suite를 실행했다. draw/snapshot 계수에는 계약·통제된 퇴역 경로도 포함되므로 비교 이미지 수와 일대일 대응하지 않는다. Indexed draw는 전체 draw 계수의 부분집합이다.

모든 readback의 **모든 texel·모든 RGBA 채널**을 독립 CPU 참조와 대조하고 finite를 검사했다. 허용 오차는 입력 suite가 UNorm8 `1.01/255`·Float16 `0.002`, 바인딩 suite가 UNorm8 `2.1/255`·Float16 `0.004`다. 따라서 texel bytes는 비교한 데이터 분량이며 bit-exact 색상 일치나 전체 native 전송 트래픽을 뜻하지 않는다. 두 suite 모두 `0xb3` sentinel로 매 행·마지막 행의 모든 padding 바이트 보존을 정확히 검사했다.

## 입력과 바인딩의 실행 범위

| 범위 | 실행한 검사 |
|---|---|
| Vertex format·메모리 | Float/Float2/Float3/Float4 위치와 Float4/UNorm8x4 색상, Upload/Device 경로, nonzero slot/offset/stride·firstVertex, CPU barycentric 색상 보간 |
| Index·공유 버퍼 | UInt16/UInt32, baseVertex -1/0/1·firstIndex·offset, 같은 버퍼의 VertexAndIndexBuffer state, 1080p/4K 전체 사각형 참조 |
| Instance view | vertex/instance 별도 stream, instance slot 5, firstInstance 1·2개 instance, 직접/indexed draw; 작은 instance view 거부 뒤 유효 view로 성공 |
| Texture·상수 배열 | source/target RGBA8/RGBA16F, VS 상수의 UV 변환, FS texture 2개·상수 배열 가중 합, 단계별 register 재사용·nonzero space |
| Sampler·block·메모리 | Point/Linear·Clamp/Repeat, Upload/Device 상수, 32/272-byte shader block, 256/512-byte view·offset 256 |
| Immutable snapshot | 화면 왼쪽 draw 뒤 image 배열 순서를 바꾼 새 set으로 오른쪽 draw; 거부된 set이 이전 snapshot을 보존하고 첫 draw의 descriptor도 유지 |
| Legacy·큰 이미지 | 기존 `bindTexture` shorthand와 render-target→ShaderRead sampling, 두 포맷 1080p/4K readback |

바인딩의 작은 이미지 `68×36`은 source×target×filter×address×memory의 32개 조합이다. 해당 교차 조합에서 작은 block은 Upload, 큰 block은 Device 경로와 함께 선택한다. block 크기와 memory의 모든 독립 교차 조합을 검사했다고 표현하지 않는다. 포맷별 legacy 2회와 큰 이미지 2회를 더해 실행당 40개 이미지를 비교했다. CPU가 texel 양자화·point/linear 보간·wrap/clamp·UV 변환·가중 합을 독립 계산한다.

입력의 일반 참조 크기는 `65×37`이며 큰 indexed 경로는 UInt16 `1920×1080`, UInt32 `3840×2160`이다. UInt32 실행 성공을 모든 가능한 index 값의 실제 GPU fetch 검증으로 확대하지 않는다. index 내용 자체의 유효 범위에 대한 호출자 계약도 그대로다.

## 거부·상태·수명

| 계약 | 실행한 검사 |
|---|---|
| 입력 경계·foreign | slot/stride/alignment·offset/view·index range·base vertex·instance view, null/foreign/readback buffer, 잘못된 index format; 거부 뒤 유효 draw 참조 유지 |
| 입력 상태·version | 입력 state 없는 bind, 변경된 bound state·stride mismatch 거부; 승인된 Upload binding 뒤 CPU write가 stale submit으로 거부됨 |
| 바인딩 거부 원자성 | 잘못된 view 거부 32회 후 임시 allocation 회수; 빠진/중복/잘못된 binding·element, null/foreign image·constant, 미초기화/attachment-feedback image, 잘못된 constant 범위 거부 |
| Snapshot·budget | snapshot 없는 draw와 pipeline 재선택 후 draw 거부; 5개 resource descriptor를 쓰는 set 51개(255개 descriptor) 뒤 다음 set을 거부하고 이전 set으로 draw 성공 |
| 늦은 거부·version | 나중 constant 오류가 앞선 유효 Upload version을 포착하지 않음; 승인된 constant snapshot 뒤 CPU write는 stale submit으로 거부됨 |
| Thread | 다른 thread의 vertex binding·resource binding 거부 |
| Native 소유 수명 | 입력 buffer/pipeline/image/상수 wrapper 해제와 command list 해제 후에도 submit/wait/readback 성공; 종료 뒤 SDK 소유 allocation/pending 계수 0 |
| 퇴역 | public device 종료로 pending generation drain/retire, child description 보존 및 native 연산 `DeviceLost` |

Descriptor 검사는 위 resource budget 경로를 실제로 실행한 근거이며, 별도 256개 snapshot 한도를 끝까지 채운 검사로 세지 않는다. 통제된 퇴역은 예상 밖 TDR·native device loss 검증이 아니다. SDK 자원 계수는 driver/OS 전체 메모리나 장시간 메모리 기준의 근거가 아니다.

정상 suite는 `validation=0`이다. 이때 validation error 계수 0을 정상 layer 실행 성공으로 세지 않는다. CLI 오타/추가 인수 8회는 작업 전 exit 1·Usage, 빈 layer 경로의 `--validation` 요청 4회는 exit 1·Khronos layer 부재 진단으로 거부됐다. 정상 core/sync validation과 오류 검출 controls는 `P3-LNX-11`에 남긴다.

## 환경·재현·증거

GitHub-hosted Ubuntu 24.04.5 LTS x86_64, kernel `6.17.0-1022-azure`, g++-14/Ninja/Release에서 실행했다. `libvulkan1 1.3.275.0-1build1`, `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4`, 고정 Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf`를 사용했다. D3D12 OFF, Vulkan/tests ON, validation tests OFF다.

같은 실행의 설치 SDK device probe가 `llvmpipe (LLVM 20.1.2, 256 bits)`의 native CPU 종류(4)와 UUID/FNV ID를 대조했다. ID `5892941257450743403`은 JSON 문자열로 보존했고 네 suite 실행의 이름/ID가 일치했다. Device UUID `6d65736132352e322e382d3075627500`, driver UUID `6c6c766d706970655555494400000000`, 실제 loader/ICD module 경로·크기/hash는 receipt에 보존했다. 외부 runtime binary는 artifact에 묶지 않았다.

[`tools/run_linux_input.py`](../tools/run_linux_input.py)는 실제 Linux·clean source/고정 headers·새 build/install/evidence root를 요구한다. 관측한 lavapipe manifest 하나를 child의 `VK_DRIVER_FILES`/`VK_ICD_FILENAMES`에 같은 절대 경로로 지정한다. 상속 VK/VVL 입력 제거, implicit layer 비활성화, 빈 explicit layer 경로, `LP_NUM_THREADS=2`, 0700 XDG runtime 경로도 child에만 적용한다. 시스템 driver/layer를 교체하지 않는다. 전용 [workflow](../.github/workflows/linux-vulkan-input.yml)가 runtime package를 준비하고 다음을 실행한다.

```sh
python3 tools/run_linux_input.py
```

기존 `VertexFixtures.hpp`/`BindingFixtures.hpp`와 두 support header를 그대로 사용했다. 기존 [입력](PHASE3_VULKAN_VERTEX_REPORT.md)·[바인딩](PHASE3_VULKAN_BINDINGS_REPORT.md)의 shader provenance와 이번 실행을 구분한다. 새 assembly/semantic validation·HLSL compile은 수행하지 않았고 SDK interface 검사를 일반 SPIR-V semantic validator로 표현하지 않는다. 실제 사용한 suite/support/fixture의 Git bytes/hash를 검증했다.

[Artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-input/artifact.zip)의 SHA256 `29a985fa5e4df8953831bfab3c5d9f831617c5173e2bb09f996685780bf0206b`은 GitHub artifact digest와 일치했다. 모든 내부 diagnostic 크기/hash, job log와 ZIP receipt의 일치, 검증 커밋의 입력 파일 bytes 및 SDK 27개 소스 파일의 fingerprint도 대조했다. 빌드/설치 SDK library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 일치했다.

Native Git/LF SDK fingerprint는 `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830`, 보존한 Windows CRLF SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`다. 제품 SDK와 메모리 정책은 그대로이며 이전 Windows 실행을 Linux 실행으로 재귀속하지 않는다.

같은 커밋의 [기존 Build and correctness CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40178114349)는 portable C++ CTest 통과 뒤 기존 Python 오류 92건/614개가 재현됐다. [해당 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-input/common-portable-job.log)를 보존하고 `P3-LNX-14`는 미완료로 유지한다. 전용 input/binding CI 성공을 전체 CI 성공으로 표현하지 않는다.

`P3-LNX-08`만 software ICD 범위에서 완료한다. 물리 GPU `P3-LNX-15`는 최종 gate의 필수 의존성으로 유지한다. D32/depth·timestamp·정상 validation·전체 설치 소비자/입력·라이선스 감사 `P3-LNX-12`, Linux WSI와 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 조사 보류도 유지한다. 다음 Linux 항목은 `P3-LNX-09` D32·depth/MRT다.

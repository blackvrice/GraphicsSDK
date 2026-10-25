# Phase 3 — Linux D32·depth/MRT 계약

2026-10-25 · `P3-LNX-09` · 검증 커밋 `f42c84a`.

실제 Ubuntu에서 기존 `VulkanDepthTests.cpp`를 변경 없이 빌드했다. **Linux software ICD에서 D32 전송·depth-only/read-only·비교 8종·쓰기 mask·혼합 MRT의 색/깊이 전체 참조와 pass/pipeline 호환성·수명 계약을 완료**한다. 관측 장치는 CPU 종류의 Mesa lavapipe이며 hardware adapter는 0개다. 물리 GPU의 같은 공통 suite 수용은 최종 gate의 필수 의존성 `P3-LNX-15`에 유지한다.

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40180300877)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40180300877/job/121129737069)이 성공했다. SDK C++·공개 API·root `CMakeLists.txt`·기존 suite/support·SPIR-V fixture header는 변경하지 않았다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-depth/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-depth/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-depth/job.log)에 결과를 보존했다.

## 실제 결과

| 실행 | Assertion | Pass / draw | 전체 비교 이미지 (D32 포함) | D32 이미지 | 검증 texel bytes | Padding 있는 이미지 |
|---|---:|---:|---:|---:|---:|---:|
| in-tree `vulkan_depth` CTest | 442 | 103 / 129 | 139 | 63 | 167,427,552 | 131 |
| 설치 SDK `vulkan_depth_consumer` | 442 | 103 / 129 | 139 | 63 | 167,427,552 | 131 |
| 합계 | **884** | **206 / 258** | **278** | **126** | **334,855,104** | **262** |

전체 이미지 278개는 색상 152개와 D32 126개다. D32 이미지 계수는 전체 이미지의 부분집합이다. pass/draw 계수에는 계약·거부 대조용 setup·통제된 퇴역 경로도 포함되므로 비교 이미지와 일대일 대응하지 않는다. CTest는 `^vulkan_depth$` 하나를 정확히 선택했고 JUnit에서 testcase 1개 통과와 skip/failure/error 부재를 확인했다. 설치 소비자는 별도 project의 `find_package(GraphicsSDK CONFIG REQUIRED)`로 같은 suite를 빌드했다.

모든 readback의 **모든 색상 texel·모든 RGBA 채널 및 모든 D32 texel**을 CPU 참조와 대조하고 finite를 검사했다. 색상 허용 오차는 UNorm8 `2.1/255`, Float16 `0.004`, D32는 절대 오차 **`< 0.000001`**이다. 위 bytes는 검증한 texel 데이터 분량이며 색상/depth의 bit-exact 일치나 전체 native 복사 트래픽을 뜻하지 않는다. `0xb3` sentinel로 매 행·마지막 행의 모든 padding 바이트 보존을 정확히 검사했다.

## 전송·비교·쓰기·MRT 범위

| 범위 | 실제 실행 내용 |
|---|---|
| D32 footprint·왕복 | `1×1`, `3×7`, `65×37`, `1920×1080`, `3840×2160`; texel당 4바이트·256-byte rowPitch 정렬·마지막 행 padding, Upload/Device 경로 |
| Depth 전송·Load | CPU 패턴 `(3x+7y)%17/16` 업로드→DepthRead+Load→readback; Device 경로는 중간 Device buffer로 image→buffer→image도 실행 |
| Depth-only | fragment shader 생략, 색상 첨부 없이 Clear 후 draw 또는 Clear→Load 뒤 draw; 위 모든 크기에서 D32 전체 값 `.25` 비교 |
| 비교·쓰기 mask | RGBA8/RGBA16F `65×37`에서 Never/Less/Equal/LessEqual/Greater/NotEqual/GreaterEqual/Always × 쓰기 OFF/ON; `.5` clear와 `.25/.5/.75`를 비교해 색상·깊이 모두 검사 |
| Depth-test disabled | 두 색상 포맷에서 Never·쓰기 OFF·test OFF를 실행해 색상 출력과 깊이 보존 확인 |
| Depth+MRT | 2·8개 혼합 RGBA8/RGBA16F 첨부, near/far draw 순서 반전, 초기 색상·D32 참조 |
| Read-only Load | 이전 내용을 Load한 DepthRead pass에서 writing pipeline의 draw를 거부; 읽기 전용 pipeline으로 일부 색상 갱신, 전체 깊이 보존 |

비교/쓰기 mask는 색상 포맷당 16개 조합이며 test disabled를 1개 더해 총 34개 비교 case다. D32 sampling·stencil·다른 depth 포맷/MSAA/mip/array 수용을 포함하지 않는다. D32의 ShaderRead 전이는 기존 `Unsupported` 거부 계약을 확인했다.

## 호환성·거부·수명

| 계약 | 실행한 검사 |
|---|---|
| Pipeline/pass 호환성 | 색상 출력에 fragment 누락, test 없는 write, attachment 없는 depth state, depth-only/color-only pipeline과 pass 불일치 거부 |
| 상태·초기화 | color의 depth state·D32의 RenderTarget 거부, 미초기화 depth Load, writable/read-only 상태 불일치, pass 안 depth 전이 거부 |
| Clear·첨부 | 범위 밖/NaN depth clear, read-only Clear, foreign·크기 불일치 depth 거부 |
| 거부 원자성 | 뒤쪽 depth 검증 실패가 앞쪽 color Clear/초기화·resource/version capture를 남기지 않음; foreign depth 거부 32회 뒤 앞쪽 임시 color allocation 회수 |
| 보존·version·thread | 거부된 color+depth pass 뒤 기존 blue 색상 보존, 경쟁 command의 depth 사용 뒤 stale submit 거부, 다른 thread의 depth pipeline 설정 거부 |
| Native 소유 수명 | image/상수/pipeline wrapper와 command list 해제 후 submit/wait/readback 성공; 종료 뒤 SDK 소유 allocation/pending 계수 0 |
| 통제된 퇴역 | public device 종료가 pending depth 작업을 drain/retire; D32 description/footprint 보존, child native 연산 `DeviceLost` |

통제된 퇴역은 예상 밖 TDR·native device loss 검증이 아니다. SDK 자원 계수는 driver/OS 전체 메모리나 장시간 메모리 회수 기준의 근거가 아니다. 성능·WSI·실제 표시 검증도 수행하지 않았다.

정상 suite는 `validation=0`이다. 이 상태의 validation error 계수 0을 정상 layer 실행 성공으로 세지 않는다. CLI 오타/추가 인수 4회는 작업 전 exit 1·Usage, 빈 layer 경로의 `--validation` 요청 2회는 exit 1·Khronos layer 부재 진단으로 거부됐다. 정상 core/sync validation과 오류 검출 controls는 `P3-LNX-11`에 남긴다.

## 환경·재현·증거

GitHub-hosted Ubuntu 24.04.5 LTS x86_64, kernel `6.17.0-1022-azure`, g++-14/Ninja/Release에서 실행했다. `libvulkan1 1.3.275.0-1build1`, `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4`, 고정 Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf`를 사용했다. D3D12 OFF, Vulkan/tests ON, validation tests OFF다.

같은 실행의 설치 SDK device probe가 `llvmpipe (LLVM 20.1.2, 256 bits)`의 native CPU 종류(4)와 UUID/FNV ID를 대조했다. ID `5892941257450743403`은 JSON 문자열로 보존했고 두 suite 실행의 이름/ID가 일치했다. Device UUID `6d65736132352e322e382d3075627500`, driver UUID `6c6c766d706970655555494400000000`, 실제 loader/ICD module 경로·크기/hash는 receipt에 보존했다. 외부 runtime binary를 artifact에 묶지 않았다.

[`tools/run_linux_depth.py`](../tools/run_linux_depth.py)는 실제 Linux·clean source/고정 headers·새 build/install/evidence root를 요구한다. 관측한 lavapipe manifest 하나를 child의 `VK_DRIVER_FILES`/`VK_ICD_FILENAMES`에 같은 절대 경로로 지정한다. 상속 VK/VVL 입력 제거, implicit layer 비활성화, 빈 explicit layer 경로, `LP_NUM_THREADS=2`, 0700 XDG runtime 경로도 child에만 적용한다. 시스템 driver/layer를 교체하지 않는다. 전용 [workflow](../.github/workflows/linux-vulkan-depth.yml)가 runtime package를 준비하고 다음을 실행한다.

```sh
python3 tools/run_linux_depth.py
```

기존 `DepthFixtures.hpp`와 support header를 그대로 사용했다. [기존 shader provenance](PHASE3_VULKAN_DEPTH_REPORT.md)와 이번 실행을 구분한다. 새 assembly/semantic validation·HLSL compile은 수행하지 않았고 SDK interface 검사를 일반 SPIR-V semantic validator로 표현하지 않는다. 실제 사용한 suite/support/fixture의 Git bytes/hash를 검증했다.

[Artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-depth/artifact.zip)의 SHA256 `7e4c322c5a3df8f2002d31f11b9c9b7d19283630f5f1e7aaafa7f54d8b34e3c4`은 GitHub artifact digest와 일치했다. 모든 내부 diagnostic 크기/hash, job log와 ZIP receipt의 일치, 검증 커밋의 입력 파일 bytes 및 SDK 27개 소스 파일의 fingerprint도 대조했다. 빌드/설치 SDK library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 일치했다.

Native Git/LF SDK fingerprint는 `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830`, 보존한 Windows CRLF SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`다. 제품 SDK와 메모리 정책은 그대로이며 이전 Windows 실행을 Linux 실행으로 재귀속하지 않는다.

같은 커밋의 [기존 Build and correctness CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40180300886)는 portable C++ CTest 통과 뒤 기존 Python 오류 92건/614개가 재현됐다. [해당 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-depth/common-portable-job.log)를 보존하고 `P3-LNX-14`는 미완료로 유지한다. 전용 depth CI 성공을 전체 CI 성공으로 표현하지 않는다.

`P3-LNX-09`만 software ICD 범위에서 완료한다. 물리 GPU `P3-LNX-15`는 최종 gate의 필수 의존성으로 유지한다. Timestamp·정상 validation·전체 설치 소비자/입력·라이선스 감사 `P3-LNX-12`, Linux WSI와 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 조사 보류도 유지한다. 다음 Linux 항목은 `P3-LNX-10` Timestamp다.

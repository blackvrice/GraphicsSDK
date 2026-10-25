# Phase 3 — Linux 컬러 pass·기본 draw 계약

2026-10-25 · `P3-LNX-07` · 검증 커밋 `026a91a`.

실제 Ubuntu에서 기존 `VulkanRenderPassTests.cpp`와 `VulkanDrawTests.cpp`를 변경 없이 빌드했다. **Linux software ICD에서 컬러 Clear/Load/Store·SPIR-V 삼각형·blend/scissor·instancing·MRT의 전체 이미지 참조와 상태/수명 계약을 완료**한다. 관측 장치는 CPU 종류의 Mesa lavapipe, hardware adapter는 0개다. 물리 GPU의 같은 공통 suite 수용은 필수 `P3-LNX-15`에 유지한다.

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40176979891)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40176979891/job/121119661414)이 성공했다. SDK C++·공개 API·root `CMakeLists.txt`·기존 C++ suite·SPIR-V fixture header는 변경하지 않았다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-draw/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-draw/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-draw/job.log)에 근거를 보존했다.

## 실제 결과

| 실행 | Assertion | Pass / draw | 전체 비교 이미지 | 검증 texel bytes | Padding 있는 이미지 |
|---|---:|---:|---:|---:|---:|
| 컬러 pass in-tree CTest | 297 | 64 pass | 69 | 373,531,756 | 57 |
| 컬러 pass 설치 SDK 소비자 | 297 | 64 pass | 69 | 373,531,756 | 57 |
| draw in-tree CTest | 319 | 31 draw | 33 | 124,745,708 | 29 |
| draw 설치 SDK 소비자 | 319 | 31 draw | 33 | 124,745,708 | 29 |
| 합계 | **1,232** | **128 pass / 62 draw** | **204** | **996,554,928** | **172** |

`vulkan_color_pass`와 `vulkan_draw`를 각각 정확히 선택했고 각 JUnit에서 testcase 1개 통과, skip/failure/error 부재를 확인했다. 설치 소비자는 별도 project의 `find_package(GraphicsSDK CONFIG REQUIRED)`로 `vulkan_render_consumer`와 `vulkan_draw_consumer`만 빌드해 같은 suite를 실행했다. pass/draw 계수에는 계약·미제출 recording 폐기·통제된 퇴역 경로도 포함된다. 모든 계수가 readback 이미지 하나와 일대일 대응하는 것은 아니다.

각 readback의 **모든 texel·모든 RGBA 채널**을 CPU 참조와 대조했다. UNorm8 허용 오차는 `1.01/255`, Float16은 `0.001`이며 finite도 검사한다. 따라서 위 texel bytes는 비교한 데이터 분량이며 모든 색상 바이트의 bit-exact 일치나 전체 native 전송 트래픽을 뜻하지 않는다. 업로드한 패턴의 Load/Store 보존 검사는 texel bytes를 정확히 비교했다. 컬러 pass의 `0xa7`, draw의 `0xb9` sentinel로 매 행·마지막 행의 전체 padding 보존도 정확히 검사했다.

## 컬러 pass와 draw 범위

| 범위 | 실제 실행 내용 |
|---|---|
| Clear/Load/Store | RGBA8UNorm/RGBA16Float의 `1×1`, `3×7`, `65×17`, `1920×1080`, `3840×2160`; Clear→인접 Load, 제출 뒤 Load, 재Clear |
| Float16·업로드 | 음수 `-2`·1 초과 `4` clear 성분; 두 포맷 `65×17` 업로드 뒤 Load/Store의 전체 texel/padding 보존 |
| 컬러 MRT | 1~8개 혼합 RGBA8/RGBA16F 첨부, 각기 다른 clear 값, 연속 Load/Store와 모든 첨부 readback |
| 기본 SPIR-V draw | 두 포맷의 fullscreen(1×1~4K), `65×37`·`129×73` 삼각형 geometry와 배경색 CPU 참조 |
| Scissor·blend | 분리된 빨강/초록 영역, Load pass의 반투명 빨강 합성; RGB와 alpha 참조, 잘못된 scissor 뒤 유효 사각형 유지 |
| Instancing | `65×37`, (instanceCount, firstInstance) = (1,1)·(2,0)·(1,0), firstVertex 6; 이동한 삼각형 coverage 참조 |
| Draw MRT | `33×17`의 2개·8개 혼합 포맷 출력, 출력별 CPU 색상 참조 |

삼각형 참조는 pixel center와 CPU geometry 식을 사용하며 해당 홀수 크기는 sample이 삼각형 edge에 정확히 걸리지 않도록 정한 기존 fixture다. 이 실행은 vertex/index buffer나 texture/constant binding suite의 수용을 대신하지 않는다.

SPIR-V는 기존 자체 fixture header의 embedded word array를 그대로 사용했다. [기존 조립·검증 provenance](PHASE3_VULKAN_DRAW_REPORT.md)의 범위와 이번 실제 실행을 구분한다. 이번에는 새 assembly/semantic validation·HLSL compile을 수행하지 않았다. SDK의 framing/interface 검사도 일반 SPIR-V semantic validator로 표현하지 않는다. 실제 빌드한 fixture header의 Git bytes/hash를 receipt와 대조했다.

## 거부·초기화·수명

| 계약 | 검사 |
|---|---|
| Pass 초기화·거부 | 전이만 한 image의 Load 거부, 나중 첨부의 실패가 앞선 Clear를 실행/초기화하지 않음; 빈/null/중복/크기 불일치·잘못된 usage/load/clear·9개 첨부 거부 |
| Pass 원자성·상태 | 잘못된/foreign 첨부 거부 32회 뒤 allocation/reference 통계 보존; nested pass, 열린 pass의 end/submit·barrier/copy 거부, 유효 recording 유지 |
| Pipeline/draw | 불량 byte framing·stage/output 수·format/layout 불일치·미지원 builtin/DXBC 거부; null/foreign/호환되지 않는 pipeline 뒤 binding 보존 |
| Draw 경계 | zero vertex/instance count, vertex/instance 범위 overflow, 빈/범위 초과 scissor를 32회 반복 거부, 유효 draw 참조 일치 |
| Version·thread | pass submit 후 sibling recording 거부; 다른 thread의 pass 종료·pipeline 생성 거부 |
| Native 소유 수명 | attachment/pipeline wrapper를 해제해도 recording/submission이 native 자원 보유; command list 해제 후 wait/readback 성공; 미제출 framebuffer/pipeline 폐기와 SDK 소유 계수 0 |
| 통제된 퇴역 | pending/submitted 및 미제출 recording이 public device 종료로 drain/retire; CPU description 보존, child native 사용은 `DeviceLost` |

정상 suite는 `validation=0`이다. 이 상태의 validation error 계수 0을 정상 layer 실행 성공으로 세지 않는다. CLI 오타/추가 인수 8회는 작업 전 exit 1·Usage, 빈 layer 경로의 `--validation` 요청 4회는 exit 1·Khronos layer 부재 진단으로 거부됐다. 정상 core/sync validation과 오류 검출 controls는 `P3-LNX-11`에 남긴다.

통제된 퇴역은 예상 밖 TDR·native device loss 검증이 아니다. SDK 소유 계수는 driver/OS 전체 메모리나 장시간 메모리 회수 기준의 근거가 아니다. 성능·WSI·표시 검증도 수행하지 않았다.

## 환경·재현·증거

GitHub-hosted Ubuntu 24.04.5 LTS x86_64, kernel `6.17.0-1022-azure`, g++-14/Ninja/Release에서 실행했다. `libvulkan1 1.3.275.0-1build1`, `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4`, 고정 Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf`를 사용했다. D3D12 OFF, Vulkan/tests ON, validation tests OFF다.

같은 실행의 설치 SDK device probe가 `llvmpipe (LLVM 20.1.2, 256 bits)`의 native CPU 종류(4)와 UUID/FNV ID를 대조했다. ID `5892941257450743403`은 JSON 문자열로 보존했고 네 suite 실행의 이름/ID가 일치했다. Device UUID `6d65736132352e322e382d3075627500`, driver UUID `6c6c766d706970655555494400000000` 및 실제 loader/ICD module 경로·크기/hash는 receipt에 보존했다. 외부 runtime binary를 배포하지 않는다.

[`tools/run_linux_draw.py`](../tools/run_linux_draw.py)는 실제 Linux·clean source/고정 headers·새 build/install/evidence root를 요구한다. 관측한 lavapipe manifest 하나를 child의 `VK_DRIVER_FILES`/`VK_ICD_FILENAMES`에 같은 절대 경로로 지정한다. 상속 VK/VVL 입력 제거, implicit layer 비활성화, 빈 explicit layer 경로, `LP_NUM_THREADS=2`, 0700 XDG runtime 경로도 child에만 적용한다. 시스템 driver/layer를 교체하지 않는다. 전용 [workflow](../.github/workflows/linux-vulkan-draw.yml)가 runtime package를 준비하고 다음을 실행한다.

```sh
python3 tools/run_linux_draw.py
```

[Artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-draw/artifact.zip)의 SHA256 `ec95d8b83f41ad255ef331c3de19bdb6a0d74bc7c8299b667d7429c175169c34`은 GitHub artifact digest와 일치했다. 모든 내부 diagnostic 크기/hash, job log와 ZIP receipt의 일치, 검증 커밋의 suite/fixture/probe/runner/preset/workflow bytes 및 SDK 27개 소스 파일의 fingerprint를 대조했다. 빌드/설치 SDK library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 일치했다.

Native Git/LF SDK fingerprint는 `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830`, 보존한 Windows CRLF SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`다. 제품 SDK와 메모리 정책은 그대로이며 이전 Windows 실행을 Linux 실행으로 재귀속하지 않는다.

같은 커밋의 [기존 Build and correctness CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40176979887)는 portable C++ CTest 통과 뒤 기존 Python 오류 92건/614개가 재현됐다. [해당 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-draw/common-portable-job.log)를 보존하고 `P3-LNX-14`는 미완료로 유지한다. 전용 graphics CI 성공을 전체 CI 성공으로 표현하지 않는다.

`P3-LNX-07`만 software ICD 범위에서 완료한다. 물리 GPU `P3-LNX-15`는 최종 gate의 필수 의존성으로 유지한다. 입력/binding·depth·timestamp·정상 validation·전체 설치 소비자/입력·라이선스 감사 `P3-LNX-12`, Linux WSI와 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 조사 보류도 유지한다. 다음 Linux 항목은 `P3-LNX-08` 입력·바인딩이다.

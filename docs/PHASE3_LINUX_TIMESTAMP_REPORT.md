# Phase 3 — Linux timestamp query·availability·수명

2026-10-25 · `P3-LNX-10` · 검증 커밋 `b1a7298`.

실제 Ubuntu에서 timestamp suite를 in-tree CTest와 설치 SDK 소비자로 실행했다. **Linux software ICD에서 실제 Vulkan query 기반 elapsed 결과·availability·query 수명과 native queue metadata를 확인하고, 별도 CPU 검사에서 변환·wrap 계산을 검증했다.** 관측 장치는 CPU 종류의 Mesa lavapipe다. 물리 GPU timestamp 수용은 최종 gate의 필수 의존성 `P3-LNX-15`에 유지한다.

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40181013932)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40181013932/job/121131888284)이 성공했다. SDK C++·공개 API·root `CMakeLists.txt`·shader fixture는 변경하지 않았다. 기존 timestamp suite에는 elapsed 값과 즉시 조회 결과를 출력하는 진단만 추가했으며 기존 assertion·workload는 그대로다. 별도 Linux native metadata probe와 runner/workflow를 추가했다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-timestamp/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/job.log)에 근거를 보존했다.

## 실제 query 결과와 native metadata

| 실행 | Timestamp assertion | 측정 pair | 즉시 조회 ready / not-ready | 버린 query recording | 비교 이미지 (D32) |
|---|---:|---:|---:|---:|---:|
| in-tree `vulkan_timestamps` | 156 | 26 | 0 / 23 | 3 | 9 (3) |
| 설치 SDK `vulkan_timestamp_consumer` | 156 | 26 | 0 / 23 | 3 | 9 (3) |
| 합계 | **312** | **52** | **0 / 46** | **6** | **18 (6)** |

모든 pair의 SDK `elapsedGpuMilliseconds()` 값, pair index, 양수 요구 여부를 [원본 출력](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/native/ctest-timestamp.stdout)에 남겼다. 설치 소비자의 값도 [별도 출력](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/native/installed-timestamp.stdout)에 보존했다. 각 결과는 finite·0 이상이고 반복 read가 같았다. 복사 5개·큰 graphics 2개의 양수 요구 pair는 각 실행에서 모두 양수였다. 빈 구간은 0도 허용하는 기존 계약을 유지한다.

관측 범위는 in-tree `0.010950~321.659444ms`, 설치 소비자 `0.007674~93.992893ms`다. 여러 빈/복사/graphics workload를 섞은 진단 값이며 성능 비교·물리 GPU 정확도·1ns 측정 정밀도의 근거로 사용하지 않는다. SDK는 native tick 차이와 period로 elapsed를 계산한다. Host steady-clock 구간은 counter-cycle 모호성/상한 검사에 사용하며 elapsed 계산을 대신하지 않는다. 추가한 출력은 host 경과 시간에 영향을 주므로 이 실행을 성능 표본으로 사용하지 않는다.

| Native metadata probe의 관측 | 값 |
|---|---|
| Adapter / 종류 | `llvmpipe (LLVM 20.1.2, 256 bits)` / `VK_PHYSICAL_DEVICE_TYPE_CPU` (4) |
| SDK/native ID | `5892941257450743403` (JSON 문자열) |
| Queue family / flags / count | `0` / `15` / `1` |
| Timestamp valid bits | **64** |
| Timestamp period | **1ns** (tick 변환 단위) |
| SDK `timestampQueries` | true |
| Metadata probe assertion | 18 |

[`Metadata.cpp`](../tests/linux_vulkan_timestamp/Metadata.cpp)는 native Vulkan instance의 properties/queue family를 조회하고 첫 nonempty graphics queue를 선택했다. UUID/FNV ID·이름·CPU 종류를 SDK와 대조하고 설치 SDK device의 timestamp capability도 확인했다. 이 probe는 metadata만 조회하며 query를 실행하거나 SDK의 원시 start/end tick을 노출하지 않는다. 실제 query 실행은 timestamp suite에서 수행한다. 기존 device probe의 식별 결과와도 일치한다.

즉시 조회는 이번에 모두 `InvalidState`(not-ready)였고 wait 뒤 결과를 얻었다. 기존 suite는 즉시 완료 관측 성공도 허용하므로 모든 환경에서 not-ready를 강제하지 않는다. 원시 Vulkan availability flag를 별도 노출한 것은 아니며 public API의 준비 상태 계약을 관측했다. 미완료/미resolve 결과와 query availability를 검사하는 SDK 경로는 그대로다.

## Wrap·변환 수학 검사

`vulkan_timestamp_math` CTest는 **3,775 assertion / GPU calls 0**으로 통과했다. 36/40/48/64-bit와 `.5/1/10ns` period, mask·경계 wrap·같은 counter, deterministic interval, 잘못된 metadata/host bound·역순 기록·한 cycle 이상의 모호성·overflow를 검사했다.

이 wrap 입력은 CPU 변환 함수의 합성 데이터다. 실제 native counter wrap이나 전체 cycle 지연은 재현하지 않았다. CPU 수학 성공을 실제 GPU query 실행이나 물리 GPU wrap 사건으로 합산하지 않는다. Metadata probe·CPU math·timestamp 실행의 assertion 계수도 각각 구분했다.

## Workload·오류·수명 계약

| 범위 | 실행한 검사 |
|---|---|
| Query 슬롯·resolve | begin 전 read/write/resolve, 잘못된/중복 슬롯, 부분 pair, pass 안 resolve, 중복 resolve·resolve 뒤 write, ended 미제출 read 거부 |
| 재사용·순서 | begin의 이전 결과 폐기, 부분/역순 제출 거부, 미제출 pair를 버린 empty generation, 새 generation 16회와 반복 결과 안정성 |
| Timed copy | `4099`, `1048579`, `16777216`바이트 Upload→Device→Readback을 보유/recording 해제 두 경로로 실행; 별도 `65539`·`2097155`바이트를 연속 제출하고 뒤 fence 먼저 대기 |
| Timed graphics | `65×37`, `1920×1080`, `3840×2160`의 RGBA8·RGBA16F 혼합 MRT+D32 draw, pass 안 end timestamp와 pass 밖 resolve |
| Thread·보유 | 다른 thread의 timestamp write/read 거부, query pool을 buffer allocation bytes로 합성하지 않음, pending 보유 통계 계약·recording을 fence 전에 버린 뒤 readback 성공 |
| 퇴역 | pending result와 cached result 양쪽에서 public device 종료 후 `DeviceLost`, query recording 재사용 거부 |

두 실행 합계 **75,644,964 buffer 바이트**를 changing payload와 정확히 대조했다. 색/D32의 **331,852,960 texel 바이트**는 전체 참조 비교 분량이다. 색상은 UNorm8 `2.1/255`·Float16 `0.004`, D32는 절대 오차 `<0.000001`로 finite와 함께 검사했다. 모든 row/final-row padding의 `0xb3` 보존도 확인했으며 padding 있는 이미지는 합계 6개다. Texel bytes는 bit-exact 색상 일치나 전체 복사 트래픽을 뜻하지 않는다.

SDK 소유 자원 회수 계수 0과 query/result 수명을 확인했지만 driver/OS 전체 메모리·장시간 메모리 기준의 근거는 아니다. Pending 통계 검사는 이미 완료된 경우 pending 0도 허용한다. 통제된 device 종료는 예상 밖 native loss/TDR 검증이 아니다.

정상 suite는 `validation=0`이다. Validation error 계수 0을 정상 layer 실행 성공으로 세지 않는다. CLI 오타/추가 인수 4회는 작업 전 exit 1·Usage, 빈 layer 경로의 `--validation` 요청 2회는 exit 1·Khronos layer 부재로 거부됐다. 정상 core/sync validation·오류 검출 controls는 `P3-LNX-11`에 남긴다.

## 재현·증거와 잔여 범위

GitHub-hosted Ubuntu 24.04.5 LTS x86_64, kernel `6.17.0-1022-azure`, g++-14/Ninja/Release에서 실행했다. `libvulkan1 1.3.275.0-1build1`, `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4`, 고정 Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf`를 사용했다. D3D12 OFF, Vulkan/tests ON, validation tests OFF다.

[`tools/run_linux_timestamp.py`](../tools/run_linux_timestamp.py)는 실제 Linux·clean source/고정 headers·새 build/install/evidence root를 요구한다. 관측한 lavapipe manifest 하나를 child의 `VK_DRIVER_FILES`/`VK_ICD_FILENAMES`에 같은 절대 경로로 지정한다. 상속 VK/VVL 입력 제거, implicit layer 비활성화, 빈 explicit layer 경로, `LP_NUM_THREADS=2`, 0700 XDG runtime 경로도 child에만 적용한다. 시스템 driver/layer를 교체하지 않는다. 전용 [workflow](../.github/workflows/linux-vulkan-timestamp.yml)가 runtime package를 준비하고 다음을 실행한다.

```sh
python3 tools/run_linux_timestamp.py
```

[Artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/artifact.zip)의 SHA256 `1a2d25742552ac67b9832b6bf5db4333e3357c11e8ed2f8f403dfa296c648fc9`은 GitHub artifact digest와 일치했다. 모든 내부 diagnostic 크기/hash, job log와 ZIP receipt의 일치, 검증 커밋의 suite/math/support/fixture/native probe/runner/preset/workflow bytes를 대조했다. 기존 timestamp suite 변경은 진단 출력뿐임을 source 대조로 확인했다. 기존 [shader provenance](PHASE3_VULKAN_TIMESTAMP_REPORT.md)는 보존했으며 새 semantic validation·HLSL compile은 수행하지 않았다.

SDK 27개 소스의 native Git/LF fingerprint는 `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830`, 보존한 Windows CRLF SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`로 그대로다. 빌드/설치 SDK library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 일치했다. 메모리 정책과 기존 Windows 실행의 귀속도 변경하지 않았다.

같은 커밋의 [Linux compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40181013921)도 두 preset의 compile·CPU 11개씩을 통과했다. 해당 [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/compile-job.log)의 source/fingerprint와 CPU 22개·GPU/layer 실행 0을 확인했다. 이 별도 성공을 정상 validation 실행으로 합산하지 않는다.

[기존 Build and correctness CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40181013899)는 portable C++ CTest 통과 뒤 기존 Python 오류 92건/614개가 재현됐다. [로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-timestamp/common-portable-job.log)를 보존하고 `P3-LNX-14`는 미완료로 유지한다. 전체 CI가 모두 성공했다고 표현하지 않는다.

`P3-LNX-10`만 software ICD 범위에서 완료한다. 물리 GPU `P3-LNX-15`는 최종 gate의 필수 의존성으로 유지한다. 정상 validation·전체 설치 소비자/입력·라이선스 감사 `P3-LNX-12`, Linux WSI와 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 조사 보류도 유지한다. 다음 Linux 항목은 `P3-LNX-11` core/sync validation이다.

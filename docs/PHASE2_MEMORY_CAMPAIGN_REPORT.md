# Phase 2 어댑터 고정 메모리 campaign과 실패 기록

2026-10-22. 첫 미완료 항목인 메모리 스트레스의 실행·관측·실패 처리를 보강했다.
**도구 검증은 통과했지만 원래 장시간 메모리 gate는 미완료다.** SDK 공개 API/구현과
메모리 policy는 바꾸지 않았다. SDK fingerprint는
`85190ccf45a1c6c57473c25c8bef5c69e5387a5631a742d5561aef8b0e659970`이다.

## 구현과 계약

`MemoryStress.cpp`는 `--adapter <uint64 decimal>`을 공개 DeviceDesc에 전달한다.
DXGI observer도 [EnumAdapterByLuid](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgifactory4-enumadapterbyluid)로
실제 DeviceCaps ID를 조회한다. 이름 중복으로 거부하거나 다른 GPU를 추측하지 않는다.
GetDesc1의 LUID·이름·software 구분을 다시 확인한다. 공유 helper를 쓰는 native-only 대조도
관측용 caps에 실제 LUID를 채우도록 수정했다. 네이티브 graphics 작업은 추가하지 않았다.

새 schema 2는 요청 ID(null 또는 명시적 0 포함), SDK ID, observer ID, 최종 ID, LUID signed
high/unsigned low, software/GBV/state-tracking 여부를 기록한다. 분석기는 ID 불일치·검증 축소를
거부한다. 세 모드의 전체 판정은 gpu/validation의 동일 ID와 별도 WARP ID를 요구한다.
과거 schema 1은 유지하되 새 명시 선택의 증거로 사용하지 않는다.

모든 수치 옵션을 unsigned decimal로 끝까지 파싱하고 overflow·부호·trailing text·중복을
거부한다. `--max-private-growth-mib`는 1..4096, 기본 512다. 각 8-frame 전체 참조 비교와
drain 뒤, warmup을 포함해 process private commit 증가를 확인한다. 기준점은 device/statistics
계약 뒤, CPU/GPU 양성 대조·shader setup·warmup 전이다. 따라서 초기 검증/driver/cache 비용도
포함하며 SDK만의 누수량이 아니다. 한 회차가 한도를 초과할 수 있고, hard allocation ceiling이나
연속 peak 측정이 아니다. [DXGI usage](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)는
동일 ID의 local/nonlocal 두 segment를 계속 기록하며 실패한 query를 0으로 합성하지 않는다.

증가 한도를 넘으면 `guard_aborted`, 최종 zero-ownership checkpoint, `aborted`/
`private_growth_limit` footer를 기록하고 native exit 2로 종료한다. runner는 exit 1과
`accepted=false`를 남긴다. `--diagnostic-seconds`는 짧은 기능 검사이며 성공해도 메모리 판정은
false다. 원래 hardware 900초 / full GBV 300초 / WARP 300초, warmup 30초와 plateau 기준은
그대로다. 한 모드만 실행하면 전체 세 모드 campaign을 수용하지 않는다.

runner는 시작 전 `accepted=false` 파일을 만들고 매 process의 PID/명령/exit/log 및 고정
입력·source 복사본을 보존한다. native rejection·guard·plateau 실패 뒤에도 다음 모드를
실행한다. timeout은 자식을 kill/wait하고 중단하며, 입력 변경은 전체 판정을 무효화한다.
예기치 않은 native exit는 JSON footer가 없어도 십진/16진 종료 코드를 명시한다.

## 실제 실행 결과와 발견한 실패

GTX 1660 SUPER / Intel UHD 770 / Microsoft WARP에서 SDK와 observer ID가 각각
96576 / 106703 / 106596으로 일치했다. ID는 이 OS 세션의 관측이며 영구 번호가 아니다.
최종 실행의 명시 선택 3초 진단은 각각 392 / 624 / 912 frames, 전체 이미지
1,176 / 1,872 / 2,736개를 비교했다. 모두 짧은 기능 검사로 `accepted=false`다.

NVIDIA의 원래 300초 GBV 요청은 기본 512MiB 증가 한도에서 warmup 중 608 frames /
1,824개 전체 이미지를 검증한 뒤 중단했다. Intel 64MiB 제어는 첫 8-frame 회차 뒤 중단했다.
두 경우 모두 최종 SDK 보유 다섯 값 0, active generation의 ERROR/CORRUPTION 0과
명시적 중단 receipt를 확인했다. 낮은 한도의 발동은 장시간 churn 증가의 별도 증명이 아니다.

처음 탐색의 Intel 기본 512MiB 실행은 **112.875초 wall time에 0xc0000005**로 끝났다.
마지막 drained 기록은 elapsed 105.433초 / 4,792 verified frames이며 완전한 footer가 없다.
runner는 성공으로 분류하지 않고 원본·exit·입력 해시와 `accepted=false`를 보존했다.
Windows Application event 1000도 동일 executable/PID의 액세스 위반을 기록하지만 faulting
module은 `unknown`, offset 0이다. SDK·runtime·driver 중 원인을 특정할 증거가 부족하다.
이 실패는 미해결이며, 낮은 한도의 후속 검사로 덮거나 장시간 통과로 재분류하지 않는다.

실제 계속 실행 대조는 기본 512MiB / 진단 10초에서 native **gpu 0 → validation 2 → warp 0**,
runner exit 1을 확인했다. GPU 1,136 frames와 WARP 2,208 frames는 전체 참조를 비교했다.
64/128MiB 대조는 초기 양성 대조 비용까지 포함해 세 모드 모두 중단시켰으며 별도 원본으로
보존한다. 초기 검증 스크립트의 'Intel도 guard abort' 및 '낮은 한도에서 WARP 정상' 가정은
실제 출력과 맞지 않아 중단됐고, 위 최종 실제 결과를 기준으로 검사했다.

## 회귀·배포·재현

Core CTest **25/25**, Python **139/139**가 통과했다. 새 15개 회귀는 zero/high-bit ID,
관측·요청·최종 ID 불일치, 하드웨어 혼합, 검증 축소, guard/exit/threshold 불일치, source 변경,
timeout reap, native crash 뒤 남은 모드 실행을 검사한다. 합성 자료는 실제 GPU 증거와 구별한다.
11개 native 인자/기존 파일 거부와 실제 missing-ID runner 실패 파일도 확인했다.
native CPU observer 1초 및 public SDK hardware/WARP 각 2-cycle lifetime smoke가 통과했다.
공유 helper 소비자들을 새로 빌드했으며 이번 빌드에서는 compiler retry가 필요하지 않았다.

새 [stress inventory](../licenses/generated/windows-sdk-memory-campaign-release/inventory.json)는
compiler-read 25 inputs/1 own component와 다섯 도구의 실제 linker closure를 감사한다.
SDK 19/1, 공통 977/22, 어댑터 37/1 및 기존 SDK ZIP의 `--check`가 그대로 일치한다.
SDK source/library/설치본과 이전 7개 SDK ZIP을 보존했고 새 SDK ZIP은 만들지 않는다.
common 72/72·presentation 7/7·기존 소비자와 장시간 결과는 기존 해시 증거를 확인했으며
이번 새 실행으로 표현하지 않는다. 새 외부 dependency나 Sandbox/competitor 수정은 없다.

[사용 명령](../tools/memory_validation_probe/README.md#exact-adapter-resource-churn-campaign),
[최종 원본](../benchmarks/reports/validation/2026-10-22-phase2-memory-campaign-final/),
[실패 탐색 원본](../benchmarks/reports/validation/2026-10-22-phase2-memory-campaign/),
[요약·해시·source archive](../benchmarks/reports/2026-10-22-phase2-memory-campaign/)를 함께 확인한다.
원래 GBV 장시간 gate, Intel crash 원인, 육안 표시/실제 DPI/예상치 못한 TDR·hot-unplug와
Vulkan/Linux/Metal/macOS는 남아 있다. 새 성능 우위나 Phase 2 완료를 주장하지 않는다.

# Linux 세 SDK 업로드 반복 측정 도구

2026-11-02. P3-LBEN-01의 실행기·타이머 이식이며 실제 Linux GPU 성능 수용은
미완료다. draw/PSO/allocation의 timed campaign은 별도 남은 개발이다.

## 보존한 계약

`benchmarks/llgl_vulkan_upload_compare/main.cpp`의 타이머 루프와
`Upload.hpp`의 public upload/copy/fence/readback 본문을 그대로 Linux로
이식했다. SDK/SDL은 이미 qualification한 동일 Linux 업로드 바이너리를
사용한다. 별도 native 프로세스에서 staging map·CPU copy·record·submit·
업로드 fence 완료까지 측정한다. payload 생성·전체 readback·자원 준비와
정리·warmup·validation은 통계에서 제외한다. 이 CPU 완료 시간은 GPU
timestamp가 아니다. SDK padded staging과 SDL/LLGL tight layout 차이는 유지한다.
SDL 기본 instance API 요청의 차이와 native handle 귀속 한계도 유지한다.

`linux-upload-campaign.json`은 고정 Windows 비교의 반복·검증 수치를 재사용하는
Linux 계약이다. 과거 측정값·환경·결과는 가져오지 않는다. 실제 Linux GPU와
같은 source/qualification의 SDK/SDL readiness·전체 LLGL upload correctness가
필요하다. software 또는 guard-only receipt는 물리 측정에 사용할 수 없다.
원본 source/probe·UUID·driver·파일 hash가 다르면 실패한다.

물리 모드에서는 검증·오류 대조를 먼저 수행한 뒤 네 입력별 각 SDK를 5개
독립 프로세스로 실행한다. 각 프로세스는 warmup 5회·측정 30회이며, 네
입력 × 세 SDK × 5회 = 60개 측정 프로세스·원본 표본 1,800개다. 고정 seed로
시나리오 block과 인접 세 SDK 순서를 정하고, 각 입력에 다섯 서로 다른 SDK
순열을 사용한다. 프로세스가 겹치거나 반복이 누락/중복되면 실패한다.

모든 표본을 보존해 SDK/입력별 12개 통계 행을 만든다. 각 행은 5개 프로세스·
150개 표본의 min/p50/p95/p99/max/MAD와 process median을 포함한다. percentile은
`(N-1)*q` 선형 보간이며 outlier 제거는 0이다. 전체 SDK 순위를 만들지 않는다.

## 타이밍과 관측 분리

LLGL observer는 별도 untimed probe 하나에만 로드한다. native query와
LLGL instance 두 개·논리 장치 하나의 생성과 종료를 확인한다. 검증 시에는
고정 VVL core/sync를 사용하며 실제 진단을 요구한다. 검증 실패·잘못된 UUID·
device kind·누락 layer·손상 readback·sync 비활성·observer 누락을 측정 전에
검사하고 무관한 실패를 오류 대조의 성공으로 인정하지 않는다.

측정 프로세스에서는 VVL/observer와 `LD_DEBUG`를 사용하지 않는다.
LLGL은 측정 후 실제 runtime module snapshot을 남긴다. SDK/SDL 측정 프로세스는
module snapshot을 직접 출력하지 않는다. 같은 hash의 untimed probe/validation
실행과 frozen ICD/loader·명시적 환경, 매 프로세스의 native UUID·driver 대조를
근거로 결합한다. 이 한계를 `SDK_SDL_measurement_module_snapshot_observed=false`로
기록한다. SDL native handle을 직접 확인했다고 표시하지 않는다.

GNU verbose 명령·Ninja graph·raw `.d`·compiler 입력과 license 분류·생성 코드·
mode별 fingerprint·source ZIP·원본 출력·환경·PID·유한한 직렬 시간을 보존한다.
원본과 parsed 결과·샘플 합과 enclosing process 시간·전체 이미지 수·통계를
독립 재생한다. setup 파일·shared library·driver·source가 변하면 중단한다.
외부 binary/system/driver 파일은 재배포하지 않는다.

## software 검증과 실제 GPU 실행

software `smoke`는 물리 `run`과 별도 경로다. 같은 Linux software qualification의
선행 증거를 받고, 42개 프로세스에서 검증·관측·오류 대조와 짧은 plain timer를
실행한다. 검증/짧은 실행 각각 warmup 1회·3개 진단 행만 사용한다. 합계 전체
이미지 96개·2,588,083,680바이트·진단 행 72개이며 통계 행·측정 표본·수용은 0이다.

```sh
python3 tools/linux_upload_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-upload-evidence --output out/linux-upload-campaign-smoke
python3 tools/linux_upload_campaign.py check --output out/linux-upload-campaign-smoke --live
```

실제 GPU에서는 기존 수동 `linux-comparison-physical.yml`이 SDK/SDL·LLGL
qualification과 네 correctness 소비자 다음에 업로드 캠페인을 실행한다.
등록된 실제 GPU runner가 없으므로 이번에는 이 workflow를 실행하지 않았다.

```sh
python3 tools/linux_upload_campaign.py run --readiness out/linux-workloads-physical --llgl-correctness out/linux-llgl-upload-physical --output out/linux-upload-campaign-physical
python3 tools/linux_upload_campaign.py check --output out/linux-upload-campaign-physical --live
```

물리 전체 계약은 90개 프로세스(관측/검증/제어 30개 + 측정 60개), 전체 이미지
2,148개·57,908,372,340바이트다. 이 수치는 실행 시 요구하는 계약이며 이번
물리 측정 결과가 아니다. 성공한 실제 물리 캠페인의 `performance_accepted`
필드는 그 업로드 통계의 범위에만 적용한다. `physical_GPU_accepted`,
`P3_LBEN_01_completed`, Phase 3 수용 상태는 자동 완료하지 않는다.

## 남은 개발·수용

CPU 계약 테스트는 timing/body 보존, mode별 source binding, 독립 반복 순서,
validation/observer/loader trace 분리, 실제 오류 gate, 전체 참조·타입·장치와
통계의 누락/중복/outlier 보존을 검사한다. 물리 positive fixture는 synthetic
metadata이며 실제 GPU 실행이 아니다. Docker pilot의 readback·observer 검사는
별도 software 개발 점검이며 최종 고정 runtime 증거와 구분한다.

다음은 draw/instancing·PSO·자원 생성의 Linux timed consumer와 캠페인 이식이다.
업로드도 실제 GPU에서 같은 driver·source로 fresh 반복을 수집해야 최종 수용할
수 있다. 실제 표시와 Phase 3 gate는 별도로 남아 있다.

## 검증 기록

실행 소스는 `c9bc5191d2af1fba7b82c56aaedaf96f44f928fb`다.
[Linux native CI 41374639715](https://github.com/blackvrice/GraphicsSDK/actions/runs/41374639715)는
기존 SDK/SDL·LLGL correctness·물리 거부 대조 회귀 다음에 새 캠페인을 실행했다.

| 관측 | 결과 |
|---|---|
| 환경 | Linux x86_64, kernel 7.0.0-1017-azure, AMD EPYC 9V45, GCC 14.2.0 |
| 장치 | llvmpipe LLVM 20.1.2, native type 4 |
| device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| 캠페인 | 42개 native 프로세스·12개 거부·2개 core/sync 제어 |
| 전체 참조 | 이미지 96개·2,588,083,680바이트 |
| 진단 / 측정 / 통계 | 72행 / 0개 / 0행 |
| 실제 GNU compile unit / 입력 | 2 / 467 |
| 입력 분류 | 자체 4·생성 5·LLGL 86·Vulkan-Headers 13·system 359 |
| frozen 파일 / runtime 파일 | 1,124 / 42 |
| 새 LLGL timer fingerprint | `10d596d4a49b93307111611db42543e5e4b18e22a6ceb99b924df020d579a12a` |

[같은 커밋의 Windows/Linux CI 41374639467](https://github.com/blackvrice/GraphicsSDK/actions/runs/41374639467)는
각각 Python 1,398개와 입력 유지 검사를 통과했다. 새 테스트는 16개이며
로컬 전체 1,398개도 통과했다.
[Linux Vulkan compile 41374639457](https://github.com/blackvrice/GraphicsSDK/actions/runs/41374639457)도
통과했다. 물리 GPU 성공 경로는 실행하지 않았으며 로드맵 143개 ID·상태는 유지했다.

원본 376개 파일과 양 OS Python receipt/log는
[`2026-11-02-phase3-linux-upload-campaign`](../benchmarks/reports/validation/2026-11-02-phase3-linux-upload-campaign/summary.json)의
data ZIP·inventory·summary·독립 검증기로 보존했다. 현재 저장소 검증기로만
재생하며 archive 안의 Python/native 파일을 실행하지 않는다. 보존 hash는
외부 신뢰 서명이 아니다. 다음 명령은 원본·소스·타이머 이식·순서·runtime·
0개 측정/통계와 tested/executed Python hash를 재검사한다.

```sh
python3 benchmarks/reports/validation/2026-11-02-phase3-linux-upload-campaign/verify.py
```

offline 재검사는 새 GPU 측정이 아니다. 실제 Linux GPU fresh 수용과 후속
세 timed campaign·표시·Phase 3 gate는 미완료로 유지한다.

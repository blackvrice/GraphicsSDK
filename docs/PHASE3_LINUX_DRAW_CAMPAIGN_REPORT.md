# Linux 세 SDK draw/instancing 반복 측정 도구

2026-11-02. P3-LBEN-02의 실행기·타이머 이식이며 실제 Linux GPU 성능 수용은
미완료다. PSO/allocation timed campaign은 별도 남은 개발이다.

## 보존한 계약

고정 Windows `llgl_vulkan_draw_compare`의 전체 `Draw` 본문과 main의 반복·관측
루프를 보존한다. 65×37 전체 이미지, scissor `[5,3,55,31]`, 정점 3개·first
vertex/instance 0을 유지하며 draw1/draw1000/instanced1000을 비교한다. frame마다
blue/cyan clear와 red/green triangle을 바꿔 모든 바이트를 검사한다.

기록·제출·fence 대기·전체 CPU 시간 네 구간을 각각 보존하고 앞의 세 구간이
전체 시간을 분할하는지 검증한다. 자원/셰이더/pipeline 준비·전체 readback·
정리·warmup·validation은 통계에서 제외한다. CPU 완료 시간이며 GPU timestamp가
아니다. 각 SDK의 public API 차이와 LLGL의 render-pass clear 정책은 유지한다.

업로드 실행기의 source/compile/runtime 증거 수집·직렬 실행·재생을 공통으로
사용하고 draw의 계약·기하·관측·통계는 별도 검증한다. 기존 업로드 기본 API와
과거 증거 재생을 유지한다. 새로운 mode별 source fingerprint는 플랫폼 어댑터와
고정 workload/타이머 입력을 결합한다. SDK/SDL은 같은 qualification의 draw
바이너리를 사용한다.

## 관측과 측정 분리

세 SDK의 untimed probe 후 LLGL 별도 instance 관측 probe와 세 입력의 observer
감사를 수행한다. required VVL 검증에서도 LLGL observer를 함께 로드해 실제
draw/forward/instance/vertex·min/max instance·인자를 검사한다. native query와
LLGL instance 두 개·논리 장치 하나의 생성 및 종료도 확인한다.

잘못된 UUID/kind·누락 layer·core/sync 전달·sync 비활성·잘못된 draw/instance 수·
scissor·손상 readback·observer 누락을 측정 전에 검증한다. 각 오류는 지정한
실패·종료 코드 1·빈 stdout이어야 하며 무관한 실패는 인정하지 않는다.
실제 오류나 잘못된 기하/시간 분할이 나오면 후속 측정에 진입하지 않는다.

plain 측정 프로세스에는 observer/VVL/`LD_DEBUG`가 없다. LLGL의 native draw
계수는 이때 `null`이며 관측한 것처럼 숫자를 내보내지 않는다. 전체 readback과
실제 native device/queue는 계속 확인한다. LLGL은 측정 후 실제 module snapshot을
남긴다. SDK/SDL 측정 프로세스는 module snapshot을 직접 내보내지 않으며, 같은
바이너리의 untimed 실행·frozen ICD/loader·명시적 환경과 매 실행의 UUID/driver를
결합한다. `SDK_SDL_measurement_module_snapshot_observed=false`와 SDL native handle
미관측 한계를 유지한다. SDK/SDL의 명시적 instance API 1.1 요청과 LLGL의
별도 관측한 요청 값도 구분한다.

## 실행 범위

물리 `run`은 같은 source/qualification의 SDK/SDL readiness와 완료된 LLGL draw
correctness가 필요하다. `gnu-release-o2-no-ipo-unity-v1` 원본 cache·compile graph
증거가 없거나 software/guard-only이면 거부한다. SDK·SDL·소비자·LLGL은 동일
`-O2 -DNDEBUG`, IPO/unity OFF 정책을 검증한다. 명시한 native UUID와 실제 LLGL
native handle이 다르면 실패하며 다른 adapter로 재시도하지 않는다.

78개 직렬 프로세스는 선행 검증/관측/제어 33개와 측정 45개다. 세 입력 × 세 SDK ×
다섯 독립 프로세스에서 각각 warmup 5회·측정 30회를 실행한다. 원본 기본 표본은
1,350개이고, 같은 표본의 네 CPU 구간별 통계 행은 36개다. fixed seed로 시나리오
block을 섞고 입력별 서로 다른 다섯 SDK 순열을 인접 실행한다. 모든 표본·outlier를
보존해 min/p50/p95/p99/max/MAD와 process median을 구하며 제거 표본은 0이다.
서로 다른 열을 독립 반복으로 세거나 SDK 전체 순위를 만들지 않는다.

물리 전체 계약의 참조는 이미지 1,623개·15,613,260바이트다. 이는 실행 시 요구할
수치이며 이번 물리 측정 결과가 아니다. 성공 시 `performance_accepted`는 좁은
draw CPU 비교 범위에만 적용하고 `physical_GPU_accepted`, `P3_LBEN_02_completed`,
Phase 3 수용을 자동 완료하지 않는다.

```sh
python3 tools/linux_draw_campaign.py run --readiness out/linux-workloads-physical --llgl-correctness out/linux-llgl-draw-physical --output out/linux-draw-campaign-physical
python3 tools/linux_draw_campaign.py check --output out/linux-draw-campaign-physical --live
```

기존 physical 수동 workflow가 통제 qualification·네 correctness 소비자·업로드
캠페인 다음에 draw 캠페인을 실행한다. 실제 Linux GPU runner가 없어 이번에는
물리 workflow를 실행하지 않는다.

software `smoke`는 42개 프로세스(거부 15개·core/sync 2개 포함)에서 이미지
84개·808,080바이트를 검사한다. 관측/검증/짧은 plain timer의 기본 진단 행 63개는
원본에 보존하며 측정 표본·통계·성능 수용은 모두 0이다.

```sh
python3 tools/linux_draw_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-draw-evidence --output out/linux-draw-campaign-smoke
python3 tools/linux_draw_campaign.py check --output out/linux-draw-campaign-smoke --live
```

GNU verbose 명령·Ninja graph·raw depfile·입력/license 분류·mode별 생성 source와
fingerprint·source ZIP·환경·PID·원본 stdout/stderr·유한한 직렬 시간·파일 hash를
보존하고 독립 재생한다. 외부 binary/system/driver 파일은 재배포하지 않는다.

## 남은 개발·수용

CPU 계약 테스트와 Docker software pilot은 실제 GPU 수용이 아니다. 실제 Linux
GPU의 fresh draw/instancing 실행과 PSO·자원 생성 timed campaign 이식, 실제 표시와
Phase 3 gate가 남아 있다. 기존 Windows 성능값과 software 진단을 Linux 성능으로
재사용하지 않는다.

## 실제 검증과 재생 증거

소스 `bdb31227cea697a9595c2a0d26b1addf0ce8c4d6`의
[Linux native CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41383330422)가
통과했다. GCC 14.2.0, Linux x86_64, llvmpipe LLVM 20.1.2 software 장치에서
새 draw 캠페인 42개 프로세스와 이미지 84개·808,080바이트를 검증했다.
거부 제어 15개와 core/sync 전달 제어 2개를 포함한다. 기본 진단 행 63개는
원본에 남기고 측정 표본·통계 행·성능 수용은 0으로 유지했다.

성공한 LLGL 진단의 실제 native draw/forward 호출은 8,016개, instance는
16,008개, vertex는 48,024개다. plain timer에서는 native 계수를 null로 남긴다.
컴파일 2 units의 입력 469개는 generated 6·LLGL 86·자체 header 5·system 359·
Vulkan header 13개로 분류된다. runtime 파일 42개와 frozen 입력 1,132개를
보관했다. 같은 qualification의 통제 빌드 원본 graph는 SDK 6·SDL 251·probe 2·
소비자 8 units이며 모든 필요한 프로젝트의 O2/no IPO/unity 조건을 재생했다.

[동일 소스 Windows/Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41383330537)의
각 Python 계약 1,420개가 통과했다. 새 draw 계약 테스트 17개는 스케줄·시간 분할·
전체 readback·native 계수·관측 분리·통계·변조 거부·고정 workload 보존을 검사한다.
기존 upload 계약과 보관된 통제 upload 증거도 공통 실행기 변경 후 재생했다.

`benchmarks/reports/validation/2026-11-02-phase3-linux-draw-campaign/`에 원본 파일
397개를 담은 evidence ZIP, inventory·manifest·summary와 두 OS Python 로그·receipt를
보관한다. 현재 저장소의 검증기로만 재생하며 ZIP 안의 source/native 코드는
실행하지 않는다. 자체 hash 목록이며 외부 신뢰 서명을 주장하지 않는다.

```sh
python3 benchmarks/reports/validation/2026-11-02-phase3-linux-draw-campaign/verify.py
```

Build and correctness CI의 Windows/Linux 작업에도 이 독립 재생을 추가했다.
로드맵 143개 작업의 ID와 상태는 유지하며 P3-BEN-08/P3-LBEN-02에 준비 완료 증거를
추가했다. 실제 Linux GPU의 fresh draw 성능 수용과 PSO/allocation 타이밍 캠페인
개발은 남아 있다.

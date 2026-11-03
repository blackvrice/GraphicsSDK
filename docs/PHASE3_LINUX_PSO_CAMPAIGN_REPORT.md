# Linux 세 SDK PSO 반복 캠페인

2026-11-03. P3-LBEN-03의 Linux 반복 실행기 연결이다. 실제 Linux GPU 성능 수용은
미완료이며 software 진단을 성능 비교로 승격하지 않는다.

## 고정 계약과 실행

Windows `llgl-pso-comparison.json`의 pso1/pso8, 생성/public 해제 두 CPU 구간,
warmup 5회·측정 30회·다섯 독립 프로세스, seed와 인접 SDK 순열 계약을 유지한다.
GPU use/fence/idle, 전체 readback, descriptor 준비, teardown, warmup/validation은
통계 밖이다. shader 객체 생성·해제를 생성 구간에 포함한다. driver cache 상태,
native pipeline 개수, GPU 메모리 회수와 GPU timestamp는 확인하지 않는다.

기존 Linux PSO 소비자의 전체 lifecycle·반복 루프를 보존한다. native 출력 task는
기존 `P3-LBEN-03-pso-timer`를 유지하고 상위 receipt는 `P3-LBEN-03-pso-campaign`이다.
SDK/SDL은 같은 qualification에서 빌드한 pso 바이너리를 사용한다.

```sh
python3 tools/linux_pso_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-pso-evidence --output out/linux-pso-campaign-smoke
python3 tools/linux_pso_campaign.py check --output out/linux-pso-campaign-smoke --live
```

물리 `run`은 같은 source/qualification의 SDK/SDL readiness와 완료된 LLGL PSO
correctness를 요구한다. guard-only/software receipt는 거부하고 SDK·SDL·probe·
소비자의 `gnu-release-o2-no-ipo-unity-v1` 원본 cache와 compiler graph를 검증한다.
LLGL은 동일 O2/no IPO/unity 정책으로 빌드한다. UUID/kind가 다르면 실패하며 다른
adapter로 재시도하지 않는다. physical 수동 workflow에 연결하되 실제 runner가
없어 이번에는 해당 workflow를 실행하지 않는다.

## 측정 전 gate와 관측 분리

세 SDK probe, LLGL 별도 관측 probe와 입력별 native 감사, 세 SDK required VVL
검증, SDK별 누락 layer/잘못된 UUID/kind, LLGL core/sync 전달·sync 비활성,
누락 생성·해제·잘못된 pipeline·영역·scissor·readback 손상·observer 누락을 검증한다.
선행 31개 프로세스 중 하나라도 실패하면 후속 측정으로 진입하지 않는다. 거부
17개는 지정한 오류와 종료 코드 1·빈 stdout을 요구한다. core/sync 양성 대조는
2개다. LLGL required validation에서는 observer도 함께 로드해 실제 native
draw/forward/instance/vertex·인자와 생성·종료를 확인한다.

관측/validation은 이미지 hash를 별도 Python CPU 참조와 비교한다. plain 측정에는
observer/VVL/loader trace가 없고 native 계수는 null이다. native pipeline 개수를
draw 계수로 대체하지 않는다. SDK/SDL plain 프로세스의 module snapshot 미관측과
SDL native handle 미관측 한계를 유지한다. 명시적 SDK/SDL instance API 1.1과
별도 관측한 LLGL 요청 값도 구분한다.

물리 계약은 총 61개 프로세스(선행 31 + 측정 30), 기본 표본 900개다. 같은 표본의
두 CPU 열에 대한 통계 12행을 내보내며 모든 표본/outlier와 process median을
보존한다. 전체 이미지 1,082개·10,408,840바이트는 실행 시 요구할 수치이며 실제
물리 측정 결과가 아니다. 좁은 PSO CPU 비교가 성공해도 P3-LBEN-03/Phase 3 또는
전체 물리 GPU 수용을 자동 완료하지 않는다.

software smoke는 총 37개 프로세스, 이미지 56개·538,720바이트를 요구한다.
기본 진단 행 42개를 원본에 남기되 측정 표본·통계·성능 수용은 0이다.

## 증거와 검증

업로드/draw 공통 실행기의 직렬 프로세스·source ZIP·GNU verbose/Ninja/depfile·
license 분류·생성 adapter/fingerprint·실제 native module과 파일 hash·원본 로그를
재사용한다. PSO의 두 중첩 CPU 구간 합이 실제 프로세스 시간 안에 들어오는지도
검증한다. 기본 upload/draw 동작과 기존 보관 증거 재생을 유지한다.

추가 CPU 계약 테스트 13개는 고정 계약·cache 범위, gate 순서와 다섯 인접 SDK
순열, 모든 성공 모드, 이미지 총계, 관측/측정 분리, 잘못된 수치·lifecycle·hash·
native 계수·module·물리 prerequisite·정확한 오류 거부, 모든 표본/outlier 보존,
중첩 구간 시간과 수용 범위 변조를 검사한다. 합성 테스트는 GPU 실행 증거가 아니다.

## 남은 수용

실제 Linux GPU qualification과 같은 GPU/driver의 fresh PSO 비교는 미완료다.
allocation timed campaign 연결, 실제 표시와 최종 Phase 3 gate가 남아 있다.


## 실제 CI와 보관 증거

소스 `6b398213bd6e4c9def9d3e52c83f1e70a8d23d18`의
[Linux native CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41571988107)가
전체 통과했다. GCC 14.2.0, Linux x86_64, llvmpipe LLVM 20.1.2 software 장치에서
37개 캠페인 프로세스·거부 17개·core/sync 양성 대조 2개·전체 이미지 56개·
538,720바이트를 확인했다. 기본 진단 행 42개를 보존하며 측정 표본·통계 행은 0이다.
성공한 LLGL 진단의 native draw/forward 각각 72개, instance 72개, vertex 216개를
확인했다. 이는 native pipeline 생성 개수의 관측이 아니다.

컴파일 2 units의 입력 469개는 generated 6·LLGL 86·자체 header 5·system 359·
Vulkan header 13개다. runtime 파일 42개·frozen 입력 1,134개를 보관했다.
통제 qualification/소비자의 SDK 6·SDL 251·probe 2·consumer 8 units 원본 graph와
cache를 재생했다. 기존 upload/draw 캠페인과 Linux 전체 correctness 회귀도
같은 native 실행에서 통과했다.

[동일 소스 Windows/Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41571988104)의
각 Python 계약 1,441개와 [Linux Vulkan 컴파일](https://github.com/blackvrice/GraphicsSDK/actions/runs/41571988009)이
통과했다. 로컬에서도 동일 소스 계약 1,441개·입력 불변을 확인했다.

`benchmarks/reports/validation/2026-11-03-phase3-linux-pso-campaign/`에 원본 388개를
담은 evidence ZIP, inventory·manifest·summary와 두 OS Python receipt/log를 보관했다.
현재 저장소의 검증기로만 재생하며 ZIP 안의 source/native 코드는 실행하지 않는다.
자체 hash 목록이며 외부 신뢰 서명을 주장하지 않는다.

```sh
python3 benchmarks/reports/validation/2026-11-03-phase3-linux-pso-campaign/verify.py
```

Windows/Linux Build and correctness CI에도 이 재생을 추가했다. 로드맵 143개
작업 상태는 유지하고 P3-BEN-08/P3-LBEN-03에 준비 완료 증거를 추가했다.
실제 Linux GPU fresh 실행과 성능 수용, allocation timed campaign은 미완료다.

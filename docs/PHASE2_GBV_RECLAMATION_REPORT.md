# Phase 2 GBV 메모리 분리와 자원 재사용 검증

2026-10-22. 이전 미완료 메모리 회수 항목을 이어서 조사했다. SDK 없이 native texture 생성만으로
GBV 메모리 증가를 재현하고, 전체 GBV를 유지하는 고정 자원 재사용 스트레스 도구를 추가했다.
**재사용 profile은 통과했으며 원래의 반복 신규 자원 생성 gate는 여전히 미완료다.**
[이전 실패 보고서](PHASE2_MEMORY_STRESS_REPORT.md)와 policy·원본 출력은 그대로 보존한다.

SDK 공개 API·구현·기본 검증 설정은 바꾸지 않았다. SDK source fingerprint는
`833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1`이다.
이번 변경은 독립 검증 도구, 회귀 분석기, 문서와 재현 증거다.

## 원인 분리

각 case를 새 process에서 순차 실행한다. buffer 생성/복사, 빈 submit/idle, texture 생성,
descriptor view 생성, footprint 조회, clear-only, shader draw를 분리했다. SDK 객체를 거치지 않는
native committed/placed texture에서도 증가했다. zero initialization 생략과 optimized clear value도
이번 조건에서 증가를 멈추지 않았다. 따라서 이 방식들을 SDK allocator 수정으로 적용하지 않았다.

가장 작은 native 대조는 SDK Device/Queue/Fence/Resource를 생성하지 않는다. 동일 adapter에서
native debug device, 64×64 RGBA8 render target/D32 depth/일반 RGBA8 texture 세 개의 순차 생성·해제,
process/DXGI observer와 unfiltered ERROR/CORRUPTION callback만 사용한다. command queue 생성,
submit, shader, descriptor view, footprint 호출도 없다. 세 case의 LUID가 동일한지 확인한다.

CPU debug layer, full GBV, GBV state tracking만 끈 진단 대조를 비교한다. 마지막 설정은
[Microsoft 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/ne-d3d12sdklayers-d3d12_gpu_based_validation_flags)에
따라 resource state validation을 생략한다. **해당 설정은 원인 분리 전용 native process에만 쓰며,
SDK나 재사용 스트레스에 적용하지 않는다.** 검증을 줄인 대조의 안정성을 정상 GBV acceptance로
사용하지 않는다.

결과는 GBV texture state-tracking 경로와의 연관성을 지지한다. 정확한 runtime 내부 할당 원인,
특정 vendor/driver 결함, 전체 SDK의 누수 부재를 증명하지 않는다. 이 호스트의 제한된 짧은 진단이다.
Microsoft도 GBV 실행 비용을 설명하며 작은 데이터 집합에서 사용할 것을
[권고한다](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation).
그 권고 자체가 이번 메모리 증가가 정상 동작임을 증명하는 것은 아니다.

최종 native 수치와 20개 case의 원본은
[final-isolation](../benchmarks/reports/validation/2026-10-22-phase2-gbv-reclamation/final-isolation/) 및
[요약](../benchmarks/reports/2026-10-22-phase2-gbv-reclamation/summary.json)에 있다.

GTX 1660 SUPER, 동일 LUID의 최종 5초 process 결과는 다음과 같다. 이는 메모리 증가 원인을
좁히기 위한 관측이며 short process의 `PASS references`는 메모리 acceptance를 뜻하지 않는다.

| case | process private 증가 |
|---|---:|
| SDK buffer / native buffer 생성·해제 | 0.172 / 0.207MiB |
| idle / 빈 command submit | 0 / 2.379MiB |
| buffer copy 신규 / 재사용 | 4.020 / 2.949MiB |
| SDK texture 신규 생성·해제 | 173.828MiB |
| clear-only / draw 신규 texture | 154.156 / 146.309MiB |
| native texture / view 추가 / footprint 추가 | 168.230 / 166.508 / 169.285MiB |
| footprint 조회만 | 0MiB |
| native placed / NOT_ZEROED / optimized clear | 161.313 / 172.652 / 181.711MiB |
| SDK 없는 native CPU debug layer | 0.117MiB |
| SDK 없는 native full GBV | 168.938MiB |
| SDK 없는 native GBV, state tracking 생략 | 0.117MiB |

full GBV native case는 831 textures를 생성했고 CPU-only는 819개, reduced-tracking 대조는
867개를 생성했다. 동일한 시간 제한과 sleep 간격이지만 처리량이 다르므로 증가량을 성능 비교로 쓰지 않는다.
앞선 `isolation`, `render-isolation`, `native-isolation`, `heap-isolation`, `tracking-isolation`은
10초 탐색 기록이다. 첫 `isolation`은 중간 harness source snapshot이 없어 acceptance 증거에서
제외한다. 나머지 탐색도 최종 고정 source의 결과와 섞거나 장시간 통과로 재분류하지 않는다.

## 구현한 재사용 profile

`tools/memory_validation_probe/ReusableFrame.hpp`는 공개 SDK API만 사용하는 명시적 frame slot이다.
8개 slot의 texture, buffer, command list와 하나의 pipeline을 한 번 생성한다. slot마다 이전 fence
완료 후 upload/constant buffer를 다시 쓰고 다음 명령을 녹화한다. 숨은 SDK cache나 allocator가 아니다.

각 frame은 changing CBV 두 범위, sampled texture upload, scissor draw 두 번, MRT 두 장/D32 depth,
256KiB upload→device→readback copy와 timestamp resolve를 수행한다. 매 8-frame round의 마지막 fence를
기다리고 color/depth 전체 pixel과 복사 buffer의 모든 byte를 CPU reference와 비교한다. 다음 round
전에 waitIdle과 진단 조회로 pending batch/참조 0 및 고정된 객체/자원/할당 크기를 확인한다.
마지막 pool/pipeline 해제 후 SDK 다섯 보유 값 모두 0이어야 한다.

## 실행 전에 고정한 재사용 기준과 결과

[별도 policy](../tools/bounded_gbv_policy.json)는 30초 warmup + 300초 측정, 초당 최소 8 frame,
최소 60개 drained checkpoint, checkpoint 간격 최대 15초, 객체 105개/고유 자원 104개,
할당 합 최대 64MiB를 요구한다. 기존 touched CPU 32MiB/GPU 64MiB retain/release 양성 대조와
[원래 memory policy](../tools/memory_stress_policy.json)의 후반 50% 판정/메모리 허용치를 그대로 쓴다.
private commit 32MiB drift·8MiB/min slope·128MiB range, handles 16·4/min·64,
DXGI local/nonlocal 각각 16MiB·4MiB/min·128MiB다. 정책 기준은 실행 후 완화하지 않았다.

| 항목 | 관측 결과 |
|---|---:|
| GPU state tracking | 활성화 |
| warmup / 측정 | 30초 / 300.019초 |
| 전체 frames / 전체 이미지 | 70,800 / 212,400 |
| 전체 비교한 buffer bytes | 18,559,795,200 |
| drained / 후반 checkpoint | 61 / 31 |
| 고정 SDK objects / native resources | 105 / 104 |
| 고정 SDK resource allocation 합 | 11MiB |
| 최종 SDK 보유 / native ERROR·CORRUPTION | 0 / 0 |
| 후반 private commit median 증가 / slope / range | 0.03125MiB / 0.01593MiB/min / 0.06641MiB |
| 후반 handles median 증가 / slope / range | 2 / 0.997/min / 4 |
| 후반 local/nonlocal 증가·slope·range | 모두 0 |

frame/reference 합계에는 warmup도 포함한다. 최소 측정 작업량은 warmup의 frame 수를 뺀 값으로
별도 확인한다. private 초기값→최종 증가 35.371MiB는 양성 대조/초기 pool·검증 cache를 포함하는
다른 값이며 후반 plateau drift와 혼동하지 않는다. 측정은 checkpoint 관측이며 연속 peak가 아니다.
unfiltered callback은 native text queue 저장 한도나 필터와 독립적이다. 별도 counter 대조에서는
의도적으로 주입한 오류 6개를 모두 확인한다. 정상 frame의 오류 0과 구별한다.

[분석기](../tools/analyze_bounded_gbv.py)는 profile/source/adapter identity, GBV state tracking,
양성 대조·실행 시간·고정 자원 수·drain·완전한 reference·오류 관측·메모리 기준을 검사한다.
5초 smoke, 검증 축소, missing/error counter, DXGI 실패, 변경된 자원 집합, 회수 실패,
작업량·시간·checkpoint 부족 또는 증가하는 메모리는 거부한다. 결과는 명시적으로
`bounded_working_set_accepted=true`, `unbounded_texture_churn_accepted=false`다.

## 재현

Windows x64 MSVC, Windows SDK/Graphics Tools와 현재 SDK Release library가 필요하다.
GPU 검증끼리 겹치지 않게 순차 실행한다. output 폴더는 새 경로여야 한다.

```powershell
cmake -S tools/memory_validation_probe -B out/memory-validation-probe -G "Visual Studio 17 2022" -A x64
cmake --build out/memory-validation-probe --config Release --parallel 1
python tools/memory_validation_probe/run.py --profile isolation --seconds 5 --output out/gbv-isolation-new
python tools/memory_validation_probe/run.py --profile bounded --seconds 300 --output out/gbv-bounded-new
python tools/analyze_bounded_gbv.py out/gbv-bounded-new/bounded-reuse.jsonl --fingerprint 833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1 --output out/gbv-bounded-new/acceptance.json
```

runner는 실행 전 native exe/SDK library/harness 원본 해시와 source 복사본을 보존하고 실행 뒤
변경 여부를 검사한다. bounded 실행의 runner 원본과 최종 isolation runner 원본은 보존 경로가
각각 다르다. bounded 후 runner에 timeout process 종료 처리를 추가했으며, C++ workload와 binary는
두 실행 사이에 바꾸지 않았다. 재사용 policy는 실행 전 고정했고 분석기와 두 policy 최종 해시는
acceptance/evidence에 기록한다. 최신 소스 archive 이름에는 SDK fingerprint와 harness hash를
함께 넣어 같은 SDK의 다른 도구 버전과 구별한다.

## 회귀와 남은 작업

최종 회귀·compiler-read 감사·기존 패키지 보존 결과는
[현재 증거](../benchmarks/reports/2026-10-22-phase2-gbv-reclamation/)에 기록한다. SDK 소스와 library가
그대로이므로 기존 SDK ZIP을 해시/내용 검증하며 새 SDK 배포본으로 재포장하지 않는다.
새 stress inventory에는 공개 SDK header를 사용하는 ReusableFrame helper만 정확히 추가 허용한다.
신규 외부 dependency나 competitor/Sandbox 수정은 없다.

이번 실행의 core CTest 22/22, Windows D3D12-OFF 4/4, Python 82/82가 통과했다.
Python에는 검증 축소/오류 관측 누락/누수/부족한 측정 등 분석기 거부 회귀 10개를 추가했다.
최종 isolation은 19개 정상 case의 native ERROR/CORRUPTION 0과 별도 intentional counter 대조
1개를 통과했다. 새 stress 감사는 compiler-read 22 inputs/1 component이며 기존 SDK/common 감사
`--check`도 그대로 통과했다. 기존 6개 SDK ZIP 해시가 일치했고, 최신 SDK ZIP의 내용 감사도 통과했다.
공통 benchmark 72개와 설치 consumer는 이번에 재실행하지 않았으며 동일 SDK의 이전 결과와
현재 binary/package 해시를 확인했다. 이번 도구 추가를 새 SDK 성능 campaign으로 표현하지 않는다.

이 profile은 유한한 자원 집합 재사용의 실사용 검증과 최소 native 재현을 제공한다. 원래
unbounded resource creation GBV 메모리 증가는 여전히 남아 있다. 다음 단계는 다른 host/runtime에서
이 최소 재현을 대조하거나, 검증을 유지하면서 해당 증가를 해결하는 방안을 별도로 검증하는 것이다.
multi-hour stress, interactive presentation, 예상치 못한 TDR/hot-unplug, 다른 adapter/driver/OS,
Linux/macOS/Vulkan/Metal와 remote CI는 이번 실행에 포함하지 않는다. 새 성능 순위도 주장하지 않는다.

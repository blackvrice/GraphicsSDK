# Linux LLGL 네 워크로드의 물리 GPU 검증 경로

2026-11-02. P3-BEN-08·P3-LBEN-01~04의 실행 경로 준비다. 실제 Linux GPU
환경이 없어 물리 성공 실행·성능 비교·최종 수용은 미완료로 유지한다.

## 구현

upload, draw/instancing, PSO, buffer/texture allocation 네 소비자에 명시적인
`--mode physical`을 추가했다. 같은 커밋에서 성공한 물리 LLGL 최소 probe,
SDK/SDL qualification, 명시한 ICD·UUID·native integrated/discrete 타입과
보조 PCI/driver 기록이 있어야 빌드를 시작한다. `run`은 software 또는 guard
receipt를 물리 qualification으로 받아들이지 않는다. 실제 LLGL native
device/driver UUID·이름·vendor/device·API·queue·runtime binding은 기존 검사로
대조하며 다른 adapter로 자동 재시도하지 않는다.

`tools/linux_llgl_workload_mode.py`가 프로필 정책을 공유한다.
`GFX_LLGL_WORKLOAD_MODE=physical`은 생성된 `Platform.hpp`의 세 부분만
바꾼다. 명시적 physical 환경 값 요구, native type/UUID 선택, 출력 mode다.
소비자별 `linux-llgl-<profile>-physical` fingerprint는 모든 adapter 입력과
공통 정책 소스 hash를 포함한다. software 빌드와 구분하며 알 수 없는 mode는
거부한다. 기존 workload/control·CPU 참조·observer 본문과 승인된 정확히
10줄 LLGL vendor patch는 보존한다. 시스템 driver/정책은 변경하지 않는다.

정상 물리 실행 시 `physical_<profile>_correctness_verified=true`는 그
소비자의 전체 참조·control 검사만 뜻한다. `physical_GPU_accepted`,
`P3_BEN_08_completed`, `P3_LBEN_0x_completed`, performance acceptance는 false로
유지한다. 성능 표본은 0이며 diagnostic 시간을 새 성능 비교로 사용하지 않는다.
native 할당 수·driver cache·GPU 메모리 회수 수용도 추가하지 않는다.

## 실제 GPU에서 실행할 계약

아래는 실행 시 요구할 기존 계약의 수치이며 이번 물리 측정 결과가 아니다.

| 소비자 | 전체 프로세스 | 정상 / core·sync / 거부 | 참조 이미지 / 버퍼 | 참조 바이트 |
|---|---:|---:|---:|---:|
| upload | 15 | 8 / 2 / 5 | 32 / 0 | 862,694,560 |
| draw·instancing | 17 | 6 / 2 / 9 | 24 / 0 | 230,880 |
| PSO | 17 | 4 / 2 / 11 | 16 / 0 | 153,920 |
| allocation | 23 | 8 / 2 / 13 | 8,008 / 8,008 | 109,837,728 |
| 합계 | 72 | 26 / 8 / 38 | 8,080 / 8,008 | 972,917,088 |

기존 `.github/workflows/linux-comparison-physical.yml`은 같은 fresh checkout의
SDK/SDL 물리 qualification → 대표 workload → 승인 LLGL 최소 probe → 네 LLGL
전체 workload를 직렬 실행하고 원본 evidence를 업로드한다. 기존 runner label과
`icd`, `device_uuid` 입력을 유지한다. 실제 runner가 등록되기 전에는 실행하지 않는다.
GCC/G++ 14, Ninja, Vulkan loader/ICD, X11/Xrandr 개발 헤더, 공유 OpenSSL Crypto와
기존 pinned VVL build 의존성이 설치되어 있어야 한다.

```sh
python3 tools/linux_llgl_probe.py run --mode physical --qualification out/linux-comparison-physical --output out/linux-llgl-physical
for profile in upload draw pso allocation; do
  python3 "tools/linux_llgl_${profile}.py" run --mode physical --probe out/linux-llgl-physical --output "out/linux-llgl-${profile}-physical"
  python3 "tools/linux_llgl_${profile}.py" check --output "out/linux-llgl-${profile}-physical" --live
done
```

check는 저장된 mode로 source ZIP·adapter 재생·실제 GNU 명령/depfile·runtime
hash·전체 readback/control·프로세스 순서와 유한한 직렬 시간을 재검사한다.
`--live`는 원래 Linux host의 입력 파일까지 재검사한다. 외부 binary/system
파일은 재배포하지 않는다. 기본 CLI 모드는 계속 software-diagnostic이며
과거 software archive의 생성 코드·fingerprint 재생 규칙을 유지한다.

## software host에서 실제 확인하는 거부 대조

고정 VVL을 사용한 software CI는 물리 전용 최소 probe의 guard receipt를
받아 네 물리 소비자를 각각 새로 빌드한다. 네 소비자마다 native CPU 장치,
0 UUID, software 모드 요청, 필수 validation layer 누락의 네 제어를 실행한다.
각각 종료 1·빈 stdout·지정 오류가 있어야 성공이며 무관한 오류는 거부한다.
네 소비자 합계는 16개 거부 프로세스다. 선행 최소 probe의 네 제어는 별도다.

```sh
for profile in upload draw pso allocation; do
  python3 "tools/linux_llgl_${profile}.py" guard --mode physical --probe out/linux-llgl-physical-guard --output "out/linux-llgl-${profile}-physical-guard"
  python3 "tools/linux_llgl_${profile}.py" check --output "out/linux-llgl-${profile}-physical-guard" --live
done
```

guard는 qualification이 아닌 명시적 제어 경로다. LLGL renderer를 만들기 전에
멈추며 전체 workload를 실행했다고 표시하지 않는다. 이미지/버퍼/바이트와
성능 표본은 정수 0, `physical_guard_only=true`, software/physical correctness는
모두 false다. 정상 물리 실행용 전체 수치를 guard 결과로 출력하지 않는다.

새 CPU 계약 테스트는 네 생성 main/support/observer의 mode 간 바이트 보존,
모드·정책 hash 결합, qualification의 native type/PCI/완료 범위, guard와 run의
분리, 정확한 argv/오류, false/0 타입 혼동, 전체 참조·실제 native identity·성능
수용 제한을 검사한다. 물리 positive metadata fixture는 synthetic 테스트이며
실제 GPU 실행 증거가 아니다.

## 남은 개발·수용

네 소비자의 물리 경로를 구현했으나 실제 GPU positive 실행은 아직 미검증이다.
다음 구현은 Linux 세 SDK timed campaign·동일 장치/driver의 fresh 반복 수집과
통합 비교다. 이후 실제 Linux GPU에서 BEN-07/08·LBEN-01~04 계약을 실행해야
최종 수용할 수 있다. 실제 X11 화면 조작·Phase 3 gate도 별도로 남아 있다.
기존 software 통합 bundle과 당시 handoff는 관측 기록 그대로 보존한다.

## 검증 기록

실행 소스는 `b78b8b82724c857a81fba664831d1916d37a54e3`이다.
[Linux native CI 41366668887](https://github.com/blackvrice/GraphicsSDK/actions/runs/41366668887)은
기존 software 최소 probe·네 workload·SDK/SDL matrix·세 SDK 통합과 선행
physical 최소 probe의 네 제어를 통과한 뒤, 다음 네 물리 소비자 빌드를 검사했다.

| 물리 소비자 빌드 | 실제 GNU unit / 입력 기록 | 지정 거부 |
|---|---:|---:|
| upload | 1 / 459 | 4 |
| draw | 2 / 463 | 4 |
| PSO | 2 / 464 | 4 |
| allocation | 2 / 463 | 4 |

네 소비자 모두 동일 source·probe·qualification을 사용했고, 총 7개 compile
unit과 16개 거부를 확인했다. 공통 source/build/runtime 파일 결합은 1,082개이며
동일 경로의 hash 충돌과 소비자 사이의 프로세스 겹침도 재검사했다. 정상
workload·core/sync 제어·readback·성능 표본은 이 guard 결과에서 모두 0이다.

[Windows/Linux CI 41366668814](https://github.com/blackvrice/GraphicsSDK/actions/runs/41366668814)는
같은 커밋에서 각각 Python 1,382개와 입력 유지 검사를 통과했다. 새 정책 테스트는
13개이며 관련 LLGL 테스트 100개와 로컬 전체 1,382개도 통과했다.
[Linux Vulkan compile 41366668774](https://github.com/blackvrice/GraphicsSDK/actions/runs/41366668774)도
통과했다. 로드맵 143개 ID·상태는 유지했다.

원본 396개 파일은
[`2026-11-02-phase3-linux-llgl-physical-workloads`](../benchmarks/reports/validation/2026-11-02-phase3-linux-llgl-physical-workloads/summary.json)의
data ZIP과 inventory로 보존했다. 양 OS Python receipt/log·summary·독립 검증기도
포함한다. 현재 저장소의 검증기만 실행하며, archive의 Python/native 파일은
실행하지 않는다. 저장한 hash는 보존 확인용이며 외부 신뢰 서명은 아니다.

```sh
python3 benchmarks/reports/validation/2026-11-02-phase3-linux-llgl-physical-workloads/verify.py
```

offline 재검사는 16개 native 실패 기록과 tested/executed 소스 hash의 일치를
확인하며 새 물리 GPU 실행을 뜻하지 않는다. 실제 GPU positive 경로·성능·
BEN-08/LBEN/Phase 3 수용은 계속 미완료다.

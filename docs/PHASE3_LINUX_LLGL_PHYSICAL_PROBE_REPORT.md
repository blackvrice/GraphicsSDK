# Linux LLGL 물리 GPU 최소 검증 경로

2026-11-02. P3-BEN-08의 실행 경로 준비이며 완료 수용이 아니다.

## 구현 범위

`tools/linux_llgl_probe.py run --mode physical`은 같은 커밋의 성공한
SDK/SDL 물리 qualification을 요구한다. 명시한 ICD와 UUID, native
integrated/discrete 타입, PCI vendor/device와 알려진 bound driver를 기존
검증기로 재검사한다. 원본 qualification과 실제 LLGL native device의
UUID·driver UUID·이름·vendor/device·driver/API·queue·환경·실제 runtime
경로가 일치해야 한다. 기본 LLGL adapter가 다른 장치를 선택하면 실패하며
다른 adapter로 자동 재시도하지 않는다.

`GFX_LLGL_PHYSICAL_PROFILE=ON`은 software 빌드와 다른 fingerprint를 만든다.
물리 바이너리는 software 모드를 거부하고 native CPU/virtual/other 타입을
LLGL renderer 생성 전에 거부한다. software 기본값과 기존 archive replay는
유지한다. LLGL pin과 승인된 정확히 10줄 transfer-src overlay는 유지하며
upstream checkout은 수정하지 않는다.

실제 물리 qualification에서는 일반/필수 validation identity·smoke 4개,
host-only callback control 1개, wrong UUID/kind/missing-layer 거부 3개를
직렬 실행한다. 두 smoke는 각각 변경되는 4,096바이트 버퍼를 3회
write/copy/fence/read하고 모든 바이트를 대조한다. 합계 24,576바이트다.
callback 제어는 특정 VUID를 실제 callback으로 확인하며 GPU 명령을
기록하지 않는다. GNU verbose 명령·raw depfile·license/input audit·소스 ZIP·
runtime 파일 hash·환경·PID·직렬 시간·원본 stdout/stderr를 보존한다.

성공 시 `physical_probe_qualified=true`는 이 최소 probe만 뜻한다.
`physical_GPU_accepted`, `P3_BEN_08_completed`, workload/performance acceptance는
false이며 성능 표본은 0이다. application instance API 요청을 직접 관측했다는
주장도 추가하지 않는다.

## 실행

Linux x86_64 self-hosted runner에는 GCC/G++ 14, CMake 3.25+, Ninja, 설치된
Vulkan loader와 물리 ICD, X11/Xrandr 개발 헤더, pinned VVL build의 기존
의존성이 필요하다. sudo/설치·driver 변경은 workflow가 수행하지 않는다.
fresh checkout 아래에 고정 Vulkan-Headers/SDL/LLGL을 받아야 한다.

수동 `.github/workflows/linux-comparison-physical.yml`은 이 LLGL checkout과
최소 probe를 포함한다. runner label은
`self-hosted,Linux,X64,graphicssdk-gpu`이며 기존 `icd`, `device_uuid` 입력을 쓴다.
등록된 실제 runner가 있을 때 다음 명령을 실행한다.

```powershell
gh workflow run linux-comparison-physical.yml --repo blackvrice/GraphicsSDK --ref main `
  -f "icd=/absolute/installed/physical_icd.json" -f "device_uuid=EXACT_LOWERCASE_32_HEX_UUID"
```

동일 Linux checkout에서 직접 실행할 수도 있다.

```sh
python3 tools/linux_llgl_probe.py run --mode physical --qualification out/linux-comparison-physical --output out/linux-llgl-physical
python3 tools/linux_llgl_probe.py check --output out/linux-llgl-physical --live
```

`check`는 저장된 receipt의 mode를 사용한다. `--live`는 원래 Linux host의
소스·build·runtime hash까지 재검사한다. 외부 binary/system/driver 파일은
artifact에 재배포하지 않는다.

## 실제 Linux software 거부 대조

software CI는 물리 프로필을 별도 fresh build에서 실제 컴파일한 후 다음
네 실패를 확인한다. 이전 software build와 evidence를 변경하지 않는다.

| 제어 | 반드시 발생할 오류 |
|---|---|
| CPU 장치 + physical 모드 | Requested probe kind differs from native device type |
| 0 UUID | Exact UUID absent or ambiguous |
| software 모드 요청 | Physical qualification only |
| 필수 layer 검색 경로 비움 | Required comparison validation layer absent |

```sh
python3 tools/linux_llgl_probe.py guard --mode physical --qualification out/linux-comparison-evidence --output out/linux-llgl-physical-guard
python3 tools/linux_llgl_probe.py check --output out/linux-llgl-physical-guard --live
```

각 프로세스는 종료 코드 1, 빈 stdout, 지정 오류를 요구한다. 무관한 실패를
성공으로 인정하지 않는다. 네 제어 모두 LLGL renderer 생성 전에 멈추므로
LLGL 논리 장치·readback·성능 실행의 증거가 아니다. guard receipt는
`physical_guard_only=true`, `physical_probe_qualified=false`,
`minimal_buffer_reference_accepted=false`, 검증 바이트/성능 표본 0을 기록한다.

## 남은 작업

실제 Linux GPU 환경이 제공되지 않아 물리 경로의 성공 실행은 미검증이다.
기존 LLGL upload/draw/PSO/allocation 소비자는 계속 software-only다. 다음
구현은 이 네 소비자를 물리 qualification에 연결하고, Linux 세 SDK timed
campaign과 fresh 반복 수집을 준비하는 것이다. 이후 실제 GPU/driver에서
실행해야 BEN-07/08·LBEN-01~04를 수용할 수 있다. 실제 X11 화면 조작과
Phase 3 gate도 별도로 남아 있다.

2026-11-01 통합 bundle과 handoff는 당시 software 검증 기록 그대로 보존한다.
그 handoff의 LLGL physical 미구현 설명은 이 최소 probe 추가 이전 상태이며,
네 소비자의 physical 경로는 여전히 추가 구현이 필요하다.

# P3-BEN-07 — Linux 비교 probe 이식·software 준비 검증

2026-10-27 실행 · 2026-10-28 정리. **이식과 실제 Linux software 검증을 마쳤으며,
P3-BEN-07의 물리 GPU 수용은 미완료로 유지한다.** 사용 가능한 Linux GPU 환경이 없다는
사용자 확인에 따라 실제 GPU용 도구·수동 workflow까지 준비하고 software 검증을 진행했다.

[검증 요약](../benchmarks/reports/2026-10-27-phase3-linux-comparison-probe/summary.json)과
[원본 receipt](../benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/native/linux-comparison-evidence/receipt.json)에
`physical_probe_qualified=false`, `P3_BEN_07_completed=false`, 시간 표본 0개와
workload/performance 미수용을 명시했다. P3-BEN-08 workload 이식, P3-LNX-15 물리 공통 suite,
P3-BEN-09 GPU timestamp 비교의 완료 상태도 변경하지 않았다.

## 구현한 경로

- [`LinuxEnvironment.hpp`](../benchmarks/common/LinuxEnvironment.hpp)는 Linux loader의 실제 경로,
  Vulkan native UUID·driver UUID·raw API/driver version·장치 종류와 `/proc`, `uname`, RAM,
  GCC 식별을 수집한다. Linux에는 DXGI LUID를 요구하거나 합성하지 않으며 `device_luid=null`이다.
- [`Probe.cpp`](../benchmarks/linux_comparison/Probe.cpp)는 SDK와 SDL의 새 logical device를 생성한다.
  SDK는 전체 UUID에서 파생한 정확한 adapter ID로 선택한다. SDL 공개 이름은 native 목록의
  단일 장치와 대조하며 중복 이름·UUID와 다른 선택을 거부한다. SDL native handle을 직접
  확인했다는 주장은 하지 않는다. 창·표면·자원·draw·readback·timestamp는 실행하지 않는다.
- [`linux_comparison.py`](../tools/linux_comparison.py)는 새 SDK build/install, 고정 static SDL,
  probe를 빌드한다. 실제 package 경로가 새 SDK/SDL root 안에 있는지 검사하고 설치된 SDK
  library/header 바이트를 대조한다. 실제 loader/ICD/layer 경로와 해시, 소스·빌드 입력,
  명령·child 환경·PID·직렬 구간·stdout/stderr를 보존한다.
- validation 활성/비활성은 별도 process다. core/sync 설정과 실제 고정 layer 로드를 확인하며,
  누락 layer·잘못된 UUID·다른 장치 종류는 각 SDK에서 실패해야 한다. 이 결과는 장치 생성
  probe의 검증이며 렌더링 workload validation이나 성능 수용을 대신하지 않는다.

기존 Windows DXGI helper와 네 Windows C++ 비교 소비자는 변경하지 않았다. SDK 제품 소스
28파일도 그대로이며 Linux SDK fingerprint는
`fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79`다.

## 실제 실행과 검증 결과

실행 소스는 `ece1c87c613802a97fe8642f1755935a5c1058f4`다.
Ubuntu hosted CI에서 관측한 값은 다음과 같다.

| 항목 | 실제 값 |
|---|---|
| 장치 | llvmpipe (LLVM 20.1.2, 256 bits), native CPU 종류 4 |
| device UUID | `6d65736132352e322e382d3075627500` |
| driver UUID | `6c6c766d706970655555494400000000` |
| UUID-derived SDK ID | 문자열 `5892941257450743403` |
| native API/driver raw 값 | `4211006` / `104865800` |
| 환경 | Linux x86_64, kernel `6.17.0-1022-azure`, GCC `14.2.0` |
| CPU | Intel Xeon Platinum 8370C @ 2.80GHz |
| SDL | 고정 commit `3b2bc951500ca6a1f428af77e036e732d9a8afe3` |
| Vulkan-Headers | 고정 commit `b379292b2ab6df5771ba9870d53cf8b2c9295daf` |
| validation | 고정 VVL 1.3.290와 lock의 6개 component, workspace 안에서 별도 빌드 |

SDK/SDL × validation 2종 × 독립 2회인 **probe 8개**가 모두 일치했다.
잘못된 UUID·누락 layer·software를 physical로 요청한 **거부 확인 6개**도 통과했다.
전용 native inventory process 1개와 CPU selection 검사 9개 assertion/7개 거부가 별도로
통과했다. 이 CPU selection의 physical positive model은 합성이며 실제 GPU 실행이 아니다.

새 Python 검사 19개와 [전체 회귀 검사 876개](../benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/python-local/receipt.json)가
실패·오류·건너뜀 0으로 통과했다. 같은 소스의
[software native 실행 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40506571470),
[Windows·Linux 회귀 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40506571759),
[Linux Vulkan 컴파일 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40506571617)가 성공했다.

[원본 GitHub artifact](../benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/github-artifact.zip)는
GitHub digest `sha256:ed1928cf43a6e5530bb26ca2bd413045962c5553dbedc4d7b4d2bb5a2abf5df6`와
일치했다. 원본 ZIP과 풀어 보관한 파일을 byte 단위로 대조한다.
[44파일 source capsule](../benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/source-manifest.json)은
실행 당시 Linux 소스 해시와 일치하고 SDK 28파일은 직전 source ZIP과 같다.
외부 runtime의 실행 파일·라이브러리는 배포하지 않았다.

Windows 줄바꿈이 적용된 작업 파일로는 Linux 소스 fingerprint가 다르므로, 재검증은
Linux 원본 바이트를 임시 복원한다. 원본 receipt/output을 수정하지 않고 **19개 변조**
(완료·physical·workload·성능 승격, 가짜 표본, 소스/driver/kernel 변경, 누락·순서·중첩,
명령/validation 혼입, control 성공, raw/parsed 결과와 runtime/inventory/CPU control 변경 등)를
모두 거부했다. 보관 근거가 있는 커밋에서는 Windows·Linux CI도 이 offline 검사를 실행한다.

```text
python benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/verify.py --json
```

이 명령은 GPU나 native executable을 실행하지 않는다.

## 남은 실제 GPU 수용

실제 Linux physical probe 실행 0회, 수동 physical workflow dispatch 0회다.
사용 가능한 GPU 환경이 없으며 [로컬 WSL 목록 조회](../benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/host/wsl-list.json)도
빈 목록이었다. 시스템·배포판·driver 설정을 변경하지 않았다.

필요한 입력은 **Linux GPU 접속 대상 또는 준비된 self-hosted runner, 설치된 물리 ICD JSON
경로, 정확한 device UUID**다. physical 모드는 integrated/discrete native 종류와 같은
vendor/device의 PCI GPU·Linux driver를 요구한다. PCI 관측은 보조 근거이며 UUID별 PCI 주소를
직접 매핑했다는 주장은 아니다. Software·virtual·중복 이름·다른 SDL 선택은 거부한다.

[재현 명령과 준비 조건](../benchmarks/linux_comparison/README.md),
[물리 GPU 수동 workflow](../.github/workflows/linux-comparison-physical.yml)를 준비했다.
물리 환경에서 SDK/SDL·driver/compiler·고정 validation 원본을 실제 확보한 뒤 P3-BEN-07
완료를 판단한다. **로드맵은 todo를 유지하며 Phase 3 최종 수용 의존성도 그대로다.**

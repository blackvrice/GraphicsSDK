# Phase 3 — Linux 물리 GPU 실행 준비·환경 확인

2026-10-25 · `P3-LNX-15` **미완료 유지** · 도구 검증 커밋 `0ee9042`.

물리 Linux GPU에 접속할 대상이 아직 확인되지 않아 실제 공통 suite·validation 수용은 실행하지 못했다. 이번에는 하드웨어 전용 실행 도구와 수동 CI를 준비하고, **실제 Ubuntu software ICD가 하드웨어 수용으로 처리되지 않는 거부 동작**을 검증했다. 이 CI 성공을 `P3-LNX-15` 완료 근거로 합산하지 않는다.

[준비 검증 요약](../benchmarks/reports/2026-10-25-phase3-linux-hardware-readiness/summary.json), [software 거부 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40197045560), [원본 preflight](../benchmarks/reports/validation/2026-10-25-phase3-linux-hardware-readiness/native/preflight.json), [환경 조사](../benchmarks/reports/validation/2026-10-25-phase3-linux-hardware-readiness/environment/host-inventory.json)에 결과를 보존한다. 실제 물리 GPU 실행 0회, 물리 GPU 정상 suite 실행 0회이며 최종 hardware acceptance receipt는 없다.

## 현재 실행 환경

연결된 GraphicsSDK 프로젝트 실행 host는 Windows다. PATH에서는 Windows OpenSSH와 WSL을 찾았고 Docker/Podman CLI는 찾지 못했다. 사용자 SSH config도 없다. 특정 Linux SSH 대상이나 GPU runner 접속 정보는 제공되지 않았다. 저장소에 등록된 모든 self-hosted runner가 없다고 단정하는 조사는 아니다.

Windows에서 별도 권한으로 허용된 읽기 전용 WSL 조회의 원본 바이트와 반환 코드를 보존했다.

| 조회 | 실제 결과 | 해석 |
|---|---|---|
| `wsl --list --verbose` | 반환값 `4294967295`, 설치된 배포가 없다는 출력 | 시작할 Linux 배포판 미확인 |
| `wsl --status` | 반환값 0, 기본 버전 2, 가상화가 활성화되지 않아 WSL2를 시작할 수 없다는 출력 | 상태 조회 성공이며 Linux 실행 성공이 아님 |

이전 [P3-LNX-01 조사](PHASE3_LINUX_ENVIRONMENT.md)와 현재 WSL 관측은 시점이 다르므로 옛 기록을 덮어쓰지 않았다. 배포판·Windows 기능·가상화·GPU driver·원격 runner를 설치하거나 설정하지 않았다. WSL에 환경을 설치하더라도 가상/software 장치 성공만으로 물리 Linux GPU 수용을 충족시키지 않는다.

## 준비한 실행 경로

[`linux_hardware.py`](../tools/linux_hardware.py)는 두 단계를 제공한다. 기존 SDK C++·공개 API·root `CMakeLists.txt`·공통 suite는 변경하지 않았다.

1. **`preflight`**: 지정 ICD 하나를 child 환경에만 적용하고 SDK Core와 기존 독립 native device probe를 빌드한다. 두 새 process에서 native UUID/driver/version과 SDK ID·정확한 장치 생성·잘못된 ID/종류의 거부를 확인한다. 사용자가 지정한 UUID 집합이 보이는 장치 집합과 정확히 같아야 한다. UUID-derived 64-bit ID는 문자열로 대조한다. CPU/software·virtual/other 종류, 누락/중복/다른 UUID, 불완전한 native metadata는 거부한다. 같은 vendor/device를 가진 PCI GPU와 Linux driver(`nvidia`, `amdgpu`, `i915`, `xe`, `nouveau`) 관측도 필요하다. PCI 기록은 native UUID와 별도 보조 근거이며 UUID별 PCI 주소를 직접 매핑했다는 주장은 아니다. 실제 로드한 loader/ICD 파일과 probe/SDK binary 해시를 보존한다.
2. **`accept`**: 성공한 같은 source/UUID/ICD preflight와 고정 VVL runtime이 있어야 시작한다. 기존 공통 suite 8개를 in-tree CTest와 설치 소비자에서 각각 `validation=1`로 실행하고, workload·참조·정확한 장치 집합·정상 오류 계수 0·실제 layer 로드를 검사하도록 구현했다. 누락 layer 8개 거부와 native core/sync/callback controls도 실행한다. 장치·driver·SDK·runtime이 그대로인 경우에만 `hardware_GPU_accepted=true` receipt를 생성한다. **이 단계의 물리 GPU 성공 경로는 이번에 실행하지 못했으므로 수용 미완료다.**

초기 도구 실행 `a8668d9`는 validation preset에 `GFX_BUILD_TESTS=OFF`를 조합해 CMake configure에서 중단됐다. [실패 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-hardware-readiness/failed-a8668d9-job.log)를 보존했다. `0ee9042`에서는 정상 preset을 유지하고 preflight의 빌드 target만 `GraphicsSDKCore`로 제한했다. 일반 빌드 오류를 의도된 software 거부로 통과시키지 않는다.

## 실제로 검증한 범위

Ubuntu의 llvmpipe `LLVM 20.1.2`를 선택해 SDK와 native probe를 실제 빌드·설치·실행했다. 두 process의 결과가 같았고 native `device_type=4`, `software_adapters=1`, `hardware_adapters=0`이었다. ID 문자열은 `5892941257450743403`, device UUID는 `6d65736132352e322e382d3075627500`이다.

도구는 exit 1과 `Physical GPU required; software/CPU or empty candidate set rejected`로 거부했다. Preflight의 두 acceptance flag는 모두 false이고 최종 `receipt.json`은 생성되지 않았다. CI는 이 정확한 거부를 확인해 성공했다. 임의 UUID를 지정한 것은 거부 대조용이며 하드웨어 장치 선택 성공으로 기록하지 않는다.

[`test_linux_hardware.py`](../tests/test_linux_hardware.py)의 합성 대조 14개도 통과했다. GPU 종류·source·UUID·큰 ID의 문자열 타입·driver metadata·PCI driver·생성/거부 controls·validation·workload를 검사하며, 합성 positive model을 실제 GPU 관측으로 합산하지 않는다.

[같은 소스의 전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40197045611)는 Windows/Linux Python 각각 **654개·오류/실패/건너뜀 0**, Windows CTest 37개·설치/감사/소비자와 Linux CTest 11개를 통과했다. [native Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40196873742)는 직전 `a8668d9`에서 CPU 22개를 통과했으며 후속 수정은 preflight 빌드 호출뿐이다. 이 CPU/hosted CI 결과는 물리 GPU 실행이 아니다.

Native SDK fingerprint는 기존 `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830`, Core archive는 413,918 bytes / `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 그대로다. GitHub artifact digest와 내려받은 거부 기록을 대조하고, 이전 [P3-LNX-14](PHASE3_PYTHON_CI_REPORT.md)의 640개 ID를 유지한 채 대조 14개가 추가됐음을 확인했다. 실제 파일/checkout fingerprint는 플랫폼별로 보존한다.

## 실제 GPU 환경이 준비되면

[원본 준비 검증 도구](../benchmarks/reports/validation/2026-10-25-phase3-linux-hardware-readiness/verify_readiness.py)는 artifact·거부 원본·전체 CI ID·SDK 보존을 다시 검사한다. 이것은 실제 하드웨어 수용 검증 도구의 성공 실행을 대신하지 않는다. [수동 실행 조회](../benchmarks/reports/validation/2026-10-25-phase3-linux-hardware-readiness/manual-run-status.json)도 이번 관측 시점 0회였으며, 모든 등록 runner의 부재를 증명하는 결과는 아니다.

필요한 정보는 **접속 가능한 Linux SSH 대상 또는 이미 등록된 GPU runner, 사용할 물리 ICD JSON 경로, 검증할 device UUID 집합**이다. `vulkaninfo --summary`의 UUID에서 구분용 하이픈을 제거한 소문자 32자리 값을 사용한다. 하나의 ICD에 여러 장치가 보이면 모두 지정하고 모두 검사하며 임의의 한 장치로 fallback하지 않는다.

새 checkout의 `.deps/Vulkan-Headers`에는 고정 commit `b379292b2ab6df5771ba9870d53cf8b2c9295daf`가 필요하다. Python 3, CMake≥3.25, Ninja, g++-14와 작동하는 Linux Vulkan loader/물리 ICD·driver·vulkaninfo가 준비돼 있어야 한다. 실행은 항상 새 output/build/install 경로에서 수행하며 기존 근거를 지우지 않는다.

```sh
python3 tools/linux_hardware.py preflight --icd /absolute/path/to/physical-icd.json --device-uuids <exact-uuid-without-hyphens>
python3 tools/prepare_linux_validation.py
python3 tools/linux_hardware.py accept --icd /absolute/path/to/physical-icd.json --device-uuids <same-exact-uuid>
```

[수동 workflow](../.github/workflows/linux-hardware-acceptance.yml)는 main에서만 수동 실행하며 `self-hosted, Linux, X64, graphicssdk-gpu` label이 모두 있는 runner를 요구한다. 각 run/attempt마다 별도 checkout을 사용하고 checkout 청소를 끈다. 사용자 입력은 환경 변수로 전달해 인용된 인자로 처리한다. Job별 working directory와 수동 입력은 [GitHub 공식 workflow 문법](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)을 따른다. 이번에는 이 workflow를 dispatch하거나 새 runner를 등록하지 않았다.

실제 결과가 확보되면 장치/driver·원본 실행·참조·validation·runtime 해시를 독립 대조하고 그때 `P3-LNX-15` 완료를 판단한다. 최종 `P3-GATE-01`의 필수 의존성은 유지한다. WSI·새 성능·장시간 메모리·Intel 충돌 원인 수용은 별개이며 이번 작업으로 변경하지 않는다.

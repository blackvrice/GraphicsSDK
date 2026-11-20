# 다른 AI를 위한 개발 인수인계

2026-11-20 갱신. 직전 확인 기준 HEAD는 `04d9e4dd`이며 이번 세션에서 `P9-CMD-04`(동적 Viewport 설정 `setViewport`) 구현·검증과 `P9-CMD-05`(Linux/Metal 실행 검증) 분리를 반영했다.
이 값은 인수인계 기준이며 새 세션에서는 반드시 실제 `git status`, `git log`, `docs/ROADMAP_TASKS.json`을 다시 확인한다.
기존 작업 방식과 코드 품질 기준은 루트 [AGENTS.md](../AGENTS.md)를, 시간순 세션 이력은 [AI_WORK_LOG.md](AI_WORK_LOG.md)를 따른다.

## 현재 인계 요약

| 항목 | 내용 |
| --- | --- |
| 마지막 작업 | `P9-CMD-04` (`Viewport`, `validateViewport`, `CommandList::setViewport` D3D12/Vulkan/Metal 구현 및 Windows D3D12/Vulkan/Validation/WARP·설치 소비자 검증), `P9-CMD-05` 분리 |
| 마지막 확인 Commit | 직전 HEAD `04d9e4dd` (이번 세션 커밋은 `git log -1`로 확인) |
| 현재 브랜치 | `main` (`origin`: `https://github.com/blackvrice/GraphicsSDK.git`) |
| 현재 버전 | `0.0.1` (`v0.0.7` 동적 Viewport 슬라이스 진행 중) |
| 작업 중인 기능 | `P8-XFER-04/06`, `P8-CAP-02`, `P9-CMD-05` (Metal/Linux 실행 검증 대기) · 활성 필수 Phase 3 (`P3-LNX-15`) |
| 완료한 내용 | `copyBufferRegion`(`P8-XFER-01/02/03`), 부분 2D 텍스처 복사(`P8-XFER-05`), GPU 한도·포맷 조회(`P8-CAP-01`), 동적 Viewport(`P9-CMD-04`) 공개 API·D3D12·Vulkan·Metal 구현 및 Windows D3D12/Vulkan/Validation/WARP/설치 소비자 검증 통과, `ROADMAP_TASKS.json` 194 ID(112 완료) 검증 통과 |
| 남은 내용 | 필수 잔여 38 (`P3-LNX-15`, `P3-WSI-08`, `P2-ACC-01/02/04/05`, Phase 4/5/7 수용 등), 조건부 8, 후속 확장 36 (`P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-*`, `P8-MEM-*`, `P9-CMD-01/05`, `P9-CMP-*`, `P10`~`P12`) |
| 수정 파일 | `include/graphics/Types.hpp`, `include/graphics/RenderPass.hpp`, `include/graphics/CommandList.hpp`, `src/foundation/RenderPass.cpp`, `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`, `tests/RenderPassTests.cpp`, `tests/ViewportTests.cpp`, `CMakeLists.txt`, `docs/PHASE9_VIEWPORT_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_WORK_LOG.md`, `docs/AI_HANDOFF.md` |
| 테스트 결과 | Windows D3D12/Vulkan/WARP/GBV/고정 VVL/설치 소비자 5모드 및 CTest 18/18 통과 (`assertions=57366/57368/28684`, `errors=0`), `roadmap.py --check` PASS (194 ID) (Linux/macOS 실기 미실행) |
| 확인된 문제 | 기존 로컬 `out/core`의 `fence_wait_gpu`/`fence_wait_validation` SegFault 2건, 장시간 GBV 메모리(`P2-ACC-01`)·과거 Intel AV(`P2-ACC-02`) 미해결, Linux 물리 GPU·물리 Mac 부재 |
| 다음 추천 작업 | `P8-TEX-01`(Texture View·Subresource View), `P9-CMP-01`(Compute Shader·UAV·Dispatch), `P9-CMD-01`(명시적 Command Pool·재사용), `P8-XFER-04/06`·`P8-CAP-02`·`P9-CMD-05`(CI Linux/Metal 검증), 또는 Phase 7 CI 배포 후보 연동 |
| 주의사항 | `tools/macos_metal_package.py`의 13개 공개 헤더 allowlist와 `tools/macos_core.py`의 5개 `CORE` `.cpp`·11개 CPU 테스트 목록을 유지해야 기존 replay와 `macos-core.yml`이 깨지지 않는다. 기존 사용자 변경(`tests/VulkanAllocationBenchmarkTests.cpp`, `.idea/editor.xml`)은 덮어쓰거나 커밋하지 않는다. |

## 새 AI에게 전달할 지시문

아래 문장을 새 AI의 첫 작업 지시로 사용할 수 있다.

> GraphicsSDK 저장소의 AGENTS.md와 docs/AI_HANDOFF.md를 읽고 기존 개발 규칙과
> 코딩 스타일을 유지하며 이어서 개발해줘. 먼저 Git 상태와 ROADMAP_TASKS.json,
> 최근 보고서를 확인해 완료된 작업을 반복하지 마.
> 내가 지정한 작업을 진행하고, 다음 개발을 요청하면 현재 환경에서 끝낼 수 있는
> 항목을 골라 실제 구현·빌드·검증·문서·로드맵 반영까지 완료해줘.
> 내 기존 변경은 보존하고 이번에 작업한 파일만 커밋한 뒤 지정 원격에 푸시해줘.
> 실제 실행 범위와 남은 수용 조건을 최종 결과에 알려줘.

## 먼저 읽을 문서

| 목적 | 문서 |
| --- | --- |
| 필수 규칙·코딩 스타일·Git 처리 | [AGENTS.md](../AGENTS.md) |
| 작업별 상태·선행 조건·완료 근거 | [ROADMAP_TASKS.json](ROADMAP_TASKS.json), [생성된 로드맵](ROADMAP.md) |
| 독립성·소유권·backend 구조 | [ARCHITECTURE](ARCHITECTURE.md), [RHI_DESIGN](RHI_DESIGN.md) |
| 공개 API·스레드·오류 계약 | [PUBLIC_API](PUBLIC_API.md) |
| 빌드와 검증 | [BUILD](BUILD.md), [TESTING](TESTING.md) |
| Docker·GitHub Actions·다른 OS | [CROSS_PLATFORM_TESTING](CROSS_PLATFORM_TESTING.md) |
| 공정한 비교·최적화 근거 | [BENCHMARK_DESIGN](BENCHMARK_DESIGN.md), [OPTIMIZATION_LOG](OPTIMIZATION_LOG.md) |
| 고정 의존성·고지·컴파일 입력 | [DEPENDENCY_LICENSES](DEPENDENCY_LICENSES.md), [라이선스 정책](../licenses/README.md) |

문서 제목의 기준일과 테스트 수는 각 실행의 범위다. 현재 완료 상태는 JSON의
개별 ID와 근거를 사용한다. 구현·보고서가 이미 있는 후속 Phase도 선행 수용이
끝나지 않아 `todo`일 수 있다.

## 프로젝트 목표와 현재 상태

GraphicsSDK는 C++23의 독립 범용 그래픽스 SDK이며 Windows D3D12,
Windows/Linux Vulkan, macOS Metal을 대상으로 한다. 경쟁 SDK를 고정해
공정한 벤치마크로 병목을 찾고, 정확성·안정성·유지보수성을 유지하며 개선한다.
확장 기능은 단계별 로드맵과 승인된 지원 범위에 따라 개발한다.

현재 개발 스냅샷은 `0.0.1`이고 로드맵의 active phase는 3이다.
Windows 기반 기능, Linux software·XCB 검사, 호스팅 macOS Metal,
Shader·최적화 후보와 배포 도구의 구현·실행 자료가 누적돼 있다.
Phase 3 최종 수용과 전체 크로스플랫폼 릴리스는 미완료다.

| 최근 완료 ID | 현재 재사용할 산출물과 근거 |
| --- | --- |
| `P7-PKG-09/10` | [Linux 헤드리스](PHASE7_LINUX_RELEASE_CANDIDATE.md)·[XCB](PHASE7_LINUX_XCB_RELEASE_CANDIDATE.md) ZIP과 재배치 설치 소비자 |
| `P7-PKG-11` | [Linux DWARF 분리와 재링크](PHASE7_LINUX_DEBUG_SYMBOLS.md) |
| `P7-PKG-12` | [macOS arm64·Intel Metal 배포 후보](PHASE7_MACOS_RELEASE_CANDIDATE.md) |
| `P7-PKG-13` | [Windows D3D12·Vulkan 배포 후보](PHASE7_WINDOWS_RELEASE_CANDIDATE.md) |
| `P7-PKG-14` | [여섯 프로필의 설치 ZIP과 Linux 디버그 ZIP 무결성 검사](PHASE7_COMMON_CANDIDATE_AUDIT.md) |
| `P7-PKG-15` | [Windows 후보의 WARP·하드웨어 렌더링과 Vulkan 전송·그리기](PHASE7_WINDOWS_CANDIDATE_GPU.md) |

마지막 실행은 D3D12 WARP와 GTX 1660 SUPER의 각 1,024픽셀,
Vulkan GTX 1660 SUPER·Intel UHD 770의 전송 48회와 이미지 66개를 확인했다.
그 실행은 validation 계층과 창 표시를 요청하지 않았다.
GPU·드라이버와 모든 실행 환경은 새 세션에서 확인한다.

## 현재 작업 영역 인수 시 주의

2026-11-20 확인한 체크아웃은 `D:\source\GraphicsSDK`, 브랜치는 `main`,
origin은 `https://github.com/blackvrice/GraphicsSDK.git`이다.
다른 컴퓨터에서는 경로를 바꾸고 실제 remote와 인증을 확인한다.

이 시점에 이미 존재한 사용자 변경은 `tests/VulkanAllocationBenchmarkTests.cpp`,
미추적 파일은 `.idea/editor.xml`이다. 새 AI의 작업 변경으로 취급하지 않는다.
새 체크아웃에서는 실제 `git status`로 소유 범위를 다시 확인한다.

배포 ZIP·원본 로그·빌드 결과는 대부분 Git에서 제외된 `out/`에 있다.
이 저장소만 clone하면 해당 파일도 전부 복구된다고 가정하지 않는다.
원본 해시와 실행 receipt를 확인하고 해당 보고서의 재현 절차로 입력을 준비한다.
GitHub 아티팩트는 해당 workflow의 보관 기간과 다운로드 가능 여부를 확인한다.

로컬에서 확인한 주요 경로는 다음과 같다.

| 경로 | 용도 |
| --- | --- |
| `out/phase7-windows-release` | D3D12·Vulkan ZIP과 매니페스트 |
| `out/phase7-linux-dwarf/{headless,xcb}/split` | 일반·디버그 ZIP과 매니페스트. 중괄호는 두 경로의 축약 표기 |
| `out/phase7-macos-release-42750332817` | arm64·Intel ZIP과 매니페스트 |
| `out/phase7-common-audit/receipt.json` | 여섯 프로필·여덟 ZIP의 공통 검사 결과 |
| `out/phase7-windows-gpu-smoke/receipt.json` | 실제 Windows GPU 소비자의 입력·실행·로그 해시 |

## 다음에 진행할 수 있는 개발

직전 사용자 요청 흐름은 실물 Linux GPU나 Mac 없이 가능한 작업을 이어가는 것이다.
다음 후보는 배포 후보 생성·설치 smoke와 아티팩트 보관을 CI에 연결하는 작업이다.
기존 `build.yml`, `linux-vulkan-package.yml`, `macos-core.yml`과 현재 runner를 먼저
확인하고 중복 workflow·도구를 늘리지 않도록 한다. 큰 작업을 분리할 때만 새 ID를 만든다.
`P7-PKG-05/06/07`의 전체 완료는 각 선행 조건과 최종 수용 증거를 충족할 때 판단한다.

실제 환경이 필요한 항목은 다음과 같다.

- `P3-LNX-15`와 Linux 경쟁 성능 비교는 물리 GPU·ICD·정확한 UUID와 같은 환경의 실행이 필요하다.
- `P3-WSI-08`은 사람이 화면을 볼 수 있는 실제 Linux X11 데스크톱과 창 조작이 필요하다.
- 물리 Mac의 표시·성능은 호스팅 macOS의 가상 Metal 실행 자료와 별도로 수용해야 한다.
- 실제 DPI·다중 모니터, TDR/hot-unplug 수용은 해당 장비·사건·관측 증거가 필요하다.

장시간 메모리 기준과 과거 Intel 충돌은 사용자가 건너뛰고 개발을 진행하라고 한 흐름을
유지한다. 로드맵에는 이후 조사 이력도 남아 있으므로 최신 사용자 지시와 해당 보고서를
함께 확인한다. 원인이 하드웨어라고 확정하거나 해당 ID를 자동 완료로 바꾸지 않는다.
장시간 stress·충돌 조사를 기본 회귀로 자동 재개하지 않는다.

## 작업과 검증 명령

먼저 현재 상태와 선택할 ID를 확인한다.

```powershell
git status --short --branch
git log -5 --oneline
python tools/roadmap.py --next
python tools/roadmap.py --task P7-PKG-06
python tools/roadmap.py --check
```

Windows의 기본 preset은 `out/core`를 사용한다. 기존 폴더의 입력·실험 목적을
확인한 뒤 실행하고, 새 프로필은 별도 build/install/output 경로를 사용한다.
GPU 테스트는 기존 runner의 직렬 실행·제외·환경 정책을 먼저 읽는다.
아래 예시는 사용자의 메모리 조사 건너뜀 지시에 맞춰 `memory_reclamation_` 세 항목을
제외한다. 선택·제외 목록을 실행 결과에 기록하고 해당 항목의 수용 상태를 유지한다.

```powershell
cmake --preset windows
cmake --build --preset windows-release --parallel 1
ctest --preset windows-release --output-on-failure --exclude-regex '^memory_reclamation_'
```

변경에 맞는 Python 테스트는 아래와 같이 실행한다.

```powershell
python -m unittest discover -s tests -p 'test_windows_release_gpu.py'
python -m unittest discover -s tests -p 'test_release_candidate_audit.py'
# 전체 Python 실행 자료가 필요한 작업에만 새 출력 경로를 지정한다.
python tools/check_python_contracts.py --output out/python-contracts-new
```

이 Windows 환경에서는 PATH의 `python.exe`가 실행 별칭일 수 있다.
실제 사용 가능한 Python 3.12 이상을 확인한다. 앞선 실행에서 사용한 경로는
`C:/Users/black/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`다.
한국어 문서를 PowerShell로 읽을 때 `Get-Content -Encoding UTF8`를 사용한다.

로드맵을 변경한 작업은 아래 검사를 추가한다. 문서만 수정할 때는 링크와
변경분을 확인하고 소스·GPU 테스트는 변경 범위에 맞춰 선택한다.

```powershell
python tools/roadmap.py --write
python tools/roadmap.py --check
git diff --check
git diff --stat
```

## 커밋과 사용자 보고

사용자는 개발 후 커밋·푸시까지 요청했다. 현재 지시가 유효한 작업은 관련 검증을
마친 뒤 파일을 명시적으로 stage하고 커밋한다. 새 세션의 접근 권한·승인 절차는
그 환경에서 따르며, 사용자와 관계없는 저장소로 전송하지 않는다.
푸시 성공 뒤 `git rev-parse HEAD origin/main`과 `git status --short --branch`로
일치 여부 및 남은 사용자 변경을 확인한다.

마지막 답변은 한국어로 완료 ID, 구현 효과, 실행한 검증과 남은 조건,
문서·산출물 링크와 커밋을 간결하게 전달한다. 시간 경과나 준비만으로
완료를 선언하지 않고 실제 결과를 근거로 보고한다.
